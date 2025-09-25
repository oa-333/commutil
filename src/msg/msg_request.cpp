#include "msg/msg_request.h"

#include <cinttypes>

#include "commutil_common.h"
#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgRequest)

bool MsgRequest::tryGrab(uint64_t requestId) {
    MsgRequestStatus status = m_requestStatus.load(std::memory_order_acquire);
    if (status != MsgRequestStatus::RS_IDLE) {
        return false;
    }
    if (m_requestStatus.compare_exchange_strong(status, MsgRequestStatus::RS_PENDING,
                                                std::memory_order_seq_cst)) {
        m_requestId = requestId;
        return true;
    }
    return false;
}

void MsgRequest::setRequest(Msg* request) {
    std::unique_lock<std::mutex> lock(m_lock);
    m_request = request;
    m_sendTimeMillis = getCurrentTimeMillis();
}

Msg* MsgRequest::getRequest() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_request;
}

uint32_t MsgRequest::getFlags() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_flags;
}

void MsgRequest::setFlags(uint32_t flags) {
    std::unique_lock<std::mutex> lock(m_lock);
    m_flags = flags;
}

uint64_t MsgRequest::getRequestTimeMillis() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_sendTimeMillis;
}

Msg* MsgRequest::clearRequest() {
    std::unique_lock<std::mutex> lock(m_lock);
    Msg* request = m_request;
    m_request = nullptr;
    m_sendTimeMillis = 0;
    return request;
}

uint64_t MsgRequest::getResendTime() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_resendTimeMillis;
}

void MsgRequest::updateResendTime() {
    std::unique_lock<std::mutex> lock(m_lock);
    m_resendTimeMillis = getCurrentTimeMillis();
}

ErrorCode MsgRequest::waitResponse(Msg** response,
                                   uint64_t timeoutMillis /* = COMMUTIL_MSG_INFINITE_TIMEOUT */) {
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_requestStatus == MsgRequestStatus::RS_IDLE) {
        LOG_ERROR("Cannot wait for response, no pending request");
        return ErrorCode::E_INVALID_STATE;
    }
    if (timeoutMillis == COMMUTIL_MSG_INFINITE_TIMEOUT) {
        m_cv.wait(lock, [this]() {
            return m_requestStatus == MsgRequestStatus::RS_ARRIVED ||
                   m_requestStatus == MsgRequestStatus::RS_ABORTED;
        });
    } else {
        if (!m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMillis), [this]() {
                return m_requestStatus == MsgRequestStatus::RS_ARRIVED ||
                       m_requestStatus == MsgRequestStatus::RS_ABORTED;
            })) {
            LOG_WARN("Timed out waiting for response to request %" PRIu64, m_requestId);
        }
    }

    MsgRequestStatus requestStatus = m_requestStatus.load(std::memory_order_relaxed);
    switch (requestStatus) {
        case MsgRequestStatus::RS_IDLE:
            return ErrorCode::E_INVALID_STATE;

        case MsgRequestStatus::RS_PENDING:
            return ErrorCode::E_TIMED_OUT;

        case MsgRequestStatus::RS_ABORTED:
            return ErrorCode::E_IO_CANCELED;

        case MsgRequestStatus::RS_ARRIVED:
            *response = m_response;
            return ErrorCode::E_OK;

        default:
            LOG_ERROR("Invalid request status: %u", (unsigned)requestStatus);
            return ErrorCode::E_INTERNAL_ERROR;
    }
}

void MsgRequest::setResponse(Msg* response) {
    std::unique_lock<std::mutex> lock(m_lock);
    m_response = response;
    m_requestStatus = MsgRequestStatus::RS_ARRIVED;
    m_cv.notify_one();
}

bool MsgRequest::hasResponse() {
    std::unique_lock<std::mutex> lock(m_lock);
    return (m_requestStatus == MsgRequestStatus::RS_ARRIVED && m_response != nullptr);
}

void MsgRequest::abortResponse() {
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_requestStatus == MsgRequestStatus::RS_PENDING) {
        m_response = nullptr;
        m_requestStatus = MsgRequestStatus::RS_ABORTED;
        m_cv.notify_one();
    }
}

void MsgRequest::clearResponse() {
    std::unique_lock<std::mutex> lock(m_lock);
    m_request = nullptr;
    m_sendTimeMillis = 0;
    m_response = nullptr;
    m_requestStatus = MsgRequestStatus::RS_IDLE;
}

}  // namespace commutil
