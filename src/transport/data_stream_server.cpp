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
}

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

}  // namespace commutil
