#include "transport/tcp_client.h"

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(TcpClient)

ErrorCode TcpClient::initializeStreamTransport(uv_loop_t* clientLoop, uv_handle_t*& transport) {
    int res = uv_tcp_init(clientLoop, &m_socketHandle);
    if (res < 0) {
        LOG_UV_ERROR(uv_tcp_init, res, "Failed to initialize TCP socket");
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    transport = (uv_handle_t*)&m_socketHandle;
    m_connectionDetails.setNetAddress(m_serverAddress.c_str(), m_port);
    return ErrorCode::E_OK;
}

ErrorCode TcpClient::startStreamTransport() {
    // prepare server address
    uv_connect_t* connectReq = new (std::nothrow) uv_connect_t();
    if (connectReq == nullptr) {
        LOG_ERROR("Failed to allocate connect request, out of memory");
        return ErrorCode::E_NOMEM;
    }
    struct sockaddr_in dest;
    int res = uv_ip4_addr(m_serverAddress.c_str(), m_port, &dest);
    if (res < 0) {
        LOG_UV_ERROR(uv_ip4_addr, res, "Failed to prepare TCP address");
        uv_close((uv_handle_t*)&m_socketHandle, nullptr);  // no need to fire disconnect
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // issue connect to server
    // TODO: periodic reconnect attempts should take place
    connectReq->data = this;
    res =
        uv_tcp_connect(connectReq, &m_socketHandle, (const struct sockaddr*)&dest, onConnectStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_tcp_connect, res, "Failed to connect to TCP server");
        delete connectReq;
        uv_close((uv_handle_t*)&m_socketHandle, nullptr);  // no need to fire disconnect
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    return ErrorCode::E_OK;
}

}  // namespace commutil
