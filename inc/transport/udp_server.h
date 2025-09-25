#ifndef __UDP_SERVER_H__
#define __UDP_SERVER_H__

#include "comm_util_log.h"
#include "transport/concurrent_hash_table.h"
#include "transport/data_server.h"

/**
 * @def By default a UDP client slot is reclaimed if within a minute no message has arrived from
 * the client.
 */
#define COMMUTIL_DEFAULT_UDP_CLIENT_EXPIRE_SECONDS 60

namespace commutil {

class COMMUTIL_API UdpServer : public DataServer {
public:
    /**
     * @brief Construct a UDP server object.
     *
     * @param serverAddress The server's address.
     * @param port The server's port.
     */
    UdpServer(const char* serverAddress, int port,
              uint64_t clientExpireSeconds = COMMUTIL_DEFAULT_UDP_CLIENT_EXPIRE_SECONDS)
        : DataServer(ByteOrder::NETWORK_ORDER),
          m_serverAddress(serverAddress),
          m_port(port),
          m_clientExpireSeconds(clientExpireSeconds) {}
    UdpServer(const UdpServer&) = delete;
    UdpServer(UdpServer&&) = delete;
    UdpServer& operator=(const UdpServer&) = delete;
    ~UdpServer() override {}

protected:
    // initialize the transport and connect if this is a stream transport
    // any derived class should redirect incoming data to
    ErrorCode initializeTransport(uv_loop_t* serverLoop, uv_handle_t*& transport) override;

    ErrorCode startTransport() override;

    ErrorCode stopTransport() override;

    // sends a data buffer through the transport
    // any derived class should redirect the callback to onWriteStatic
    ErrorCode writeTransport(ServerBufferData* serverBufferData) override;

    // specialize for UDP connections
    ConnectionData* createConnectionData() override;

private:
    std::string m_serverAddress;
    int m_port;
    uint64_t m_clientExpireSeconds;
    uv_udp_t m_socketHandle;
    typedef ConcurrentHashTable<uint64_t> ConnectionMap;
    ConnectionMap m_connectionMap;
    uv_timer_t m_expireTimer;

    // handle read request done event
    static void onRecvStatic(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                             const struct sockaddr* addr, unsigned flags);

    // handle async send request event
    // static void onAsyncSendStatic(uv_async_t* asyncReq);

    // handle send request done event
    static void onSendStatic(uv_udp_send_t* req, int status);

    // handle read request done event
    void onRecv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr,
                unsigned flags);

    // handle async send event
    // void onAsyncSend(ServerBufferData* serverBufferData);

    struct UdpConnectionData : public ConnectionData {
        uint32_t m_entryId;
        uint64_t m_lastAccessTimeMillis;
        UdpConnectionData() {}
        ~UdpConnectionData() override {}
    };

    // handle shutdown request done event
    static void onTimerStatic(uv_timer_t* handle);

    // handle shutdown request done event
    void onTimer(uv_timer_t* handle);

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __UDP_CLIENT_H__