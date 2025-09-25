#ifndef __TCP_CLIENT_H__
#define __TCP_CLIENT_H__

#include "comm_util_log.h"
#include "transport/data_stream_client.h"

namespace commutil {

class COMMUTIL_API TcpClient : public DataStreamClient {
public:
    /**
     * @brief Construct a TCP client object.
     *
     * @param serverAddress The server's address.
     * @param port The server's port.
     */
    TcpClient(const char* serverAddress, int port, uint64_t reconnectTimeoutMillis)
        : DataStreamClient((uv_stream_t*)&m_socketHandle, reconnectTimeoutMillis,
                           ByteOrder::NETWORK_ORDER),
          m_serverAddress(serverAddress),
          m_port(port) {}
    TcpClient(const TcpClient&) = delete;
    TcpClient(TcpClient&&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    ~TcpClient() override {}

protected:
    // initialize the transport and connect if this is a stream transport
    // any derived class should redirect incoming data to
    ErrorCode initializeStreamTransport(uv_loop_t* clientLoop, uv_handle_t*& transport) override;

    // starts the transport machinery running (e.g. issue connect request)
    ErrorCode startStreamTransport() override;

private:
    std::string m_serverAddress;
    int m_port;
    uv_tcp_t m_socketHandle;

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __TCP_CLIENT_H__