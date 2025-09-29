#include "transport/data_stream_client.h"

#include <chrono>
#include <cinttypes>

#include "commutil_common.h"
#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(DataStreamClient)

ErrorCode DataStreamClient::initializeTransport(uv_loop_t* clientLoop, uv_handle_t*& transport) {
    ErrorCode rc = initializeStreamTransport(clientLoop, transport);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize stream transport: %s", errorCodeToString(rc));
        return rc;
    }
    int res = uv_timer_init(clientLoop, &m_timerHandle);
    if (res < 0) {
        LOG_UV_ERROR(uv_timer_init, res, "Failed to initialize timer");
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    m_timerHandle.data = this;
    return ErrorCode::E_OK;
}

ErrorCode DataStreamClient::terminateTransport() {
    // NOTE: timer is closed with all loop handles during call to stopTransportLoop().
    return terminateStreamTransport();
}

ErrorCode DataStreamClient::startTransport() {
    int res = uv_timer_start(&m_timerHandle, onTimerStatic, m_reconnectTimeoutMillis,
                             m_reconnectTimeoutMillis);
    if (res != 0) {
        LOG_UV_ERROR(uv_timer_start, res, "Failed to start reconnect timer");
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // we assume that a subsequent connect is expected by the sub-class
    m_lastConnectTimeMillis = getCurrentTimeMillis();
    return startStreamTransport();
}

ErrorCode DataStreamClient::stopTransport() {
    int res = uv_timer_stop(&m_timerHandle);
    if (res != 0) {
        LOG_UV_ERROR(uv_timer_stop, res, "Failed to stop reconnect timer");
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    if (m_streamHandle != nullptr) {
        m_shutdownReq.data = this;
        uv_shutdown(&m_shutdownReq, m_streamHandle, onShutdownStatic);
        m_streamHandle = nullptr;
    }
    return stopStreamTransport();
}

ErrorCode DataStreamClient::writeTransport(ClientBufferData* clientBufferData) {
    uv_write_t* writeReq = m_dataAllocator->allocateWriteRequest();
    if (writeReq == nullptr) {
        LOG_ERROR("Failed to allocate write request, out of memory");
        return ErrorCode::E_NOMEM;
    }

    writeReq->data = clientBufferData;
    int res = uv_write(writeReq, m_streamHandle, &clientBufferData->m_buf, 1, onWriteStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_write, res, "Failed to write stream message");
        m_dataAllocator->freeWriteRequest(writeReq);
        return ErrorCode::E_TRANSPORT_ERROR;
    }
    return ErrorCode::E_OK;
}

void DataStreamClient::onConnectStatic(uv_connect_t* req, int status) {
    DataStreamClient* dataClient = (DataStreamClient*)req->data;
    dataClient->onConnect(req, status);
}

void DataStreamClient::onReadStatic(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
    // cast carefully here
    DataStreamClient* dataClient = (DataStreamClient*)(DataClient*)client->data;
    dataClient->onRead(nread, buf, false);
}

void DataStreamClient::onAsyncWriteStatic(uv_async_t* asyncReq) {
    ClientBufferData* clientBufferData = (ClientBufferData*)asyncReq->data;
    DataStreamClient* dataClient = (DataStreamClient*)clientBufferData->m_dataClient;
    dataClient->onAsyncWrite(clientBufferData);
    dataClient->m_dataAllocator->freeAsyncRequest(asyncReq);
}

void DataStreamClient::onWriteStatic(uv_write_t* req, int status) {
    ClientBufferData* clientBufferData = (ClientBufferData*)req->data;
    // cast to self type so we can access members
    DataStreamClient* dataClient = (DataStreamClient*)clientBufferData->m_dataClient;
    dataClient->onWrite(clientBufferData, status);
    dataClient->m_dataAllocator->freeWriteRequest(req);
}

void DataStreamClient::onConnect(uv_connect_t* req, int status) {
    if (status != 0) {
        LOG_ERROR("Failed to connect stream client: %s (status: %d)", UV_ERROR_STR(status), status);
    } else {
        LOG_TRACE("Connected to upstream server");
        status = uv_read_start(req->handle, onAllocBufferStatic, onReadStatic);
        if (status < 0) {
            LOG_UV_ERROR(uv_read_start, status, "Failed to start reading from sstream client");
            // TODO: should we disconnect and retry connect?
        } else {
            m_isConnected.store(true);
        }
    }

    // notify result whether good or bad
    setReady(status);
    m_dataListener->onConnect(m_connectionDetails, status);

    // cleanup
    delete req;
}

void DataStreamClient::onAsyncWrite(ClientBufferData* clientBufferData) {
    // now we are in the loop context, so we can issue the write request
    uv_write_t* writeReq = m_dataAllocator->allocateWriteRequest();
    if (writeReq == nullptr) {
        LOG_ERROR("Failed to allocate write request, out of memory");
        m_dataAllocator->freeClientBufferData(clientBufferData);
        return;
    }

    writeReq->data = clientBufferData;
    int res = uv_write(writeReq, m_streamHandle, &clientBufferData->m_buf, 1, onWriteStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_write, res, "Failed to write stream message");
        m_dataAllocator->freeWriteRequest(writeReq);
        m_dataAllocator->freeClientBufferData(clientBufferData);
    }
}

void DataStreamClient::onTimerStatic(uv_timer_t* handle) {
    DataStreamClient* streamClient = (DataStreamClient*)handle->data;
    streamClient->onTimer(handle);
}

void DataStreamClient::onShutdownStatic(uv_shutdown_t* req, int status) {
    DataStreamClient* streamClient = (DataStreamClient*)req->data;
    streamClient->onShutdown(req, status);
}

void DataStreamClient::onTimer(uv_timer_t* handle) {
    (void)handle;
    // check if connected, if not then check if reconnect time has arrived
    if (!m_isConnected.load(std::memory_order_relaxed)) {
        uint64_t currTimeMillis = getCurrentTimeMillis();
        if (currTimeMillis - m_lastConnectTimeMillis >= m_reconnectTimeoutMillis) {
            // issue reconnect
            ErrorCode rc = startStreamTransport();
            if (rc != ErrorCode::E_OK) {
                LOG_ERROR("Failed to issue connect request: %s", errorCodeToString(rc));
            }
            m_lastConnectTimeMillis = currTimeMillis;
        }
    }
}

void DataStreamClient::onShutdown(uv_shutdown_t* req, int status) {
    (void)req;
    LOG_TRACE("Client shutdown completed with status: %d", status);
}

}  // namespace commutil
