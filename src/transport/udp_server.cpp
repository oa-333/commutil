#include "transport/udp_server.h"

#include "commutil_common.h"
#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(UdpServer)

#define COMMUTIL_UDP_CONN_MAP_FACTOR 4

ErrorCode UdpServer::initializeTransport(uv_loop_t* serverLoop, uv_handle_t*& transport) {
    ErrorCode rc = m_connectionMap.initialize(m_maxConnections * COMMUTIL_UDP_CONN_MAP_FACTOR);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize UDP connection map: %s", errorCodeToString(rc));
        return rc;
    }

    int res = uv_udp_init(serverLoop, &m_socketHandle);
    if (res < 0) {
        LOG_UV_ERROR(uv_tcp_init, res, "Failed to initialize UDP socket");
        m_connectionMap.destroy();
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    transport = (uv_handle_t*)&m_socketHandle;

    // prepare server address
    struct sockaddr_in dest;
    res = uv_ip4_addr(m_serverAddress.c_str(), m_port, &dest);
    if (res < 0) {
        LOG_UV_ERROR(uv_ip4_addr, res, "Failed to prepare UDP address");
        uv_close((uv_handle_t*)&m_socketHandle, nullptr);  // no need to fire disconnect
        m_connectionMap.destroy();
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    res = uv_udp_bind(&m_socketHandle, (const struct sockaddr*)&dest, UV_UDP_REUSEADDR);
    if (res < 0) {
        LOG_UV_ERROR(uv_udp_bind, res, "Failed to bind UDP server socket");
        uv_close((uv_handle_t*)&m_socketHandle, nullptr);  // no need to fire disconnect
        m_connectionMap.destroy();
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // start receive chain
    res = uv_udp_recv_start(&m_socketHandle, onAllocBufferStatic, onRecvStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_udp_recv_start, res, "Failed to start receiving on UDP server");
        uv_close((uv_handle_t*)&m_socketHandle, nullptr);  // no need to fire disconnect
        m_connectionMap.destroy();
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // start expiry timer
    res = uv_timer_init(serverLoop, &m_expireTimer);
    if (res < 0) {
        LOG_UV_ERROR(uv_timer_init, res, "Failed to initialize UDP pseudo-connection expiry timer");
        uv_close((uv_handle_t*)&m_socketHandle, nullptr);  // no need to fire disconnect
        m_connectionMap.destroy();
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    m_expireTimer.data = this;
    return ErrorCode::E_OK;
}

ErrorCode UdpServer::terminateTransport() {
    m_connectionMap.destroy();
    return ErrorCode::E_OK;
}

ErrorCode UdpServer::startTransport() {
    int res = uv_timer_start(&m_expireTimer, onTimerStatic, m_clientExpireSeconds * 1000,
                             m_clientExpireSeconds * 1000);
    if (res != 0) {
        LOG_UV_ERROR(uv_timer_start, res, "Failed to start UDP pseudo-connection expiry timer");
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

ErrorCode UdpServer::stopTransport() {
    int res = uv_timer_stop(&m_expireTimer);
    if (res != 0) {
        LOG_UV_ERROR(uv_timer_stop, res, "Failed to stop pseudo-connection expiry timer");
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

ErrorCode UdpServer::writeTransport(ServerBufferData* serverBufferData) {
    ConnectionData* connData = m_connDataArray[serverBufferData->m_connectionIndex];
    ConnectionDetails& connectionDetails = connData->m_connectionDetails;
    struct sockaddr_in dest;
    int res = uv_ip4_addr(connectionDetails.getHostName(), connectionDetails.getPort(), &dest);
    if (res < 0) {
        LOG_UV_ERROR(uv_ip4_addr, res, "Failed to prepare UDP address");
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    uv_udp_send_t* sendReq = m_dataAllocator->allocateUdpSendRequest();
    if (sendReq == nullptr) {
        LOG_ERROR("Failed to allocate UDP send request, out of memory");
        return ErrorCode::E_NOMEM;
    }
    sendReq->data = serverBufferData;
    res = uv_udp_send(sendReq, &m_socketHandle, &serverBufferData->m_buf, 1,
                      (const struct sockaddr*)&dest, onSendStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_udp_send, res, "Failed to send UDP reply message");
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

UdpServer::ConnectionData* UdpServer::createConnectionData() {
    UdpConnectionData* connData = new (std::nothrow) UdpConnectionData();
    if (connData == nullptr) {
        LOG_ERROR("Failed to allocate UDP connection data object, out of memory");
    } else {
        connData->m_connectionDetails.setNetAddress(m_serverAddress.c_str(), m_port);
    }
    return connData;
}

void UdpServer::onRecvStatic(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                             const struct sockaddr* addr, unsigned flags) {
    // cast carefully here...
    UdpServer* dataServer = (UdpServer*)(DataServer*)handle->data;
    dataServer->onRecv(handle, nread, buf, addr, flags);
}

void UdpServer::onSendStatic(uv_udp_send_t* req, int status) {
    ServerBufferData* serverBufferData = (ServerBufferData*)req->data;
    UdpServer* dataServer = (UdpServer*)serverBufferData->m_dataServer;
    dataServer->onWrite(serverBufferData, status);
    dataServer->m_dataAllocator->freeUdpSendRequest(req);
}

void UdpServer::onRecv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                       const struct sockaddr* addr, unsigned flags) {
    (void)flags;

    // cast carefully here...
    UdpServer* dataServer = (UdpServer*)(DataServer*)handle->data;

    // NOTE: if nread == 0 (e.g. would-block error), we still must release the buffer, but the addr
    // parameter is null is this case, so we can't call the code below, instead we relase the buffer
    // explicitly (this is a special case only in UdpServer)
    if (nread == 0) {
        // release buffer
        if (buf != nullptr && buf->base != nullptr) {
            dataServer->getDataAllocator()->freeRequestBuffer(buf->base);
        }
        return;
    }

    // convert IP address to integer and use concurrent hash table to get connection index and
    uint64_t ipAddr = ((const sockaddr_in*)addr)->sin_addr.s_addr;
    ipAddr = ipAddr << 32 | ((const sockaddr_in*)addr)->sin_port;

    // get connection data from hash table or create new one
    bool newConnection = false;
    uint64_t connectionIndex = 0;
    UdpConnectionData* connData = nullptr;
    uint32_t entryId = m_connectionMap.getItem(ipAddr, connectionIndex);
    if (entryId == COMMUTIL_INVALID_CHT_ENTRY_ID) {
        // TODO: do not allocate new connection if there is an error
        // grab a connection if there is such one available
        connData = (UdpConnectionData*)grabConnectionData();
        if (connData == nullptr) {
            LOG_ERROR(
                "Failed to allocate connection for new UDP client, ran out of free connections");
            return;
        }

        // fill initial connection details and put in UDP connection map
        connData->m_connectionDetails.setHostDetails((const sockaddr_in*)addr);
        connData->m_connectionHandle = (uv_handle_t*)handle;
        entryId = m_connectionMap.setItem(ipAddr, connData->m_connectionId);
        if (entryId == COMMUTIL_INVALID_CHT_ENTRY_ID) {
            LOG_ERROR("Cannot set connection data in connection map, map is full");
            connData->m_isUsed.store(0, std::memory_order_relaxed);
            return;
        }
        connData->m_entryId = entryId;
        newConnection = true;
    } else {
        connData = (UdpConnectionData*)m_connDataArray[connectionIndex];
        // TODO: check for error
    }
    if (nread > 0) {
        connData->m_lastAccessTimeMillis = getCurrentTimeMillis();
    }

    if (newConnection) {
        m_dataListener->onConnect(connData->m_connectionDetails, 0);
    }
    dataServer->onRead(connData, nread, buf, true);
}

void UdpServer::onTimerStatic(uv_timer_t* handle) {
    UdpServer* server = (UdpServer*)handle->data;
    server->onTimer(handle);
}

void UdpServer::onTimer(uv_timer_t* handle) {
    (void)handle;
    // check all connections
    uint64_t currTimeMillis = getCurrentTimeMillis();
    for (uint32_t i = 0; i < m_maxConnections; ++i) {
        UdpConnectionData* connData = (UdpConnectionData*)m_connDataArray[i];
        if (connData->m_isUsed &&
            (currTimeMillis - connData->m_lastAccessTimeMillis) >= m_clientExpireSeconds * 1000) {
            // client expired, "disconnect"
            m_dataListener->onDisconnect(connData->m_connectionDetails);
            connData->m_isUsed = 0;
            m_connectionMap.removeAt(connData->m_entryId);
        }
    }
}

}  // namespace commutil
