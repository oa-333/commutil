#ifndef __PIPE_SERVER_H__
#define __PIPE_SERVER_H__

#include <mutex>

#include "comm_util_log.h"
#include "transport/data_stream_server.h"

namespace commutil {

class COMMUTIL_API PipeServer : public DataStreamServer {
public:
    /**
     * @brief Construct a TCP server object.
     * @param pipeName The pipe name.
     * @param backlog The maximum number of pending connections.
     * @param concurrency The number of IO threads (currently not in use).
     */
    PipeServer(const char* pipeName, int backlog, uint32_t concurrency)
        : DataStreamServer(ByteOrder::HOST_ORDER),
          m_pipeName(pipeName),
          m_backlog(backlog),
          m_concurrency(concurrency),
          m_connectedPipes(0),
          m_serverState(ServerState::SS_UNINIT) {}
    PipeServer(const PipeServer&) = delete;
    PipeServer(PipeServer&&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;
    ~PipeServer() override {}

protected:
    // initialize the transport and connect if this is a stream transport
    // any derived class should redirect incoming data to
    ErrorCode initializeTransport(uv_loop_t* serverLoop, uv_handle_t*& transport) override;

    // stop transport in loop context
    ErrorCode onStopTransport() override;

    // specialize for TCP connections
    ConnectionData* createConnectionData() override;

private:
    std::string m_pipeName;
    int m_backlog;
    uint32_t m_concurrency;

    uv_pipe_t m_pipeServer;
    uint32_t m_connectedPipes;

    enum class ServerState : uint32_t { SS_UNINIT, SS_LOOP_INIT, SS_TCP_INIT, SS_INIT };
    ServerState m_serverState;

    struct PipeConnectionData : public ConnectionData {
        uv_pipe_t m_pipe;

        PipeConnectionData() {}
        ~PipeConnectionData() override {}
    };

    ErrorCode initPipeServer();
    ErrorCode termPipeServer();

    static void onConnectStatic(uv_stream_t* server, int status);
    static void onReadStatic(uv_stream_t* connection, ssize_t nread, const uv_buf_t* buf);
    void handleNewPipeConnection(uv_stream_t* server, int status);

    static void onStopTransportStatic(void* data);

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __PIPE_SERVER_H__