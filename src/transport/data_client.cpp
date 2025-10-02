#include "transport/data_client.h"

#include <cassert>
#include <cinttypes>
#include <cstring>

#include "commutil_log_imp.h"
#include "transport/transport.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(DataClient)

ClientBufferData* DataClientAllocator::allocateClientBufferData(DataClient* dataClient,
                                                                uv_buf_t& buf,
                                                                bool shouldDeallocateBuffer) {
    return new (std::nothrow) ClientBufferData(dataClient, buf, shouldDeallocateBuffer);
}

void DataClientAllocator::freeClientBufferData(ClientBufferData* cbPair) { delete cbPair; }

ErrorCode DataClient::initialize(DataListener* listener, DataClientAllocator* dataAllocator) {
    int res = uv_loop_init(&m_clientLoop);
    if (res < 0) {
        LOG_UV_ERROR(uv_loop_init, res, "Failed to initialize data client");
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // start the transport (open socket, connect, etc.)
    ErrorCode rc = initializeTransport(&m_clientLoop, m_transport);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize data client, transport layer error: %s",
                  errorCodeToString(rc));
        uv_loop_close(&m_clientLoop);
        return rc;
    }
    m_transport->data = this;

    m_dataListener = listener;
    m_dataAllocator = dataAllocator;
    if (m_dataAllocator == nullptr) {
        m_dataAllocator = &m_defaultDataAllocator;
    }
    return ErrorCode::E_OK;
}

ErrorCode DataClient::terminate() {
    // terminate transport layer
    ErrorCode rc = terminateTransport();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to terminate transport: %s", errorCodeToString(rc));
        return rc;
    }

    // close the loop
    int res = uv_loop_close(&m_clientLoop);
    if (res < 0) {
        LOG_UV_ERROR(uv_loop_close, res, "Failed to close client loop");
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

ErrorCode DataClient::start() {
    m_ioThread = std::thread(&DataClient::ioTask, this);
    return ErrorCode::E_OK;
}

ErrorCode DataClient::stop() {
    ErrorCode rc = stopTransportLoop(&m_clientLoop, m_ioThread, false, onStopTransportStatic, this);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to terminate data client");
        return rc;
    }
    return ErrorCode::E_OK;
}

bool DataClient::isReady() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_isReady;
}

int DataClient::waitReady() {
    std::unique_lock<std::mutex> lock(m_lock);
    m_cv.wait(lock, [this]() { return m_isReady; });
    return m_connectStatus;
}

ErrorCode DataClient::write(const char* buffer, uint32_t length, uint32_t flags /* = 0 */,
                            void* userData /* = nullptr */) {
    // TODO: the caller should have the chance to install an allocator for buffers
    // next we also need a way to avoid all these allocations, for instance, the message layer has
    // request data for each outgoing request, so we can put there a static buffer up to some size,
    // and if message is too large than use dynamic allocation, also the client buffer pair should
    // be defined in the request, such that no allocation is needed, since it is tied up with each
    // request. we can add a callback for allocation, which by default allocated on heap, and the
    // message layer can install its own callback, such that for the current thread we can put the
    // currently active request, and use it to provide a buffer and a client-buffer pair. the same
    // is true for allocate async-send and write-req, both of which can exist on the request data.
    // so we have these callbacks from the allocator:
    // - allocate/free buffer (must keep a bit somewhere telling whether it is dynamic or not)
    // - allocate/free client-buffer pair
    // - allocate/free async send request
    // - allocate/free write request

    // allocate or copy buffer
    bool byRef = flags & COMMUTIL_MSG_WRITE_BY_REF;
    // NOTE: if a buffer is passed not by reference, then it necessarily implies that the buffer
    // must be disposed when write is done
    if (!byRef) {
        flags |= COMMUTIL_MSG_WRITE_DISPOSE_BUFFER;
    }
    bool shouldDisposeBuffer = flags & COMMUTIL_MSG_WRITE_DISPOSE_BUFFER;

    // allocate and copy buffer if needed, otherwise just take pointer
    uv_buf_t buf = byRef ? uv_buf_init(const_cast<char*>(buffer), length)
                         : uv_buf_init(m_dataAllocator->allocateRequestBuffer(length), length);
    if (!byRef) {
        if (buf.base == nullptr) {
            LOG_ERROR("Failed to allocate transport buffer of %" PRIu64 " bytes", length);
            return ErrorCode::E_NOMEM;
        }
        memcpy(buf.base, buffer, length);
    }

    // prepare data for async call completion
    ClientBufferData* clientBufferData =
        m_dataAllocator->allocateClientBufferData(this, buf, shouldDisposeBuffer);
    if (clientBufferData == nullptr) {
        LOG_ERROR("Failed to allocate client-buffer pair, out of memory");
        if (!byRef) {
            m_dataAllocator->freeRequestBuffer(buf.base);
        }
        return ErrorCode::E_NOMEM;
    }
    clientBufferData->m_userData = userData;

    // send async write request (I/O can take place only within the loop context)
    ErrorCode rc = sendWriteRequest(clientBufferData);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to send write request on transport: %s", errorCodeToString(rc));
        m_dataAllocator->freeClientBufferData(clientBufferData);
        if (!byRef) {
            m_dataAllocator->freeRequestBuffer(buf.base);
        }
        return rc;
    }

    return ErrorCode::E_OK;
}

ErrorCode DataClient::stopTransport() {
    if (m_transport != nullptr) {
        uv_close(m_transport, onCloseStatic);
        m_transport = nullptr;
    }
    return ErrorCode::E_OK;
}

void DataClient::onAllocBufferStatic(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    DataClient* dataClient = (DataClient*)handle->data;
    dataClient->onAllocBuffer(handle, suggested_size, buf);
}

void DataClient::onCloseStatic(uv_handle_t* handle) {
    DataClient* dataClient = (DataClient*)handle->data;
    dataClient->onClose(handle);
}

void DataClient::onStopTransportStatic(void* data) {
    // let transport layer to do special stop stuff if any
    DataClient* dataClient = (DataClient*)data;
    ErrorCode rc = dataClient->stopTransport();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to stop data client running, transport layer error: %s",
                  errorCodeToString(rc));
    }
}

void DataClient::onAllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf) {
    (void)handle;
    char* buffer = m_dataAllocator->allocateRequestBuffer(suggestedSize);
    if (buffer == nullptr) {
        LOG_ERROR("Failed to allocate buff of size %zu bytes, out of memory", suggestedSize);
        // TODO: what now? should channel be closed?
    } else {
        // TODO: libuv itself has a casting problem (size_t and uint32_t)
        *buf = uv_buf_init(buffer, (uint32_t)suggestedSize);
    }
}

void DataClient::onRead(ssize_t nread, const uv_buf_t* buf, bool isDatagram) {
    if (nread < 0) {
        // notify read error
        m_dataListener->onReadError(m_connectionDetails, (int)nread);
        LOG_UV_ERROR(onRead, ((int)nread), "Failed to receive data");
        uv_close((uv_handle_t*)m_transport, onCloseStatic);

        // release buffer
        if (buf != nullptr && buf->base != nullptr) {
            m_dataAllocator->freeRequestBuffer(buf->base);
        }
        return;
    }

    // check if nothing happened
    if (nread == 0) {
        // release buffer
        if (buf != nullptr && buf->base != nullptr) {
            m_dataAllocator->freeRequestBuffer(buf->base);
        }
        return;
    }

    // notify listener
    // TODO: buffer sizes should be checked in all places for size breach before cast
    DataAction dataAction = m_dataListener->onBytesReceived(m_connectionDetails, buf->base,
                                                            (uint32_t)nread, isDatagram);

    // release buffer
    if (dataAction == DataAction::DATA_CAN_DELETE && buf->base != nullptr) {
        m_dataAllocator->freeRequestBuffer(buf->base);
    }

    // notify loop listener
    if (m_dataLoopListener != nullptr) {
        m_dataLoopListener->onLoopRecv(&m_clientLoop);
    }
}

void DataClient::onWrite(ClientBufferData* clientBufferData, int status) {
    uv_buf_t* buf = &clientBufferData->m_buf;
    if (status < 0) {
        // notify write error
        m_dataListener->onWriteError(m_connectionDetails, status);
        LOG_ERROR("Failed to write data: %s (libuv status: %d)", UV_ERROR_STR(status), status);

        // close client
        uv_close(m_transport, onCloseStatic);
    } else {
        // notify listener
        m_dataListener->onBytesSent(m_connectionDetails, buf->len, status);
    }

    // release buffers
    if (clientBufferData->m_shouldDeallocateBuffer && buf->base != nullptr) {
        m_dataAllocator->freeRequestBuffer(buf->base);
    }

    m_dataAllocator->freeClientBufferData(clientBufferData);
}

void DataClient::onClose(uv_handle_t* handle) {
    (void)handle;
    m_dataListener->onDisconnect(m_connectionDetails);
}

void DataClient::setReady(int status) {
    std::unique_lock<std::mutex> lock(m_lock);
    m_isReady = true;
    m_connectStatus = status;
    m_cv.notify_one();
}

void DataClient::ioTask() {
    notifyThreadStart("uv-client-loop");
    ErrorCode rc = startTransport();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to start data client running, transport layer error: %s",
                  errorCodeToString(rc));
        return;
    }
    if (m_dataLoopListener != nullptr) {
        m_dataLoopListener->onLoopStart(&m_clientLoop);
    }
    uv_run(&m_clientLoop, UV_RUN_DEFAULT);
    if (m_dataLoopListener != nullptr) {
        m_dataLoopListener->onLoopEnd(&m_clientLoop);
    }
}

ErrorCode DataClient::sendWriteRequest(ClientBufferData* clientBufferData) {
    // prepare async request
    uv_async_t* asyncReq = m_dataAllocator->allocateAsyncRequest();
    if (asyncReq == nullptr) {
        LOG_ERROR("Failed to allocate asynchronous request, out of memory");
        return ErrorCode::E_NOMEM;
    }
    int res = uv_async_init(&m_clientLoop, asyncReq, onAsyncWriteStatic);
    if (res != 0) {
        LOG_UV_ERROR(uv_async_init, res,
                     "Cannot send write request, failed to initialize async request");
        m_dataAllocator->freeAsyncRequest(asyncReq);
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    asyncReq->data = clientBufferData;
    res = uv_async_send(asyncReq);
    if (res != 0) {
        LOG_UV_ERROR(uv_async_send, res, "Cannot send write request, failed to send async request");
        m_dataAllocator->freeAsyncRequest(asyncReq);
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

void DataClient::onAsyncWriteStatic(uv_async_t* asyncReq) {
    ClientBufferData* clientBufferData = (ClientBufferData*)asyncReq->data;
    DataClient* dataClient = clientBufferData->m_dataClient;
    dataClient->writeTransport(clientBufferData);
    // NOTE: the callback from writing will delete the client buffer pair, so we must fix the data
    // member of the async request
    asyncReq->data = dataClient;
    // NOTE: we cannot free the request now, since the loop still holds a reference to it, instead
    // we need to close it, and let the close callback free it.
    uv_close((uv_handle_t*)asyncReq, onCloseAsyncWriteReqStatic);

    if (dataClient->m_dataLoopListener != nullptr) {
        dataClient->m_dataLoopListener->onLoopSend(&dataClient->m_clientLoop,
                                                   clientBufferData->m_userData);
    }
}

void DataClient::onCloseAsyncWriteReqStatic(uv_handle_t* handle) {
    assert(handle->type == UV_ASYNC);
    uv_async_t* asyncReq = (uv_async_t*)handle;
    DataClient* dataClient = (DataClient*)asyncReq->data;
    dataClient->m_dataAllocator->freeAsyncRequest(asyncReq);
}

}  // namespace commutil
