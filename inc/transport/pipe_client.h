#ifndef __PIPE_CLIENT_H__
#define __PIPE_CLIENT_H__

#include "comm_util_log.h"
#include "transport/data_stream_client.h"

namespace commutil {

class COMMUTIL_API PipeClient : public DataStreamClient {
public:
    /**
     * @brief Construct a named pipe client object.
     * @param pipeName The name of the pipe.
     * @note On UNIX systems this is a unix domain socket.
     */
    PipeClient(const char* pipeName, uint64_t reconnectTimeoutMillis)
        : DataStreamClient((uv_stream_t*)&m_pipeHandle, reconnectTimeoutMillis,
                           ByteOrder::HOST_ORDER),
          m_pipeName(pipeName) {}
    PipeClient(const PipeClient&) = delete;
    PipeClient(PipeClient&&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;
    ~PipeClient() override {}

protected:
    // initialize the transport and connect if this is a stream transport
    // any derived class should redirect incoming data to
    ErrorCode initializeStreamTransport(uv_loop_t* clientLoop, uv_handle_t*& transport) override;

    // starts the transport machinery running (e.g. issue connect request)
    ErrorCode startStreamTransport() override;

private:
    std::string m_pipeName;
    uv_pipe_t m_pipeHandle;

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __PIPE_CLIENT_H__