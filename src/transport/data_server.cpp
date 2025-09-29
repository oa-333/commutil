#include "transport/data_server.h"

#include <cassert>
#include <cinttypes>
#include <cstring>

#include "commutil_log_imp.h"
#include "transport/transport.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(DataServer)

ErrorCode DataServer::initialize(DataListener* listener, uint32_t maxConnections,
                                 uint32_t bufferSize,
                                 DataServerAllocator* dataAllocator /* = nullptr */) {
    // TODO: figure out how bufferSize is to be used (pre-allocated buffer per connection)
    (void)bufferSize;
    if (!changeRunState(RunState::RS_IDLE, RunState::RS_STARTING_UP)) {
        return ErrorCode::E_INVALID_STATE;
    }

    if (!initConnDataArray(maxConnections)) {
        setRunState(RunState::RS_IDLE);
        return ErrorCode::E_NOMEM;
    }

    int res = uv_loop_init(&m_serverLoop);
    if (res < 0) {
        LOG_UV_ERROR(uv_loop_init, res, "Failed to initialize data server");
        termConnDataArray();
        setRunState(RunState::RS_IDLE);
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // start the transport (open socket, connect, etc.)
    ErrorCode rc = initializeTransport(&m_serverLoop, m_transport);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize transport layer: %s", errorCodeToString(rc));
        uv_loop_close(&m_serverLoop);
        termConnDataArray();
        setRunState(RunState::RS_IDLE);
        return rc;
    }
    m_transport->data = this;
    m_dataListener = listener;
    if (dataAllocator != nullptr) {
        m_dataAllocator = dataAllocator;
    }

    setRunState(RunState::RS_RUNNING);
    return ErrorCode::E_OK;
}

ErrorCode DataServer::terminate() {
    termConnDataArray();
    return ErrorCode::E_OK;
}

ErrorCode DataServer::start() {
    // update run state early, so that io thread will not terminate
    setRunState(RunState::RS_RUNNING);

    ErrorCode rc = startTransport();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to start transport layer running: %s", errorCodeToString(rc));
        return rc;
    }
    m_serverThread = std::thread(&DataServer::serverTask, this);
    return ErrorCode::E_OK;
}

ErrorCode DataServer::stop() {
    if (!changeRunState(RunState::RS_RUNNING, RunState::RS_SHUTTING_DOWN)) {
        LOG_ERROR("Cannot stop data server, not running");
        return ErrorCode::E_INVALID_STATE;
    }

    ErrorCode rc =
        stopTransportLoop(&m_serverLoop, m_serverThread, true, onStopTransportStatic, this);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to stop transport task: %s", errorCodeToString(rc));
        return rc;
    }

    rc = stopTransport();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to stop transport layer: %s", errorCodeToString(rc));
        return rc;
    }
    LOG_TRACE("Data server stopped");
    setRunState(RunState::RS_IDLE);
    return ErrorCode::E_OK;
}

ErrorCode DataServer::replyMsg(const ConnectionDetails& connectionDetails, const char* buffer,
                               uint32_t length, bool directBuffer /* = false */,
                               bool disposeBuffer /* = true */) {
    if (getRunState() != RunState::RS_RUNNING) {
        return ErrorCode::E_INVALID_STATE;
    }
    uv_buf_t buf = uv_buf_init(
        directBuffer ? (char*)buffer : m_dataAllocator->allocateRequestBuffer(length), length);
    buf.len = length;
    if (!directBuffer) {
        memcpy(buf.base, buffer, length);
    } else if (buf.base == nullptr) {
        LOG_ERROR("Failed to allocate transport buffer of %" PRIu64 " bytes", length);
        return ErrorCode::E_NOMEM;
    }

    ServerBufferData* serverBufferData = m_dataAllocator->allocateServerBufferData(
        this, connectionDetails.getConnectionIndex(), buf, disposeBuffer);
    if (serverBufferData == nullptr) {
        LOG_ERROR("Failed to allocate server buffer data, out of memory");
        if (!directBuffer) {
            m_dataAllocator->freeRequestBuffer(buf.base);
        }
        return ErrorCode::E_NOMEM;
    }

    ErrorCode rc = sendWriteResponse(serverBufferData);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to send write response: %s", errorCodeToString(rc));
        m_dataAllocator->freeServerBufferData(serverBufferData);
        if (!directBuffer) {
            m_dataAllocator->freeRequestBuffer(buf.base);
        }
        return rc;
    }

    return ErrorCode::E_OK;
}

DataServer::ConnectionData* DataServer::createConnectionData() {
    ConnectionData* connData = new (std::nothrow) ConnectionData();
    if (connData == nullptr) {
        LOG_ERROR("Failed to allocate connection data object, out of memory");
    }
    return connData;
}

void DataServer::onAllocBufferStatic(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    DataServer* dataServer = (DataServer*)handle->data;
    dataServer->onAllocBuffer(handle, suggested_size, buf);
}

void DataServer::onCloseStatic(uv_handle_t* handle) {
    DataServer* dataServer = (DataServer*)handle->data;
    dataServer->onClose(handle);
}

void DataServer::onAllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf) {
    (void)handle;
    char* buffer = m_dataAllocator->allocateRequestBuffer(suggestedSize);
    if (buffer == nullptr) {
        LOG_ERROR("Failed to allocate buff of size %zu bytes, out of memory", suggestedSize);
        // TODO: what now? should channel be closed?
    } else {
        // TODO: libuv has size mismatch here
        *buf = uv_buf_init(buffer, (uint32_t)suggestedSize);
    }
}

void DataServer::onRead(ConnectionData* connData, ssize_t nread, const uv_buf_t* buf,
                        bool isDatagram) {
    // TODO: this code is identical also in DataClient, can we refactor to a parent DataChannel?
    if (nread < 0) {
        // notify read error
        m_dataListener->onReadError(connData->m_connectionDetails, (int)nread);
        if (nread == UV_EOF) {
            LOG_TRACE("Server side socket closed");
        } else {
            LOG_UV_ERROR(onRead, ((int)nread), "Failed to receive data");
        }

        // release buffer
        if (buf->base != nullptr) {
            m_dataAllocator->freeRequestBuffer(buf->base);
        }
        return;
    }

    // check if nothing happened
    if (nread == 0) {
        return;
    }

    // notify listener
    // TODO: check that nread cast is safe
    DataAction dataAction = m_dataListener->onBytesReceived(connData->m_connectionDetails,
                                                            buf->base, (uint32_t)nread, isDatagram);

    // release buffer
    if (dataAction == DataAction::DATA_CAN_DELETE && buf->base != nullptr) {
        m_dataAllocator->freeRequestBuffer(buf->base);
    }
}

void DataServer::onWrite(ServerBufferData* serverBufferData, int status) {
    ConnectionData* connData = m_connDataArray[serverBufferData->m_connectionIndex];
    if (status < 0) {
        // notify write error
        m_dataListener->onWriteError(connData->m_connectionDetails, status);
        LOG_UV_ERROR(onWrite, status, "Failed to write data");
    } else {
        // notify listener
        m_dataListener->onBytesSent(connData->m_connectionDetails, serverBufferData->m_buf.len,
                                    status);
    }

    // release buffers
    if (serverBufferData->m_shouldDeallocateBuffer && serverBufferData->m_buf.base != nullptr) {
        m_dataAllocator->freeRequestBuffer(serverBufferData->m_buf.base);
    }

    m_dataAllocator->freeServerBufferData(serverBufferData);
}

void DataServer::onClose(uv_handle_t* handle) {
    (void)handle;
    // TODO: how do we get connection details from handle? can we save it in data member during
    // connect notification?
    // m_dataListener->onDisconnect(*m_connectionDetails);
}

bool DataServer::initConnDataArray(uint32_t maxConnections) {
    m_connDataArray = new (std::nothrow) ConnectionData*[maxConnections];
    if (m_connDataArray == nullptr) {
        LOG_ERROR("Failed to allocate connection array of %u elements", maxConnections);
        return false;
    }
    m_maxConnections = maxConnections;
    for (uint32_t i = 0; i < m_maxConnections; ++i) {
        m_connDataArray[i] = createConnectionData();
        if (m_connDataArray[i] == nullptr) {
            LOG_ERROR("Failed to allocate connection data slot");
            for (uint32_t j = 0; j < i; ++j) {
                delete m_connDataArray[j];
            }
            delete[] m_connDataArray;
            m_connDataArray = nullptr;
            return false;
        }
        m_connDataArray[i]->m_server = this;
        m_connDataArray[i]->m_connectionHandle = nullptr;
        m_connDataArray[i]->m_connectionIndex = i;
        m_connDataArray[i]->m_isUsed = 0;
        m_connDataArray[i]->m_connectionId = (uint64_t)-1;
        m_connDataArray[i]->m_connectionDetails.setConnectionIndex(i);
    }
    return true;
}

void DataServer::termConnDataArray() {
    if (m_connDataArray != nullptr) {
        for (uint32_t i = 0; i < m_maxConnections; ++i) {
            delete m_connDataArray[i];
        }
        delete[] m_connDataArray;
        m_connDataArray = nullptr;
    }
    m_maxConnections = 0;
}

DataServer::ConnectionData* DataServer::grabConnectionData() {
    for (uint32_t i = 0; i < m_maxConnections; ++i) {
        ConnectionData* connData = m_connDataArray[i];
        uint64_t isUsed = connData->m_isUsed.load(std::memory_order_acquire);
        if (!isUsed &&
            connData->m_isUsed.compare_exchange_strong(isUsed, 1, std::memory_order_release)) {
            connData->m_connectionId = m_nextConnectionId.fetch_add(1, std::memory_order_relaxed);
            connData->m_connectionDetails.setConnectionId(connData->m_connectionId);
            return connData;
        }
    }
    return nullptr;
}

bool DataServer::changeRunState(RunState from, RunState to) {
    RunState runState = m_runState.load(std::memory_order_acquire);
    if (runState != from) {
        return false;
    }
    if (m_runState.compare_exchange_strong(runState, to, std::memory_order_seq_cst)) {
        LOG_TRACE("Changed run state from %u to %u", runState, to);
        return true;
    }
    return false;
}

void DataServer::setRunState(RunState runState) {
    LOG_TRACE("Setting run state to %u", runState);
    m_runState.store(runState, std::memory_order_relaxed);
}

DataServer::RunState DataServer::getRunState() {
    RunState runState = m_runState.load(std::memory_order_relaxed);
    LOG_DIAG("Run state is %u", runState);
    return runState;
}

void DataServer::serverTask() {
    notifyThreadStart("uv-server-loop");
    LOG_TRACE("Data server loop starting");
    uv_run(&m_serverLoop, UV_RUN_DEFAULT);
    LOG_TRACE("Data server loop ended");
}

ErrorCode DataServer::sendWriteResponse(ServerBufferData* serverBufferData) {
    ConnectionData* connData = m_connDataArray[serverBufferData->m_connectionIndex];
    uv_async_t* asyncReq = m_dataAllocator->allocateAsyncRequest();
    if (asyncReq == nullptr) {
        LOG_ERROR("Failed to allocate asynchronous request, out of memory");
        return ErrorCode::E_NOMEM;
    }
    int res = uv_async_init(connData->m_connectionHandle->loop, asyncReq, onAsyncWriteStatic);
    if (res != 0) {
        LOG_UV_ERROR(uv_async_init, res,
                     "Cannot send write request, failed to initialize async request");
        m_dataAllocator->freeAsyncRequest(asyncReq);
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    asyncReq->data = serverBufferData;
    res = uv_async_send(asyncReq);
    if (res != 0) {
        LOG_UV_ERROR(uv_async_send, res, "Cannot send write request, failed to send async request");
        m_dataAllocator->freeAsyncRequest(asyncReq);
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

void DataServer::onAsyncWriteStatic(uv_async_t* asyncReq) {
    ServerBufferData* serverBufferData = (ServerBufferData*)asyncReq->data;
    DataServer* dataServer = serverBufferData->m_dataServer;
    dataServer->writeTransport(serverBufferData);
    // NOTE: the callback from writing will delete the client buffer pair, so we must fix the data
    // member of the async request
    asyncReq->data = dataServer;
    // NOTE: we cannot free the request now, since the loop still holds a reference to it, instead
    // we need to close it, and let the close callback free it.
    uv_close((uv_handle_t*)asyncReq, onCloseAsyncWriteReqStatic);
}

void DataServer::onCloseAsyncWriteReqStatic(uv_handle_t* handle) {
    assert(handle->type == UV_ASYNC);
    uv_async_t* asyncReq = (uv_async_t*)handle;
    DataServer* dataServer = (DataServer*)asyncReq->data;
    dataServer->m_dataAllocator->freeAsyncRequest(asyncReq);
}

void DataServer::onStopTransportStatic(void* data) {
    // let transport layer to do special stop stuff if any
    DataServer* server = (DataServer*)data;
    ErrorCode rc = server->onStopTransport();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to server, transport layer error: %s", errorCodeToString(rc));
    }
}

DataServer::ConnectionData::ConnectionData()
    : m_server(nullptr), m_connectionIndex(0), m_isUsed(0), m_connectionId(0) {}

DataServer::ConnectionData::ConnectionData(const ConnectionData& connData)
    : m_server(connData.m_server),
      m_connectionHandle(connData.m_connectionHandle),
      m_connectionIndex(connData.m_connectionIndex),
      m_isUsed(connData.m_isUsed.load()),
      m_connectionId(connData.m_connectionId),
      m_connectionDetails(connData.m_connectionDetails) {}

DataServer::ConnectionData::ConnectionData(ConnectionData&& connData)
    : m_server(connData.m_server),
      m_connectionHandle(connData.m_connectionHandle),
      m_connectionIndex(connData.m_connectionIndex),
      m_isUsed(connData.m_isUsed.load()),
      m_connectionId(connData.m_connectionId),
      m_connectionDetails(connData.m_connectionDetails) {
    connData.m_server = nullptr;
    connData.m_connectionHandle = nullptr;
}

DataServer::ConnectionData::~ConnectionData() {}

DataServer::ConnectionData& DataServer::ConnectionData::operator=(const ConnectionData& connData) {
    m_server = connData.m_server;
    m_connectionHandle = connData.m_connectionHandle;
    m_connectionIndex = connData.m_connectionIndex;
    m_isUsed.store(connData.m_isUsed.load());
    m_connectionId = connData.m_connectionId;
    m_connectionDetails = connData.m_connectionDetails;

    return *this;
}

DataServer::ConnectionData& DataServer::ConnectionData::operator=(ConnectionData&& connData) {
    m_server = connData.m_server;
    m_connectionHandle = connData.m_connectionHandle;
    m_connectionIndex = connData.m_connectionIndex;
    m_isUsed.store(connData.m_isUsed.load());
    m_connectionId = connData.m_connectionId;
    m_connectionDetails = connData.m_connectionDetails;

    connData.m_server = nullptr;
    connData.m_connectionHandle = nullptr;

    return *this;
}

}  // namespace commutil
