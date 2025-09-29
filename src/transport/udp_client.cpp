#include "transport/udp_client.h"

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(UdpClient)

ErrorCode UdpClient::initializeTransport(uv_loop_t* clientLoop, uv_handle_t*& transport) {
    int res = uv_udp_init(clientLoop, &m_socketHandle);
    if (res < 0) {
        LOG_UV_ERROR(uv_tcp_init, res, "Failed to initialize UDP socket");
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    transport = (uv_handle_t*)&m_socketHandle;
    m_connectionDetails.setNetAddress(m_serverAddress.c_str(), m_port);
    return ErrorCode::E_OK;
}

ErrorCode UdpClient::startTransport() {
    // prepare server address
    struct sockaddr_in dest;
    int res = uv_ip4_addr(m_serverAddress.c_str(), m_port, &dest);
    if (res < 0) {
        LOG_UV_ERROR(uv_ip4_addr, res, "Failed to prepare UDP address");
        uv_close((uv_handle_t*)&m_socketHandle, nullptr);  // no need to fire disconnect
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // start receive chain
    res = uv_udp_recv_start(&m_socketHandle, onAllocBufferStatic, onRecvStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_udp_recv_start, res, "Failed to start receiving on UDP client");
        uv_close((uv_handle_t*)&m_socketHandle, nullptr);  // no need to fire disconnect
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // fire a pseudo-connect notification to allow uniform handling in upper layer
    ConnectionDetails connectionDetails(m_serverAddress.c_str(), m_port, 0, 0);
    m_dataListener->onConnect(connectionDetails, 0);

    return ErrorCode::E_OK;
}

ErrorCode UdpClient::stopTransport() {
    int res = uv_udp_recv_stop(&m_socketHandle);
    if (res < 0) {
        LOG_UV_ERROR(uv_udp_recv_stop, res, "Failed to stop UDP receive");
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

ErrorCode UdpClient::writeTransport(ClientBufferData* clientBufferData) {
    struct sockaddr_in dest;
    int res = uv_ip4_addr(m_serverAddress.c_str(), m_port, &dest);
    if (res < 0) {
        LOG_UV_ERROR(uv_ip4_addr, res, "Failed to prepare UDP address");
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    uv_udp_send_t* sendReq = m_dataAllocator->allocateUdpSendRequest();
    if (sendReq == nullptr) {
        LOG_ERROR("Failed to allocate UDP send request, out of memory");
        return ErrorCode::E_NOMEM;
    }
    sendReq->data = clientBufferData;
    res = uv_udp_send(sendReq, &m_socketHandle, &clientBufferData->m_buf, 1,
                      (const struct sockaddr*)&dest, onSendStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_udp_send, res, "Failed to send UDP message");
        m_dataAllocator->freeUdpSendRequest(sendReq);
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    sockaddr_in localAddr;
    int len = sizeof(sockaddr_in);
    if (uv_udp_getsockname(&m_socketHandle, (struct sockaddr*)&localAddr, &len) >= 0) {
        char address[INET_ADDRSTRLEN];
        const char* hostName = inet_ntop(AF_INET, &localAddr.sin_addr, address, sizeof(address));
        int port = ntohs(localAddr.sin_port);
        LOG_TRACE("Client UDP address is: %s: %d", hostName, port);
    }

    return ErrorCode::E_OK;
}

void UdpClient::onRecvStatic(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                             const struct sockaddr* addr, unsigned flags) {
    (void)addr;
    (void)flags;
    // cast carefully here...
    UdpClient* dataClient = (UdpClient*)(DataClient*)handle->data;
    dataClient->onRead(nread, buf, true);
}

void UdpClient::onSendStatic(uv_udp_send_t* req, int status) {
    ClientBufferData* clientBufferData = (ClientBufferData*)req->data;
    // cast to self so we can access members
    UdpClient* dataClient = (UdpClient*)clientBufferData->m_dataClient;
    dataClient->onWrite(clientBufferData, status);
    dataClient->m_dataAllocator->freeUdpSendRequest(req);
}

}  // namespace commutil
