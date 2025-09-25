#include "transport/data_stream_server.h"

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(DataStreamServer)

ErrorCode DataStreamServer::writeTransport(ServerBufferData* serverBufferData) {
    uv_write_t* writeReq = m_dataAllocator->allocateWriteRequest();
    if (writeReq == nullptr) {
        LOG_ERROR("Failed to allocate write request, out of memory");
        return ErrorCode::E_NOMEM;
    }

    writeReq->data = serverBufferData;
    ConnectionData* connData = m_connDataArray[serverBufferData->m_connectionIndex];
    int res = uv_write(writeReq, (uv_stream_t*)connData->m_connectionHandle,
                       &serverBufferData->m_buf, 1, onWriteStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_write, res, "Failed to write stream message");
        m_dataAllocator->freeWriteRequest(writeReq);
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    return ErrorCode::E_OK;

#if 0
    // TODO: we must use uv_async_send here, since uv_write is NOT thread-safe
    // will this harm performance?
    // send asynchronous request to the loop so it will shutdown itself
    ConnectionData* connData = m_connDataArray[serverBufferData->m_connectionIndex];
    uv_async_t* asyncReq = m_dataAllocator->allocateAsyncRequest();
    if (asyncReq == nullptr) {
        LOG_ERROR("Failed to allocate asynchronous request, out of memory");
        return false;
    }
    int res = uv_async_init(connData->m_connectionHandle->loop, asyncReq, onAsyncWriteStatic);
    if (res != 0) {
        LOG_UV_ERROR(uv_async_init, res,
                             "Cannot send write request, failed to initialize async request");
        m_dataAllocator->freeAsyncRequest(asyncReq);
        return false;
    }

    asyncReq->data = serverBufferData;
    res = uv_async_send(asyncReq);
    if (res != 0) {
        LOG_UV_ERROR(uv_async_send, res,
                             "Cannot send write request, failed to send async request");
        m_dataAllocator->freeAsyncRequest(asyncReq);
        return false;
    }
    return true;
#endif
#if 0
    uv_write_t* writeReq = new (std::nothrow) uv_write_t();
    if (writeReq == nullptr) {
        LOG_ERROR("Failed to allocate write request, out of memory");
        return false;
    }
    writeReq->data = connBufPair;
    int res = uv_write(writeReq, (uv_stream_t*)transport, buffer, 1, onWriteStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_write, res, "Failed to send TCP reply message");
        return false;
    }

    return true;
#endif
}

#if 0
void DataStreamServer::onAsyncWriteStatic(uv_async_t* asyncReq) {
    ServerBufferData* serverBufferData = (ServerBufferData*)asyncReq->data;
    DataStreamServer* dataServer = (DataStreamServer*)serverBufferData->m_dataServer;
    dataServer->onAsyncWrite(serverBufferData);
    dataServer->m_dataAllocator->freeAsyncRequest(asyncReq);
}
#endif

void DataStreamServer::onAllocConnBufferStatic(uv_handle_t* handle, size_t suggested_size,
                                               uv_buf_t* buf) {
    ConnectionData* connData = (ConnectionData*)handle->data;
    DataStreamServer* server = (DataStreamServer*)connData->m_server;
    server->onAllocBuffer(handle, suggested_size, buf);
}

void DataStreamServer::onWriteStatic(uv_write_t* req, int status) {
    ServerBufferData* serverBufferData = (ServerBufferData*)req->data;
    // cast to self type so we can access members
    DataStreamServer* dataServer = (DataStreamServer*)serverBufferData->m_dataServer;
    dataServer->onWrite(serverBufferData, status);
    dataServer->m_dataAllocator->freeWriteRequest(req);
}

#if 0
void DataStreamServer::onAsyncWrite(ServerBufferData* serverBufferData) {
    // now we are in the loop context, so we can issue the write request
    uv_write_t* writeReq = m_dataAllocator->allocateWriteRequest();
    if (writeReq == nullptr) {
        LOG_ERROR("Failed to allocate write request, out of memory");
        m_dataAllocator->freeServerBufferData(serverBufferData);
        return;
    }

    writeReq->data = serverBufferData;
    ConnectionData* connData = m_connDataArray[serverBufferData->m_connectionIndex];
    int res = uv_write(writeReq, (uv_stream_t*)connData->m_connectionHandle, &serverBufferData->m_buf, 1,
                       onWriteStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_write, res, "Failed to write stream message");
        m_dataAllocator->freeWriteRequest(writeReq);
        m_dataAllocator->freeServerBufferData(serverBufferData);
    }
}
#endif

}  // namespace commutil
