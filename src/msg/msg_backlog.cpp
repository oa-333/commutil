#include "msg/msg_backlog.h"

#include <algorithm>

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgBacklog)

void MsgBacklog::initialize(MsgRequestPool* requestPool) { m_requestPool = requestPool; }

ErrorCode MsgBacklog::addPendingRequest(Msg* pendingRequest) {
    uint64_t requestId = pendingRequest->getHeader().getRequestId();
    MsgRequest* requestData = m_requestPool->getRequestData(pendingRequest);
    if (requestData == nullptr) {
        LOG_ERROR("Cannot add pending request, request with id %" PRIu64
                  " not found in request pool",
                  requestId);
        return ErrorCode::E_NOT_FOUND;
    }

    std::pair<RequestMap::iterator, bool> resPair =
        m_requestMap.insert(RequestMap::value_type(requestId, {pendingRequest, requestData}));
    if (!resPair.second) {
        LOG_ERROR("Cannot add request with id %" PRIu64 " to backlog, duplicate id", requestId);
        return ErrorCode::E_ALREADY_EXISTS;
    }

    std::pair<RequestSet::iterator, bool> resPair2 =
        m_requestSet.insert(RequestSet::value_type({pendingRequest, requestData}));
    if (!resPair2.second) {
        LOG_ERROR("Cannot add request with id %" PRIu64 " to backlog, request set inconsistency",
                  requestId);
        m_requestMap.erase(resPair.first);
        return ErrorCode::E_DATA_CORRUPT;
    }

    std::pair<MinHeap::iterator, bool> resPair3 = m_minHeap.insert({pendingRequest, requestData});
    if (!resPair3.second) {
        LOG_ERROR("Cannot add request with id %" PRIu64 " to backlog, heap inconsistency",
                  requestId);
        m_requestSet.erase(resPair2.first);
        m_requestMap.erase(resPair.first);
        return ErrorCode::E_DATA_CORRUPT;
    }
    m_backlogSizeBytes += pendingRequest->getPayloadSizeBytes();
    LOG_TRACE("Added pending request %" PRIu64, requestId);
    return ErrorCode::E_OK;
}

ErrorCode MsgBacklog::removePendingRequest(Msg* request) {
    // find request index by hash index
    uint64_t requestId = request->getHeader().getRequestId();
    LOG_TRACE("Removing pending request %" PRIu64, requestId);
    RequestMap::iterator mapItr = m_requestMap.find(requestId);
    if (mapItr == m_requestMap.end()) {
        LOG_ERROR("Cannot remove request with id %" PRIu64 ", request not found", requestId);
        return ErrorCode::E_NOT_FOUND;
    }

    uint64_t requestTimeMillis = mapItr->second.m_request->getRequestTimeMillis();

    // find in set by request time
    RequestSet::iterator setItr = findRequestByTime(requestTimeMillis, request);
    if (setItr == m_requestSet.end()) {
        LOG_ERROR("Cannot remove request with id %" PRIu64
                  ", data inconsistency (request not found in set)",
                  requestId);
        return ErrorCode::E_DATA_CORRUPT;
    }
    MsgRequest* requestData = setItr->m_request;

    // find in priority queue
    uint64_t resendTime = requestData->getResendTime();
    ResendTimeCompare cmp(resendTime);
#if 0 && COMMUTIL_CPP_VER >= 201304L
    MinHeap::iterator heapItr = m_minHeap.find(cmp);
#else
    MinHeap::iterator heapItr =
        std::find_if(m_minHeap.begin(), m_minHeap.end(),
                     [request](const BacklogEntry& entry) { return (entry.m_msg == request); });
#endif
    while (heapItr != m_minHeap.end() && heapItr->m_msg != request &&
           heapItr->m_request->getResendTime() == resendTime) {
        ++heapItr;
    }
    if (heapItr == m_minHeap.end() || heapItr->m_msg != request) {
        LOG_ERROR("Cannot remove request with id %" PRIu64
                  ", data inconsistency (request not found in heap)",
                  requestId);
        return ErrorCode::E_DATA_CORRUPT;
    }

    m_requestMap.erase(mapItr);
    m_requestSet.erase(setItr);
    m_minHeap.erase(heapItr);

    // update total contained size
    m_backlogSizeBytes -= request->getPayloadSizeBytes();
    return ErrorCode::E_OK;
}

ErrorCode MsgBacklog::pruneBacklog(uint64_t maxBacklogSizeBytes) {
    ErrorCode rc = ErrorCode::E_OK;
    while (m_backlogSizeBytes > maxBacklogSizeBytes) {
        // check for internal error
        if (m_requestSet.empty()) {
            LOG_ERROR(
                "Failed to prune request backlog, internal error (no more requests but size limit "
                "still breached)");
            return ErrorCode::E_INTERNAL_ERROR;
        }

        // first remove size from next pruned entry (otherwise the loop will never end)
        BacklogEntry entry = *m_requestSet.begin();
        // NOTE: assuming container is in consistent state
        uint32_t messageSize = entry.m_msg->getPayloadSizeBytes();
        if (messageSize > m_backlogSizeBytes) {
            // impossible, but still be careful
            LOG_ERROR("Message body size (%u bytes) exceeds current backlog size (%" PRIu64
                      " bytes), resetting backlog size to zero",
                      messageSize, m_backlogSizeBytes);
            m_backlogSizeBytes = 0;
        } else {
            m_backlogSizeBytes -= messageSize;
        }

        // first remove from pool
        // NOTE: entry may become invalid, so we copy first request id (to be used later)
        uint64_t requestId = entry.m_msg->getHeader().getRequestId();
        LOG_WARN("Removing from backlog request %" PRIu64 " due to size limit", requestId);
        ErrorCode rc2 = m_requestPool->clearPendingRequest(entry.m_msg);
        if (rc2 != ErrorCode::E_OK) {
            LOG_WARN("Failed to clear from pool pending request with id %" PRIu64 ": %s", requestId,
                     errorCodeToString(rc2));
            if (rc == ErrorCode::E_OK) {
                rc = rc2;
            }
            // we keep going, this is best effort
        }

        // now remove from internal indices and backlog vector
        rc2 = removePendingRequest(entry.m_msg);
        if (rc2 != ErrorCode::E_OK) {
            LOG_WARN("Failed to prune pending request with id %" PRIu64 ": %s", requestId,
                     errorCodeToString(rc2));
            if (rc == ErrorCode::E_OK) {
                rc = rc2;
            }
            // we keep going, this is best effort
        }
    }

    return rc;
}

bool MsgBacklog::hasReadyResendRequest(uint64_t timerBarrierMillis) {
    return !m_requestSet.empty() &&
           m_requestSet.begin()->m_request->getResendTime() < timerBarrierMillis;
}

Msg* MsgBacklog::getReadyResendRequest() { return m_requestSet.begin()->m_msg; }

void MsgBacklog::updateReadyResendRequest() {
    // entry contains updated resend time so we just remove and re-insert it
    RequestSet::iterator itr = m_requestSet.begin();
    BacklogEntry entry = *itr;
    m_requestSet.erase(itr);
    m_requestSet.insert(entry);
}

MsgBacklog::RequestSet::iterator MsgBacklog::findRequestByTime(uint64_t requestTimeMillis,
                                                               Msg* request) {
    RequestTimeCompare cmp(requestTimeMillis);
#if 0 && COMMUTIL_CPP_VER >= 201304L
    std::pair<RequestSet::iterator, RequestSet::iterator> rangeItr = m_requestSet.equal_range(cmp);
    RequestSet::iterator setItr = rangeItr.second;
    while (rangeItr.first != rangeItr.second) {
        if (rangeItr.first->m_msg == request) {
            // entry found
            setItr = rangeItr.first;
            break;
        }
        ++rangeItr.first;
    }

    // check that entry was found
    if (setItr == rangeItr.second) {
        return m_requestSet.end();
    }
    return setItr;
#else
    RequestSet::iterator setItr = std::lower_bound(m_requestSet.begin(), m_requestSet.end(), cmp);
    while (setItr != m_requestSet.end() && setItr->m_msg != request &&
           setItr->m_request->getRequestTimeMillis() == requestTimeMillis) {
        ++setItr;
    }
    if (setItr != m_requestSet.end() && setItr->m_msg != request) {
        setItr = m_requestSet.end();
    }
    return setItr;
#endif
}

}  // namespace commutil
