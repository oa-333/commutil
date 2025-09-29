#include "transport/tcp_server.h"

#include <cassert>
#include <cstring>

#include "commutil_log_imp.h"
#include "transport/transport.h"

// server loop: open tcp server and pipe server, listen only on pipe server
// io tasks: create pipe clients and connect to pipe server
// upon new pipe connection: when all pipes connected, start listen on tcp server
// save server-side end of pipe connection
// upon new tcp connection: accept socket and write socket to server-side end of pipe connection
// (round-robin)
// read on io task side: "accept" new connection and issue read-start

#ifdef COMMUTIL_WINDOWS
#define COMMUTIL_PIPE_NAME "\\\\.\\pipe\\elog"
#else
#define COMMUTIL_PIPE_NAME "/tmp/elog.pipe"
#endif

namespace commutil {

IMPLEMENT_CLASS_LOGGER(TcpServer)

ErrorCode TcpServer::initializeTransport(uv_loop_t* serverLoop, uv_handle_t*& transport) {
    (void)serverLoop;
    ErrorCode rc = initTcpServer();
    if (rc != ErrorCode::E_OK) {
        return rc;
    }

    rc = initPipeServer();
    if (rc != ErrorCode::E_OK) {
        (void)termTcpServer();
        return rc;
    }

    m_ioTasks.resize(m_concurrency);
    for (uint32_t i = 0; i < m_concurrency; ++i) {
        m_ioTasks[i].m_server = this;
        m_ioTasks[i].m_taskId = i;
    }

    rc = initIOTasks();
    if (rc != ErrorCode::E_OK) {
        (void)termTcpServer();
        return rc;
    }

    // we are done here
    transport = (uv_handle_t*)&m_server;
    return ErrorCode::E_OK;
}

ErrorCode TcpServer::startTransport() {
    // start the IO tasks
    for (uint32_t i = 0; i < m_concurrency; ++i) {
        m_ioTasks[i].m_ioTask = std::thread(&TcpServer::ioTask, this, i);
    }
    return ErrorCode::E_OK;
}

ErrorCode TcpServer::stopTransport() {
    // join all IO tasks
    ErrorCode rc = termIOTasks();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to stop TCP IO tasks: %s", errorCodeToString(rc));
        return rc;
    }
    return ErrorCode::E_OK;
}

DataServer::ConnectionData* TcpServer::createConnectionData() {
    TcpConnectionData* connData = new (std::nothrow) TcpConnectionData();
    if (connData == nullptr) {
        LOG_ERROR("Failed to allocate TCP connection data object, out of memory");
    } else {
        connData->m_connectionDetails.setNetAddress(m_serverAddress.c_str(), m_port);
    }
    return connData;
}

ErrorCode TcpServer::initTcpServer() {
    m_serverState = ServerState::SS_LOOP_INIT;
    int res = uv_tcp_init(&m_serverLoop, &m_server);
    if (res < 0) {
        LOG_UV_ERROR(uv_tcp_init, res, "Failed to initialize TCP server object");
        (void)termTcpServer();
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    m_server.data = this;
    m_serverState = ServerState::SS_INIT;

    struct sockaddr_in bindAddr;
    res = uv_ip4_addr(m_serverAddress.c_str(), m_port, &bindAddr);
    if (res < 0) {
        LOG_UV_ERROR(uv_ip4_addr, res, "Failed to get server address/port: %s/%d",
                     m_serverAddress.c_str(), m_port);
        (void)termTcpServer();
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    res = uv_tcp_bind(&m_server, (const sockaddr*)&bindAddr, 0);
    if (res < 0) {
        LOG_UV_ERROR(uv_ip4_addr, res, "Failed to bind server socket to address/port: %s/%d",
                     m_serverAddress.c_str(), m_port);
        (void)termTcpServer();
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // defer listen until last pipe connect
    m_connectedPipes = 0;
    return ErrorCode::E_OK;
}

ErrorCode TcpServer::termTcpServer() {
    if (m_serverState == ServerState::SS_INIT) {
        uv_close((uv_handle_t*)&m_server, nullptr);
    }
    m_serverState = ServerState::SS_UNINIT;
    return ErrorCode::E_OK;
}

ErrorCode TcpServer::initPipeServer() {
    // pipe server shares loop with TCP server
    int res = uv_pipe_init(&m_serverLoop, &m_pipeServer, 0);
    if (res < 0) {
        LOG_UV_ERROR(uv_pipe_init, res, "Failed to initialize pipe server object");
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    m_pipeServer.data = this;

#ifndef COMMUTIL_WINDOWS
    if (unlink(COMMUTIL_PIPE_NAME) < 0) {
        int sysErr = errno;
        if (sysErr != ENOENT) {
            LOG_SYS_ERROR(unlink, "Failed to unlink previous pipe instance at: %s",
                          COMMUTIL_PIPE_NAME);
            return ErrorCode::E_SYSTEM_FAILURE;
        }
    }
#endif

    res = uv_pipe_bind(&m_pipeServer, COMMUTIL_PIPE_NAME);
    if (res < 0) {
        LOG_UV_ERROR(uv_pipe_bind, res, "Failed to bind pipe server object to path: %s",
                     COMMUTIL_PIPE_NAME);
        uv_close((uv_handle_t*)&m_pipeServer, nullptr);
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // it seems that the backlog parameter to uv_listen has no effect at all, and without the call
    // to uv_pipe_pending_instances(), the pipe server is stuck at 4 incoming connections
    // we use the concurrency level as the backlog value
    uv_pipe_pending_instances(&m_pipeServer, (int)m_concurrency);
    res = uv_listen((uv_stream_t*)&m_pipeServer, (int)m_concurrency, onNewPipeConnectionStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_listen, res, "Failed to listen on server pipe");
        uv_close((uv_handle_t*)&m_pipeServer, nullptr);
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

ErrorCode TcpServer::termPipeServer() {
    if (m_serverState == ServerState::SS_INIT) {
        uv_close((uv_handle_t*)&m_pipeServer, nullptr);
        m_serverState = ServerState::SS_TCP_INIT;
    }
    return ErrorCode::E_OK;
}

ErrorCode TcpServer::initIOTasks() {
    for (uint32_t i = 0; i < m_concurrency; ++i) {
        ErrorCode rc = initIOTask(i);
        if (rc != ErrorCode::E_OK) {
            for (uint32_t j = 0; j < i; ++j) {
                (void)termIOTask(j);
            }
            return rc;
        }
    }
    return ErrorCode::E_OK;
}

ErrorCode TcpServer::termIOTasks() {
    ErrorCode rc = ErrorCode::E_OK;
    for (uint32_t i = 0; i < m_concurrency; ++i) {
        ErrorCode rc2 = termIOTask(i);
        if (rc2 != ErrorCode::E_OK) {
            LOG_ERROR("Failed to terminate IO task %u: %s", i, errorCodeToString(rc2));
        }
        // report only first error, but continue as much as we can
        if (rc == ErrorCode::E_OK) {
            rc = rc2;
        }
    }
    return rc;
}

ErrorCode TcpServer::initIOTask(uint32_t id) {
    IOTaskData& taskData = m_ioTasks[id];
    int res = uv_loop_init(&taskData.m_ioLoop);
    if (res < 0) {
        LOG_UV_ERROR(uv_loop_init, res, "Failed to initialize IO task loop");
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    taskData.m_ioLoop.data = &taskData;
    taskData.m_state = IOTaskState::TS_LOOP_INIT;

    res = uv_pipe_init(&taskData.m_ioLoop, &taskData.m_clientPipe, 1);
    if (res < 0) {
        LOG_UV_ERROR(uv_pipe_init, res, "Failed to initialize pipe server object");
        termIOTask(id);
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    taskData.m_clientPipe.data = &taskData;
    taskData.m_state = IOTaskState::TS_SERVER_PIPE_INIT;

    taskData.m_connectReq.data = &taskData;
    uv_pipe_connect(&taskData.m_connectReq, &taskData.m_clientPipe, COMMUTIL_PIPE_NAME,
                    onPipeConnectStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_pipe_connect, res, "Failed to initialize pipe connect");
        termIOTask(id);
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

ErrorCode TcpServer::termIOTask(uint32_t id) {
    IOTaskData& ioTask = m_ioTasks[id];
    ErrorCode rc = stopTransportLoop(&ioTask.m_ioLoop, ioTask.m_ioTask, true);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to stop IO task %u: %s", id, errorCodeToString(rc));
        return rc;
    }
    ioTask.m_state = IOTaskState::TS_UNINIT;
    return ErrorCode::E_OK;
}

void TcpServer::ioTask(uint32_t id) {
    std::string taskName = std::string("tcp-server-io-task-") + std::to_string(id);
    notifyThreadStart(taskName.c_str());
    LOG_TRACE("IO task %u loop starting", id);
    uv_run(&m_ioTasks[id].m_ioLoop, UV_RUN_DEFAULT);
    LOG_TRACE("IO task %u loop ended", id);
}

void TcpServer::onPipeConnectStatic(uv_connect_t* connectReq, int status) {
    IOTaskData* taskData = (IOTaskData*)connectReq->data;
    taskData->m_server->handlePipeConnect(taskData, connectReq, status);
}

void TcpServer::onNewPipeConnectionStatic(uv_stream_t* server, int status) {
    ((TcpServer*)server->data)->handleNewPipeConnection(server, status);
}

void TcpServer::onNewConnectionStatic(uv_stream_t* server, int status) {
    ((TcpServer*)server->data)->handleNewConnection(server, status);
}

void TcpServer::onPipeReadStatic(uv_stream_t* connection, ssize_t nread, const uv_buf_t* buf) {
    IOTaskData* taskData = (IOTaskData*)connection->data;
    taskData->m_server->handlePipeRead(taskData, nread, buf);
}

void TcpServer::onReadStatic(uv_stream_t* connection, ssize_t nread, const uv_buf_t* buf) {
    TcpConnectionData* connectionData = (TcpConnectionData*)(ConnectionData*)connection->data;
    TcpServer* server = (TcpServer*)connectionData->m_server;
    server->onRead(connectionData, nread, buf, false);
    // close connection if read failed
    if (nread < 0) {
        uv_close((uv_handle_t*)&connectionData->m_socket2, nullptr);
        connectionData->m_isUsed = 0;
        server->m_dataListener->onDisconnect(connectionData->m_connectionDetails);
    }
}

void TcpServer::onWrite2Static(uv_write_t* req, int status) {
    if (status < 0) {
        LOG_ERROR("Failed to write incoming socket to pipe (libuv error code %d: %s)", status,
                  UV_ERROR_STR(status));
        // TODO: what about the connection? it should be closed
        delete req;
        return;
    }
    delete req;
}

void TcpServer::onAllocPipeBufferStatic(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    IOTaskData* taskData = (IOTaskData*)handle->data;
    TcpServer* server = (TcpServer*)taskData->m_server;
    server->onAllocBuffer(handle, suggested_size, buf);
}

void TcpServer::handlePipeConnect(IOTaskData* taskData, uv_connect_t* connectReq, int status) {
    (void)connectReq;
    (void)status;
    // start reading from client-side end of pipe
    LOG_TRACE("Client side pipe %u connected, issuing start-read", taskData->m_taskId);
    int res = uv_read_start((uv_stream_t*)&taskData->m_clientPipe, onAllocPipeBufferStatic,
                            onPipeReadStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_read_start, res, "Failed to start reading from pipe connection");
        uv_close((uv_handle_t*)&taskData->m_clientPipe, nullptr);
        // TODO: io task will bail out, what now? perhaps full server restart is required?
    }
}

void TcpServer::handleNewPipeConnection(uv_stream_t* server, int status) {
    (void)status;
    LOG_TRACE("Accepted server-side pipe connection %u", m_connectedPipes + 1);

    // get any valid pipe slot
    bool slotFound = false;
    uint32_t taskId = (uint32_t)-1;
    for (uint32_t i = 0; i < m_concurrency; ++i) {
        if (!m_ioTasks[i].m_isUsed) {
            m_ioTasks[i].m_isUsed = true;
            taskId = i;
            slotFound = true;
            break;
        }
    }

    if (!slotFound) {
        LOG_ERROR("Failed to find vacant pipe lost");
        // TODO: must either stop server or retry connect pipe, probably best is to restart server
        // but can we do this from notification context?
        return;
    }

    IOTaskData& taskData = m_ioTasks[taskId];
    int res = uv_pipe_init(&m_serverLoop, &taskData.m_serverPipe, 1);
    if (res < 0) {
        LOG_UV_ERROR(uv_pipe_init, res, "Failed to initialize server-side pipe connection object");
        taskData.m_isUsed = false;
        return;
    }
    taskData.m_serverPipe.data = &taskData;

    res = uv_accept(server, (uv_stream_t*)&taskData.m_serverPipe);
    if (res < 0) {
        LOG_UV_ERROR(uv_accept, res, "Failed to accept pipe connection");
        uv_close((uv_handle_t*)&taskData.m_serverPipe, nullptr);
        taskData.m_isUsed = false;
        // TODO: must either stop server or retry connect pipe, probably best is to restart server
        // but can we do this from notification context?
        return;
    }

    // no reading on this end of the pipe connection

    if (++m_connectedPipes == m_concurrency) {
        // now that all IO tasks have connected, we can issue listen on server socket
        LOG_TRACE("All IO pipes connecting, now listening on server socket");
        res = uv_listen((uv_stream_t*)&m_server, m_backlog, onNewConnectionStatic);
        if (res < 0) {
            LOG_UV_ERROR(uv_listen, res,
                         "Failed to listen to incoming connections at address/port: %s/%d",
                         m_serverAddress.c_str(), m_port);
            (void)termTcpServer();
            termIOTasks();
            // TODO: wouldn't a full restart be better here?
        }
    }
}

void TcpServer::handleNewConnection(uv_stream_t* server, int status) {
    if (status < 0) {
        LOG_UV_ERROR(handleNewConnection, status, "Failed to accept new connection");
        // stop loop if stop flag was raised
        if (getRunState() == RunState::RS_SHUTTING_DOWN) {
            LOG_TRACE("Detected shutdown sequence, stopping TCP server loop");
            uv_stop(&m_serverLoop);
        }
        return;
    }

    TcpConnectionData* connectionData = (TcpConnectionData*)grabConnectionData();
    if (connectionData == nullptr) {
        LOG_ERROR("Connection request denied: reached limit of %u connections", m_maxConnections);
        // TODO: socket should be accepted and immediately closed
        return;
    }

    int res = uv_tcp_init(&m_serverLoop, &connectionData->m_socket);
    if (res < 0) {
        LOG_UV_ERROR(uv_tcp_init, res, "Failed to initialize TCP connection object");
        connectionData->m_isUsed = 0;
        return;
    }
    connectionData->m_socket.data = connectionData;

    res = uv_accept(server, (uv_stream_t*)&connectionData->m_socket);
    if (res < 0) {
        LOG_UV_ERROR(uv_accept, res, "Failed to accept TCP connection");
        uv_close((uv_handle_t*)&connectionData->m_socket, nullptr);
        connectionData->m_isUsed = 0;
        return;
    }

    connectionData->m_connectionDetails.setHostDetails(&connectionData->m_socket);
    connectionData->m_connectionDetails.setConnectionId(connectionData->m_connectionId);

    // transfer connection to another IO task
    // note: we use connection index to make sure same connection object is always mapped to the
    // same IO task
    uint32_t ioTaskId = connectionData->m_connectionIndex % m_concurrency;
    IOTaskData& taskData = m_ioTasks[ioTaskId];

    // there is no race here with other incoming connections, but only with IO task processing
    // previous incoming connections
    {
        std::unique_lock<std::mutex> lock(taskData.m_lock);
        taskData.m_pendingConns.push_front(connectionData);
    }

    LOG_TRACE("Accepted TCP connection %p %s, passing to IO task %u", connectionData,
              connectionData->m_connectionDetails.toString(), ioTaskId);
    uv_write_t* writeReq = new (std::nothrow) uv_write_t();
    if (writeReq == nullptr) {
        LOG_ERROR("Failed to allocate write request, out of memory");
        connectionData->m_isUsed = 0;
    }
    writeReq->data = this;
    uv_buf_t buf = uv_buf_init((char*)"a", 1);  // use some dummy buffer, it is not used anyway
    res = uv_write2(writeReq, (uv_stream_t*)&taskData.m_serverPipe, &buf, 1,
                    (uv_stream_t*)&connectionData->m_socket, onWrite2Static);
    if (res < 0) {
        LOG_UV_ERROR(uv_write2, res, "Failed to pass TCP connection to IO task %u", ioTaskId);
        uv_close((uv_handle_t*)&connectionData->m_socket, nullptr);
        delete writeReq;
        connectionData->m_isUsed = 0;
    }
}

void TcpServer::handlePipeRead(IOTaskData* taskData, ssize_t nread, const uv_buf_t* buf) {
    // TODO: upon failure we need to somehow close the incoming socket, but we can't tell from which
    // connection data it arrived... so eventually, it is better to use a pending stack, which is
    // sure to be correlated with callback order (instead of passing it through buffer in uv_write2)
    // this way if something bad happened, we just close the socket and continue, and the client on
    // the other end will get the disconnect event
    (void)buf;
    LOG_TRACE("Incoming TCP connection arrived at IO task %u (status: %d)", taskData->m_taskId,
              (int)nread);

    // first thing: get the pending connection from private queue
    TcpConnectionData* connectionData = nullptr;
    {
        std::unique_lock<std::mutex> lock(taskData->m_lock);
        if (!taskData->m_pendingConns.empty()) {
            connectionData = taskData->m_pendingConns.back();
            taskData->m_pendingConns.pop_back();
        }
    }

    if (connectionData == nullptr) {
        if (nread != UV_EOF) {
            LOG_ERROR("Missing pending connection");
        }
    } else {
        LOG_TRACE("TCP connection %s located by IO task %u",
                  connectionData->m_connectionDetails.toString(), taskData->m_taskId);
    }

    // check read status
    if (nread < 0) {
        if (nread == UV_EOF) {
            LOG_TRACE("Detected initiated pipe close, at task %u", taskData->m_taskId);
        } else {
            LOG_UV_ERROR(handlePipeRead, ((int)nread),
                         "Failed to read from pipe client, closing incoming connection");
        }
        if (connectionData != nullptr) {
            uv_close((uv_handle_t*)&connectionData->m_socket, nullptr);
            connectionData->m_isUsed = 0;
        }
        return;
    }

    // now receive the socket properly on the pipe stream, according to instructions of uv
    if (!uv_pipe_pending_count(&taskData->m_clientPipe)) {
        // nothing happened, maybe spurious wakeup
        LOG_WARN("No incoming socket on pipe, closing incoming connection");
        uv_close((uv_handle_t*)&connectionData->m_socket, nullptr);
        connectionData->m_isUsed = 0;
        return;
    }
    LOG_TRACE("Counted one pending socket on pipe stream");

    uv_handle_type pending = uv_pipe_pending_type(&taskData->m_clientPipe);
    if (pending != UV_TCP) {
        LOG_WARN("Incoming socket on pipe has wrong type, closing incoming connection");
        uv_close((uv_handle_t*)&connectionData->m_socket, nullptr);
        connectionData->m_isUsed = 0;
        return;
    }
    LOG_TRACE("Pending incoming socket on pipe stream is TCP socket");

    // this is not a TCP accept, but rather socket transfer over pipe, enabling to be associated
    // with another loop
    int res = uv_tcp_init(&taskData->m_ioLoop, &connectionData->m_socket2);
    if (res < 0) {
        LOG_UV_ERROR(handlePipeRead, ((int)nread), "Failed to initialize TCP client socket");
        uv_close((uv_handle_t*)&connectionData->m_socket, nullptr);
        connectionData->m_isUsed = 0;
        return;
    }
    LOG_TRACE("TCP socket initialized");

    res =
        uv_accept((uv_stream_t*)&taskData->m_clientPipe, (uv_stream_t*)&connectionData->m_socket2);
    if (res < 0) {
        LOG_UV_ERROR(uv_accept, res, "Failed to accept TCP client socket into pipe loop");
        uv_close((uv_handle_t*)&connectionData->m_socket, nullptr);
        connectionData->m_isUsed = 0;
        return;
    }

    connectionData->m_socket2.data = connectionData;
    LOG_TRACE("TCP connection %s accepted by IO task %u",
              connectionData->m_connectionDetails.toString(), taskData->m_taskId);

    if (!m_dataListener->onConnect(connectionData->m_connectionDetails, 0)) {
        LOG_WARN("Server application rejected connection. Connection will be closed");
        uv_close((uv_handle_t*)&connectionData->m_socket2, nullptr);
        connectionData->m_isUsed = 0;
        return;
    }

    res = uv_read_start((uv_stream_t*)&connectionData->m_socket2, onAllocConnBufferStatic,
                        onReadStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_read_start, res, "Failed to start reading from TCP connection");
        uv_close((uv_handle_t*)&connectionData->m_socket2, nullptr);
        connectionData->m_isUsed = 0;
    }
    LOG_TRACE("Issued start-read from socket");
    connectionData->m_connectionHandle = (uv_handle_t*)&connectionData->m_socket2;
}

}  // namespace commutil
