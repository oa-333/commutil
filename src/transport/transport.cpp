#include "transport/transport.h"

#include "commutil_log_imp.h"
#include "transport/connection_details.h"
#include "transport/data_client.h"
#include "transport/data_loop_listener.h"
#include "transport/data_server.h"
#include "transport/data_stream_client.h"
#include "transport/data_stream_server.h"
#include "transport/pipe_client.h"
#include "transport/pipe_server.h"
#include "transport/tcp_client.h"
#include "transport/tcp_server.h"
#include "transport/udp_client.h"
#include "transport/udp_server.h"

namespace commutil {

static Logger sLogger;

struct TerminateData {
    TerminateData(TerminateCallback cb = nullptr, void* data = nullptr) : m_cb(cb), m_data(data) {}
    TerminateData(const TerminateData&) = delete;
    TerminateData(TerminateData&&) = delete;
    TerminateData& operator=(TerminateData&) = delete;
    ~TerminateData() {}

    TerminateCallback m_cb;
    void* m_data;
};

void close_cb(uv_handle_t* handle) {
    LOG_DEBUG("libuv handle %p (type %u) closed", (void*)handle, (int)handle->type);
}

void walk_and_close(uv_handle_t* handle, void* arg) {
    (void)arg;
    if (!uv_is_closing(handle)) {
        uv_close(handle, close_cb);  // Pass a callback for confirmation
    }
}

static void async_stop(uv_async_t* handle) {
    // execute user custom code
    if (handle->data != nullptr) {
        TerminateData* terminateData = (TerminateData*)handle->data;
        if (terminateData->m_cb != nullptr) {
            terminateData->m_cb(terminateData->m_data);
        }
    }

    uv_stop(handle->loop);
}

ErrorCode stopTransportLoop(uv_loop_t* loop, std::thread& ioThread, bool closeLoop /* = false */,
                            TerminateCallback cb /* = nullptr */, void* data /* = nullptr */) {
    // send asynchronous request to the loop so it will shutdown itself
    uv_async_t asyncReq = {};
    TerminateData* terminateData = nullptr;
    if (cb != nullptr) {
        terminateData = new (std::nothrow) TerminateData(cb, data);
        if (terminateData == nullptr) {
            LOG_ERROR(
                "Failed to terminate transport loop: Failed to allocate callback data, out of "
                "memory");
            return ErrorCode::E_NOMEM;
        }
    }
    asyncReq.data = terminateData;

    int res = uv_async_init(loop, &asyncReq, async_stop);
    if (res != 0) {
        LOG_UV_ERROR(uv_async_init, res,
                     "Cannot terminate libuv loop, failed to initialize async request");
        if (terminateData != nullptr) {
            delete terminateData;
        }
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    res = uv_async_send(&asyncReq);
    if (res != 0) {
        LOG_UV_ERROR(uv_async_send, res,
                     "Cannot terminate libuv loop, failed to send async request");
        if (terminateData != nullptr) {
            delete terminateData;
        }
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    // join IO thread
    ioThread.join();

    // close the async request handle
    uv_close((uv_handle_t*)&asyncReq, nullptr);

    // now close all handles (could be also socket2...)
    uv_walk(loop, walk_and_close, nullptr);

    // run loop once more for callbacks to fire
    // NOTE: using UV_RUN_DEFAULT block forever because the stop flag has been set to 0 by previous
    // call to uv_run, so we use UV_RUN_ONE instead
    uv_run(loop, UV_RUN_ONCE);

    // and finally close the loop
    if (closeLoop) {
        res = uv_loop_close(loop);
        if (res < 0) {
            LOG_UV_ERROR(uv_loop_close, res, "Failed to close transport loop");
            if (terminateData != nullptr) {
                delete terminateData;
            }
            return ErrorCode::E_TRANSPORT_ERROR;
        }
    }

    if (terminateData != nullptr) {
        delete terminateData;
    }
    return ErrorCode::E_OK;
}

void registerTransportLoggers() {
    registerLogger(sLogger, "transport");
    ConnectionDetails::registerClassLogger();
    DataClient::registerClassLogger();
    DataLoopListener::registerClassLogger();
    DataServer::registerClassLogger();
    DataStreamClient::registerClassLogger();
    DataStreamServer::registerClassLogger();
    PipeClient::registerClassLogger();
    PipeServer::registerClassLogger();
    TcpClient::registerClassLogger();
    TcpServer::registerClassLogger();
    UdpClient::registerClassLogger();
    UdpServer::registerClassLogger();
}

void unregisterTransportLoggers() {
    UdpServer::unregisterClassLogger();
    UdpClient::unregisterClassLogger();
    TcpServer::unregisterClassLogger();
    TcpClient::unregisterClassLogger();
    PipeServer::unregisterClassLogger();
    PipeClient::unregisterClassLogger();
    DataStreamServer::unregisterClassLogger();
    DataStreamClient::unregisterClassLogger();
    DataServer::unregisterClassLogger();
    DataLoopListener::unregisterClassLogger();
    DataClient::unregisterClassLogger();
    ConnectionDetails::unregisterClassLogger();
    unregisterLogger(sLogger);
}

}  // namespace commutil
