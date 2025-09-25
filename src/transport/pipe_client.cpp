#include "transport/pipe_client.h"

#include "commutil_log_imp.h"
#include "transport/transport.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(PipeClient)

ErrorCode PipeClient::initializeStreamTransport(uv_loop_t* clientLoop, uv_handle_t*& transport) {
    int res = uv_pipe_init(clientLoop, &m_pipeHandle, 0);
    if (res < 0) {
        LOG_UV_ERROR(uv_pipe_init, res, "Failed to initialize pipe");
        return ErrorCode::E_TRANSPORT_ERROR;
    }

    transport = (uv_handle_t*)&m_pipeHandle;
    m_connectionDetails.setPipeName(m_pipeName.c_str());
    return ErrorCode::E_OK;
}

ErrorCode PipeClient::startStreamTransport() {
    std::string fullPipeName = std::string(COMMUTIL_PIPE_NAME_PREFIX) + m_pipeName;
    uv_connect_t* req = new (std::nothrow) uv_connect_t();
    if (req == nullptr) {
        LOG_ERROR("Failed to allocate pipe connect request, out of memory");
        return ErrorCode::E_NOMEM;
    }
    req->data = this;
    uv_pipe_connect(req, &m_pipeHandle, fullPipeName.c_str(), onConnectStatic);
    return ErrorCode::E_OK;
}

}  // namespace commutil
