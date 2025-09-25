#ifndef __DATA_STREAM_CLIENT_H__
#define __DATA_STREAM_CLIENT_H__

#include <atomic>

#include "comm_util_log.h"
#include "transport/data_client.h"

namespace commutil {

/**
 * @brief Abstract base class for all network/IPC clients. The data client does not take care of
 * message boundaries, and is responsible only for transporting data buffers to and fro.
 */
class COMMUTIL_API DataStreamClient : public DataClient {
public:
    ~DataStreamClient() override {}

protected:
    DataStreamClient(uv_stream_t* streamHandle, uint64_t reconnectTimeoutMillis,
                     ByteOrder byteOrder)
        : DataClient(byteOrder),
          m_streamHandle(streamHandle),
          m_reconnectTimeoutMillis(reconnectTimeoutMillis),
          m_isConnected(false),
          m_lastConnectTimeMillis(0) {}
    DataStreamClient(const DataStreamClient&) = delete;
    DataStreamClient(DataStreamClient&&) = delete;

    DataStreamClient& operator=(const DataStreamClient&) = delete;

    // initialize the transport and connect if this is a stream transport
    // any derived class should redirect incoming data to
    ErrorCode initializeTransport(uv_loop_t* clientLoop, uv_handle_t*& transport) final;

    // cleans up resources associated with the transport layer
    ErrorCode terminateTransport() final;

    // starts the transport machinery running (e.g. issue connect request)
    ErrorCode startTransport() final;

    // stops the transport
    ErrorCode stopTransport() final;

    // sends a data buffer through the transport
    // any derived class should redirect the callback to onWriteStatic
    ErrorCode writeTransport(ClientBufferData* clientBufData) override;

    // initialize the transport and connect if this is a stream transport
    // any derived class should redirect incoming data to onReadStatic()
    virtual ErrorCode initializeStreamTransport(uv_loop_t* clientLoop, uv_handle_t*& transport) = 0;

    // cleans up resources associated with the transport layer
    virtual ErrorCode terminateStreamTransport() { return ErrorCode::E_OK; }

    // starts the transport machinery running (e.g. issue connect request)
    virtual ErrorCode startStreamTransport() = 0;

    // stops the transport
    virtual ErrorCode stopStreamTransport() { return ErrorCode::E_OK; }

    // handle connect request done event
    static void onConnectStatic(uv_connect_t* req, int status);

    // handle read request done event
    static void onReadStatic(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);

    // handle async write request
    static void onAsyncWriteStatic(uv_async_t* asyncReq);

    // handle write request done event
    static void onWriteStatic(uv_write_t* req, int status);

    // handle connect request done event
    void onConnect(uv_connect_t* req, int status);

    // handle async write request
    void onAsyncWrite(ClientBufferData* clientBufferData);

private:
    uv_stream_t* m_streamHandle;
    uv_timer_t m_timerHandle;
    uv_shutdown_t m_shutdownReq;
    uint64_t m_reconnectTimeoutMillis;
    std::atomic<bool> m_isConnected;
    std::atomic<uint64_t> m_lastConnectTimeMillis;

    // handle shutdown request done event
    static void onTimerStatic(uv_timer_t* handle);

    // handle shutdown request done event
    static void onShutdownStatic(uv_shutdown_t* req, int status);

    // handle shutdown request done event
    void onTimer(uv_timer_t* handle);

    // handle shutdown request done event
    void onShutdown(uv_shutdown_t* req, int status);

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __DATA_CLIENT_H__