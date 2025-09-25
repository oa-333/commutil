#include "transport/pipe_server.h"

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

namespace commutil {

IMPLEMENT_CLASS_LOGGER(PipeServer)

ErrorCode PipeServer::initializeTransport(uv_loop_t* serverLoop, uv_handle_t*& transport) {
    (void)serverLoop;
    ErrorCode rc = initPipeServer();
    if (rc != ErrorCode::E_OK) {
        return rc;
    }

    // update run state, so that io thread will not terminate
    transport = (uv_handle_t*)&m_pipeServer;
    return ErrorCode::E_OK;
}

ErrorCode PipeServer::onStopTransport() {
    // shutdown each connected pipe
    for (uint32_t i = 0; i < m_maxConnections; ++i) {
        if (m_connDataArray[i]->m_isUsed) {
            ConnectionData* connData = m_connDataArray[i];
            int res = uv_shutdown(&connData->m_shutdownReq,
                                  (uv_stream_t*)&connData->m_connectionHandle, nullptr);
            if (res != 0) {
                LOG_UV_ERROR(uv_shutdown, res, "Failed to request shutdown of connection %u", i);
                return ErrorCode::E_TRANSPORT_ERROR;
            }
            connData->m_isUsed.store(0, std::memory_order_release);
        }
    }
    return ErrorCode::E_OK;
}

DataServer::ConnectionData* PipeServer::createConnectionData() {
    PipeConnectionData* connData = new (std::nothrow) PipeConnectionData();
    if (connData == nullptr) {
        LOG_ERROR("Failed to allocate pipe connection data object, out of memory");
    } else {
        connData->m_connectionDetails.setPipeName(m_pipeName.c_str());
    }
    return connData;
}

ErrorCode PipeServer::initPipeServer() {
    // pipe server shares loop with TCP server
    int res = uv_pipe_init(&m_serverLoop, &m_pipeServer, 0);
    if (res < 0) {
        LOG_UV_ERROR(uv_pipe_init, res, "Failed to initialize pipe server object");
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    m_pipeServer.data = this;

    std::string fullPipeName = std::string(COMMUTIL_PIPE_NAME_PREFIX) + m_pipeName;
#ifndef COMMUTIL_WINDOWS
    if (unlink(fullPipeName.c_str()) < 0) {
        int sysErr = errno;
        if (sysErr != ENOENT) {
            LOG_SYS_ERROR(unlink, "Failed to unlink previous pipe instance at: %s",
                          fullPipeName.c_str());
            return ErrorCode::E_SYSTEM_FAILURE;
        }
    }
#endif

    res = uv_pipe_bind(&m_pipeServer, fullPipeName.c_str());
    if (res < 0) {
        LOG_UV_ERROR(uv_pipe_bind, res, "Failed to bind pipe server object to name %s",
                     fullPipeName.c_str());
        uv_close((uv_handle_t*)&m_pipeServer, nullptr);
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // it seems that the backlog parameter to uv_listen has no effect at all, and without the call
    // to uv_pipe_pending_instances(), the pipe server is stuck at 4 incoming connections
    // we use the concurrency level as the backlog value
    uv_pipe_pending_instances(&m_pipeServer, m_backlog);
    res = uv_listen((uv_stream_t*)&m_pipeServer, (int)m_concurrency, onConnectStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_listen, res, "Failed to listen on server pipe");
        uv_close((uv_handle_t*)&m_pipeServer, nullptr);
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

ErrorCode PipeServer::termPipeServer() {
    if (m_serverState == ServerState::SS_INIT) {
        uv_close((uv_handle_t*)&m_pipeServer, nullptr);
        m_serverState = ServerState::SS_TCP_INIT;
    }
    return ErrorCode::E_OK;
}

void PipeServer::onConnectStatic(uv_stream_t* server, int status) {
    ((PipeServer*)server->data)->handleNewPipeConnection(server, status);
}

void PipeServer::onReadStatic(uv_stream_t* connection, ssize_t nread, const uv_buf_t* buf) {
    PipeConnectionData* connectionData = (PipeConnectionData*)(ConnectionData*)connection->data;
    PipeServer* server = (PipeServer*)connectionData->m_server;
    server->onRead(connectionData, nread, buf, false, false);
    // close connection if read failed
    if (nread < 0) {
        uv_close((uv_handle_t*)&connectionData->m_pipe, onCloseStatic);
        connectionData->m_isUsed = 0;
        server->m_dataListener->onDisconnect(connectionData->m_connectionDetails);
    }
}

void PipeServer::handleNewPipeConnection(uv_stream_t* server, int status) {
    (void)server;
    // check read status
    if (status < 0) {
        LOG_UV_ERROR(handleNewPipeConnection, status, "Failed to accept new pipe client");
        return;
    }

    LOG_TRACE("Incoming pipe connection");

    // grab a new connection slot
    PipeConnectionData* connectionData = (PipeConnectionData*)grabConnectionData();
    if (connectionData == nullptr) {
        LOG_ERROR("Cannot accept pipe client, no more available connection slots");
        // TODO: connection should be accepted and immediately closed
        return;
    }

    // this is not a TCP accept, but rather socket transfer over pipe, enabling to be associated
    // with another loop
    int res = uv_pipe_init(&m_serverLoop, &connectionData->m_pipe, 0);
    if (res < 0) {
        LOG_UV_ERROR(handleNewPipeConnection, res, "Failed to initialize pipe handle");
        connectionData->m_isUsed = 0;
        return;
    }
    LOG_TRACE("Pipe initialized");

    res = uv_accept((uv_stream_t*)&m_pipeServer, (uv_stream_t*)&connectionData->m_pipe);
    if (res < 0) {
        LOG_UV_ERROR(uv_accept, res, "Failed to accept pipe client");
        uv_close((uv_handle_t*)&connectionData->m_pipe, nullptr);
        connectionData->m_isUsed = 0;
        return;
    }

    connectionData->m_pipe.data = connectionData;
    LOG_TRACE("Pipe connection accepted");

    if (!m_dataListener->onConnect(connectionData->m_connectionDetails, 0)) {
        LOG_WARN("Server application rejected connection. Connection will be closed");
        uv_close((uv_handle_t*)&connectionData->m_pipe, nullptr);
        connectionData->m_isUsed = 0;
        return;
    }

    res =
        uv_read_start((uv_stream_t*)&connectionData->m_pipe, onAllocConnBufferStatic, onReadStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_read_start, res, "Failed to start reading from pipe connection");
        uv_close((uv_handle_t*)&connectionData->m_pipe, nullptr);
        connectionData->m_isUsed = 0;
        return;
    }
    LOG_TRACE("Issued start-read from pipe");
    connectionData->m_connectionHandle = (uv_handle_t*)&connectionData->m_pipe;

    LOG_TRACE("Incoming server-side pipe connection %u", m_connectedPipes + 1);
}

}  // namespace commutil
