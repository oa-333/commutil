#include "msg/msg_backlog.h"

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgBacklog)

void MsgBacklog::initialize(MsgRequestPool* requestPool) { m_requestPool = requestPool; }

ErrorCode MsgBacklog::addPendingRequest(Msg* pendingRequest, uint64_t requestTimeMillis) {
    uint64_t requestId = pendingRequest->getHeader().getRequestId();
    uint32_t entryIndex = (uint32_t)m_backlog.size();
    std::pair<HashIndex::iterator, bool> resPair =
        m_hashIndex.insert(HashIndex::value_type(requestId, {entryIndex, requestTimeMillis}));
    if (!resPair.second) {
        LOG_ERROR("Cannot add request with id %" PRIu64 " to backlog, duplicate id", requestId);
        return ErrorCode::E_ALREADY_EXISTS;
    }
    m_treeIndex.insert(TreeIndex::value_type(requestTimeMillis, entryIndex));
    m_backlog.push_back({pendingRequest, requestId, requestTimeMillis});
    m_backlogSizeBytes += pendingRequest->getPayloadSizeBytes();
    return ErrorCode::E_OK;
}

ErrorCode MsgBacklog::removePendingRequest(Msg* request) {
    // find request index by hash index
    uint64_t requestId = request->getHeader().getRequestId();
    HashIndex::iterator hashItr = m_hashIndex.find(requestId);
    if (hashItr == m_hashIndex.end()) {
        LOG_ERROR("Cannot remove request with id %" PRIu64 ", request not found", requestId);
        return ErrorCode::E_NOT_FOUND;
    }

    uint32_t entryIndex = hashItr->second.first;
    uint64_t requestTimeMillis = hashItr->second.second;

    // find in tree index by request time
    TreeIndex::iterator treeItr = findRequestByTime(requestTimeMillis, entryIndex);
    if (treeItr == m_treeIndex.end()) {
        LOG_ERROR("Cannot remove request with id %" PRIu64
                  ", data inconsistency (request not found in map)",
                  requestId);
        return ErrorCode::E_DATA_CORRUPT;
    }

    // get index in backlog
    if (entryIndex >= m_backlog.size()) {
        LOG_ERROR("Cannot remove pending request with id %" PRIu64
                  ", internal error (entry index out of range)",
                  requestId);
        return ErrorCode::E_INTERNAL_ERROR;
    }

    // if this is the last entry then we are done
    if (entryIndex == m_backlog.size() - 1) {
        m_hashIndex.erase(hashItr);
        m_treeIndex.erase(treeItr);
        m_backlog.pop_back();
        return ErrorCode::E_OK;
    }

    // exchange with last index
    uint32_t lastIndex = (uint32_t)m_backlog.size() - 1;
    uint64_t exchangeRequestId = m_backlog.back().m_requestId;
    uint64_t exchangeRequestTime = m_backlog.back().m_requestTimeMillis;

    // find exchanged entry in hash index
    HashIndex::iterator exHashItr = m_hashIndex.find(exchangeRequestId);
    if (exHashItr == m_hashIndex.end()) {
        LOG_ERROR("Cannot remove request with id %" PRIu64
                  ", data inconsistency (exchanged request %" PRIu64 " not found in hash map)",
                  requestId, exchangeRequestId);
        return ErrorCode::E_DATA_CORRUPT;
    }

    // find in tree index by exchange request time
    TreeIndex::iterator exTreeItr = findRequestByTime(exchangeRequestTime, lastIndex);
    if (exTreeItr == m_treeIndex.end()) {
        LOG_ERROR("Cannot remove request with id %" PRIu64
                  ", data inconsistency (exchange request %" PRIu64 " not found in tree map)",
                  requestId, exchangeRequestId);
        return ErrorCode::E_DATA_CORRUPT;
    }

    // now we can do the exchange
    exHashItr->second.first = entryIndex;
    exTreeItr->second = entryIndex;
    m_backlog[entryIndex] = m_backlog[lastIndex];
    m_backlog.pop_back();
    return ErrorCode::E_OK;
}

ErrorCode MsgBacklog::pruneBacklog(uint64_t maxBacklogSizeBytes) {
    ErrorCode rc = ErrorCode::E_OK;
    while (m_backlogSizeBytes > maxBacklogSizeBytes) {
        // check for internal error
        if (m_treeIndex.empty()) {
            LOG_ERROR(
                "Failed to prune request backlog, internal error (no more requests but size limit "
                "still breached)");
            return ErrorCode::E_INTERNAL_ERROR;
        }

        // first remove size from next pruned entry (otherwise the loop will never end)
        uint32_t entryIndex = m_treeIndex.begin()->second;
        // NOTE: assuming container is in consistent state
        BacklogEntry& entry = m_backlog[entryIndex];
        uint32_t messageSize = entry.m_request->getPayloadSizeBytes();
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
        uint64_t requestId = entry.m_requestId;
        ErrorCode rc2 = m_requestPool->clearPendingRequest(entry.m_request);
        if (rc2 != ErrorCode::E_OK) {
            LOG_WARN("Failed to clear from pool pending request with id %" PRIu64 ": %s", requestId,
                     errorCodeToString(rc2));
            if (rc == ErrorCode::E_OK) {
                rc = rc2;
            }
            // we keep going, this is best effort
        }

        // now remove from internal indices and backlog vector
        rc2 = removePendingRequest(entry.m_request);
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

MsgBacklog::TreeIndex::iterator MsgBacklog::findRequestByTime(uint64_t requestTimeMillis,
                                                              uint32_t entryIndex) {
    std::pair<TreeIndex::iterator, TreeIndex::iterator> rangeItr =
        m_treeIndex.equal_range(requestTimeMillis);
    TreeIndex::iterator treeItr = rangeItr.second;
    while (rangeItr.first != rangeItr.second) {
        if (rangeItr.first->second == entryIndex) {
            // entry found
            treeItr = rangeItr.first;
            break;
        }
        ++rangeItr.first;
    }

    // check that entry was found
    if (treeItr == rangeItr.second) {
        return m_treeIndex.end();
    }
    return treeItr;
}

}  // namespace commutil
