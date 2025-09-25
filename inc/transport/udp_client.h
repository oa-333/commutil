#ifndef __UDP_CLIENT_H__
#define __UDP_CLIENT_H__

#include "comm_util_log.h"
#include "transport/data_client.h"

namespace commutil {

class COMMUTIL_API UdpClient : public DataClient {
public:
    /**
     * @brief Construct a UDP client object.
     *
     * @param serverAddress The server's address.
     * @param port The server's port.
     */
    UdpClient(const char* serverAddress, int port)
        : DataClient(ByteOrder::NETWORK_ORDER), m_serverAddress(serverAddress), m_port(port) {}
    UdpClient(const UdpClient&) = delete;
    UdpClient(UdpClient&&) = delete;
    UdpClient& operator=(const UdpClient&) = delete;
    ~UdpClient() override {}

protected:
    // initialize the transport and connect if this is a stream transport
    // any derived class should redirect incoming data to
    ErrorCode initializeTransport(uv_loop_t* clientLoop, uv_handle_t*& transport) override;

    // starts the transport machinery running (e.g. issue connect request)
    ErrorCode startTransport() override;

    // starts the transport machinery running (e.g. issue connect request)
    ErrorCode stopTransport() override;

    // sends a data buffer through the transport
    // any derived class should redirect the callback to onWriteStatic
    ErrorCode writeTransport(ClientBufferData* clientBufData) override;

private:
    std::string m_serverAddress;
    int m_port;
    uv_udp_t m_socketHandle;

    // handle read request done event
    static void onRecvStatic(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                             const struct sockaddr* addr, unsigned flags);

    // handle send request done event
    static void onSendStatic(uv_udp_send_t* req, int status);

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __UDP_CLIENT_H__