#include "msg/msg_request_pool.h"

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgRequestPool)

void MsgRequestPool::initialize(uint32_t poolSize) { m_requests.resize(poolSize); }

void MsgRequestPool::terminate() { m_requests.clear(); }

void MsgRequestPool::setListener(MsgRequestListener* listener) { m_listener = listener; }

MsgRequest* MsgRequestPool::getVacantRequest(uint64_t requestId, uint32_t& requestIndex) {
    // search a full cycle, starting from one place after the last request
    uint32_t requestCount = (uint32_t)m_requests.size();
    uint32_t initialRequestId = (uint32_t)requestId % requestCount;
    for (size_t i = 0; i < m_requests.size(); ++i) {
        uint32_t index = (initialRequestId + i) % requestCount;
        if (m_requests[index].tryGrab(requestId)) {
            requestIndex = index;
            return &m_requests[index];
        }
    }
    return nullptr;
}

MsgRequest* MsgRequestPool::getRequestData(Msg* request) {
    uint32_t requestIndex = request->getHeader().getRequestIndex();
    if (requestIndex >= m_requests.size()) {
        LOG_ERROR("Cannot retrieve request data for request index %u: out of range", requestIndex);
        return nullptr;
    }
    MsgRequest* requestData = &m_requests[requestIndex];
    uint64_t requestId = request->getHeader().getRequestId();
    if (requestId != requestData->getRequestId()) {
        LOG_ERROR("Cannot retrieve request data for request with request id %" PRIu64
                  ": expected request id %" PRIu64,
                  requestId, requestData->getRequestId());
        return nullptr;
    }

    return requestData;
}

void MsgRequestPool::abortPendingRequests() {
    for (uint32_t i = 0; i < m_requests.size(); ++i) {
        m_requests[i].abortResponse();
    }
}

ErrorCode MsgRequestPool::waitPendingResponse(
    const MsgRequestData& requestData, Msg** response,
    uint64_t timeoutMillis /* = COMMUTIL_MSG_INFINITE_TIMEOUT */) {
    if (requestData.m_requestIndex >= m_requests.size()) {
        LOG_ERROR("Cannot wait for pending response with request index %u: out of range",
                  requestData.m_requestIndex);
        return ErrorCode::E_INVALID_ARGUMENT;
    }
    MsgRequest* request = &m_requests[requestData.m_requestIndex];
    if (request->getRequestId() != requestData.m_requestId) {
        LOG_ERROR("Cannot wait for pending response with request id %" PRIu64
                  ": expected request id %" PRIu64,
                  requestData.m_requestId, request->getRequestId());
        return ErrorCode::E_DATA_CORRUPT;
    }

    // wait for request cv to be signaled
    ErrorCode rc = request->waitResponse(response, timeoutMillis);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed waiting for response: %s", errorCodeToString(rc));
        return rc;
    } else if (*response == nullptr) {
        LOG_ERROR("Failed waiting for pending response with request id %" PRIu64
                  ": response is null",
                  requestData.m_requestId);
        return ErrorCode::E_INTERNAL_ERROR;
    }

    // can release response now
    request->clearResponse();
    return ErrorCode::E_OK;
}

ErrorCode MsgRequestPool::clearPendingRequest(Msg* response) {
    uint32_t requestIndex = response->getHeader().getRequestIndex();
    if (requestIndex >= m_requests.size()) {
        LOG_ERROR(
            "Cannot clear request entry for incoming response with request index %u: out of range",
            requestIndex);
        return ErrorCode::E_INVALID_ARGUMENT;
    }
    MsgRequest* request = &m_requests[requestIndex];
    uint64_t requestId = response->getHeader().getRequestId();
    if (request->getRequestId() != requestId) {
        LOG_ERROR("Cannot clear request entry for incoming response with request id %" PRIu64
                  ": expected request id %" PRIu64,
                  requestId, request->getRequestId());
        return ErrorCode::E_DATA_CORRUPT;
    }

    request->clearResponse();
    return ErrorCode::E_OK;
}

ErrorCode MsgRequestPool::setResponse(Msg* msg, uint32_t& requestFlags) {
    uint32_t requestIndex = msg->getHeader().getRequestIndex();
    if (requestIndex >= m_requests.size()) {
        LOG_ERROR("Invalid response request index: %u", requestIndex);
        return ErrorCode::E_INVALID_ARGUMENT;
    }

    MsgRequest& request = m_requests[requestIndex];
    uint64_t requestId = msg->getHeader().getRequestId();
    if (request.getRequestId() != requestId) {
        LOG_ERROR("Invalid request id: %" PRIu64, requestId);
        return ErrorCode::E_DATA_CORRUPT;
    }

    request.setResponse(msg);
    requestFlags = request.getFlags();
    if (m_listener != nullptr) {
        m_listener->onResponseArrived(request.getRequest(), msg);
    }
    return ErrorCode::E_OK;
}

ErrorCode MsgRequestPool::getRequest(Msg* response, Msg** requestMsg) {
    uint32_t requestIndex = response->getHeader().getRequestIndex();
    if (requestIndex >= m_requests.size()) {
        LOG_ERROR("Invalid request id %u, out of range (>= %zu)", requestIndex, m_requests.size());
        return ErrorCode::E_INVALID_ARGUMENT;
    }

    MsgRequest& request = m_requests[requestIndex];
    if (request.getRequestId() != response->getHeader().getRequestId()) {
        LOG_WARN("Mismatching request id, request has %" PRIu64 ", but response has %" PRIu64,
                 request.getRequestId(), response->getHeader().getRequestId());
        return ErrorCode::E_DATA_CORRUPT;
    }
    *requestMsg = request.getRequest();
    return ErrorCode::E_OK;
}

uint32_t MsgRequestPool::getReadyRequestCount() {
    uint32_t count = 0;
    forEachRequest([&count](MsgRequest& request) {
        if (request.hasResponse()) {
            ++count;
        }
    });
    return count;
}

}  // namespace commutil