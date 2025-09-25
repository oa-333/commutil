#ifndef __TCP_SERVER_H__
#define __TCP_SERVER_H__

#include <mutex>
#include <vector>

#include "comm_util_log.h"
#include "transport/data_stream_server.h"

namespace commutil {

class COMMUTIL_API TcpServer : public DataStreamServer {
public:
    /**
     * @brief Construct a TCP server object.
     * @param serverAddress The server's address.
     * @param port The server's port.
     * @param backlog The maximum number of pending connections.
     * @param concurrency The number of IO threads (currently not in use).
     */
    TcpServer(const char* serverAddress, int port, int backlog, uint32_t concurrency)
        : DataStreamServer(ByteOrder::NETWORK_ORDER),
          m_serverAddress(serverAddress),
          m_port(port),
          m_backlog(backlog),
          m_concurrency(concurrency),
          m_connectedPipes(0),
          m_serverState(ServerState::SS_UNINIT) {}
    TcpServer(const TcpServer&) = delete;
    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    ~TcpServer() override {}

protected:
    // initialize the transport and connect if this is a stream transport
    // any derived class should redirect incoming data to
    ErrorCode initializeTransport(uv_loop_t* serverLoop, uv_handle_t*& transport) override;

    // start the transport layer
    ErrorCode startTransport() override;

    // called after transport layer is stopped
    ErrorCode stopTransport() override;

    // specialize for TCP connections
    ConnectionData* createConnectionData() override;

private:
    std::string m_serverAddress;
    int m_port;
    int m_backlog;
    uint32_t m_concurrency;
    uv_tcp_t m_server;

    uv_pipe_t m_pipeServer;
    uint32_t m_connectedPipes;

    enum class ServerState : uint32_t { SS_UNINIT, SS_LOOP_INIT, SS_TCP_INIT, SS_INIT };
    ServerState m_serverState;

    struct TcpConnectionData;
    enum class IOTaskState : uint32_t { TS_UNINIT, TS_LOOP_INIT, TS_SERVER_PIPE_INIT, TS_INIT };
    struct IOTaskData {
        IOTaskData(const IOTaskData& taskData)
            : m_server(taskData.m_server), m_state(taskData.m_state), m_isUsed(taskData.m_isUsed) {}
        IOTaskData() : m_state(IOTaskState::TS_UNINIT), m_isUsed(false) {}
        IOTaskData(IOTaskData&& taskData) = delete;
        IOTaskData& operator=(const IOTaskData&) = delete;
        ~IOTaskData() {}

        TcpServer* m_server;
        IOTaskState m_state;
        uv_loop_t m_ioLoop;
        uv_pipe_t m_clientPipe;
        uv_pipe_t m_serverPipe;
        uv_connect_t m_connectReq;
        std::thread m_ioTask;
        uint32_t m_taskId;
        bool m_isUsed;

        // due to unclear libuv behavior, we pass incoming connection via private queue
        std::mutex m_lock;
        std::list<TcpConnectionData*> m_pendingConns;
    };
    std::vector<IOTaskData> m_ioTasks;

    struct TcpConnectionData : public ConnectionData {
        uv_tcp_t m_socket;  // this socket is not used after transfer through pipe
        uv_tcp_t m_socket2;
        uv_shutdown_t m_shutdownReq;

        TcpConnectionData() {}
        ~TcpConnectionData() override {}
    };

    ErrorCode initTcpServer();
    ErrorCode termTcpServer();
    ErrorCode initPipeServer();
    ErrorCode termPipeServer();
    ErrorCode initIOTasks();
    ErrorCode termIOTasks();
    ErrorCode initIOTask(uint32_t id);
    ErrorCode termIOTask(uint32_t id);

    void ioTask(uint32_t id);

    static void onPipeConnectStatic(uv_connect_t* connectReq, int status);
    static void onNewPipeConnectionStatic(uv_stream_t* server, int status);
    static void onNewConnectionStatic(uv_stream_t* server, int status);
    static void onPipeReadStatic(uv_stream_t* connection, ssize_t nread, const uv_buf_t* buf);
    static void onReadStatic(uv_stream_t* connection, ssize_t nread, const uv_buf_t* buf);
    static void onWriteStatic(uv_write_t* req, int status);
    static void onWrite2Static(uv_write_t* req, int status);
    static void onAllocPipeBufferStatic(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    void handlePipeConnect(IOTaskData* taskData, uv_connect_t* connectReq, int status);
    void handleNewPipeConnection(uv_stream_t* server, int status);
    void handleNewConnection(uv_stream_t* server, int status);
    void handlePipeRead(IOTaskData* taskData, ssize_t nread, const uv_buf_t* buf);
    // void handleRead(ConnectionData* connectionData, ssize_t nread, const uv_buf_t* buf);

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __TCP_SERVER_H__