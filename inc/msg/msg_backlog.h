#ifndef __MSG_BACKLOG_H__
#define __MSG_BACKLOG_H__

#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include <vector>

#include "comm_util_def.h"
#include "comm_util_log.h"
#include "msg.h"
#include "msg_request_pool.h"

namespace commutil {

/** @brief Backlog management. Not thread-safe. */
class COMMUTIL_API MsgBacklog {
public:
    MsgBacklog() : m_backlogSizeBytes(0), m_requestPool(nullptr) {}
    MsgBacklog(const MsgBacklog&) = delete;
    MsgBacklog(MsgBacklog&&) = delete;
    MsgBacklog& operator=(const MsgBacklog&) = delete;
    ~MsgBacklog() {}

    /** @brief Initializes the backlog with a size limit. */
    void initialize(MsgRequestPool* requestPool);

    inline uint32_t getEntryCount() const { return (uint32_t)m_requestMap.size(); }

    /** @brief Queries whether the backlog is empty. */
    inline bool isEmpty() const { return m_requestMap.empty(); }

    /** @brief Adds pending request to the backlog. */
    ErrorCode addPendingRequest(Msg* pendingRequest);

    /** @brief Removes a specific request from the backlog (either finished or expired). */
    ErrorCode removePendingRequest(Msg* request);

    /**
     * @brief Ensures backlog fits size limit. Removes oldest requests as needed. Requests in the
     * pool are aborted accordingly.
     */
    ErrorCode pruneBacklog(uint64_t maxBacklogSizeBytes);

    // TODO: add API for
    // query request set min item for resend due time
    // update item (pop from heap update resend time and put back in heap)

    /**
     * @brief Queries Whether the backlog has any request ready for resend according to the given
     * time barrier (normally that would be the current time).
     * @param timerBarrierMillis The time barrier.
     * @return True if there is at least one request ready for resending.
     */
    bool hasReadyResendRequest(uint64_t timerBarrierMillis);

    /** @brief Retrieves the first request ready for resending. */
    Msg* getReadyResendRequest();

    /**
     * @brief Updates the first request that was ready for resending (assuming its resend has just
     * been updated).
     */
    void updateReadyResendRequest();

    /** @brief Traverses all pending requests with an external visitor. */
    /*template <typename Visitor>
    inline void forEachRequest(Visitor visitor) const {
        for (const BacklogEntry& entry : m_backlog) {
            if (!visitor(entry.m_request)) {
                break;
            }
        }
    }*/

private:
    uint64_t m_backlogSizeBytes;
    MsgRequestPool* m_requestPool;

    struct BacklogEntry {
        Msg* m_msg;
        MsgRequest* m_request;
    };

    // map from request index to backlog entry and request time index
    typedef std::unordered_map<uint64_t, BacklogEntry> RequestMap;
    RequestMap m_requestMap;

    // index from request time to backlog entry index, ordered by request time
    // we use multimap because several requests may have same timestamp
    // the tree index is used for pruning
    struct EntryRequestTimeCmp {
        inline bool operator()(const BacklogEntry& lhs, const BacklogEntry& rhs) const {
            uint64_t lhsTime = lhs.m_request->getRequestTimeMillis();
            uint64_t rhsTime = rhs.m_request->getRequestTimeMillis();
            if (lhsTime < rhsTime) {
                return true;
            }
            if (lhsTime > rhsTime) {
                return false;
            }
            return lhs.m_msg->getHeader().getRequestId() < rhs.m_msg->getHeader().getRequestId();
        }
    };
    typedef std::set<BacklogEntry, EntryRequestTimeCmp> RequestSet;
    RequestSet m_requestSet;

    // manage a minimum heap so that we can tell which next request is due for resend
    // the minimum heap is sorted by next resend time
    struct EntryResendTimeCmp {
        inline bool operator()(const BacklogEntry& lhs, const BacklogEntry& rhs) const {
            uint64_t lhsTime = lhs.m_request->getResendTime();
            uint64_t rhsTime = rhs.m_request->getResendTime();
            if (lhsTime < rhsTime) {
                return true;
            }
            if (lhsTime > rhsTime) {
                return false;
            }
            return lhs.m_msg->getHeader().getRequestId() < rhs.m_msg->getHeader().getRequestId();
        }
    };

    // use set instead of priority queue due to erase requirement
    typedef std::set<BacklogEntry, EntryResendTimeCmp> MinHeap;
    MinHeap m_minHeap;

    RequestSet::iterator findRequestByTime(uint64_t requestTimeMillis, Msg* request);

    // comparator for set.equal_range
    struct RequestTimeCompare {
        uint64_t m_requestTimeMillis;

        RequestTimeCompare(uint64_t requestTimeMillis = 0)
            : m_requestTimeMillis(requestTimeMillis) {}

        friend bool operator<(const BacklogEntry& entry, const RequestTimeCompare& requestTime) {
            return entry.m_request->getRequestTimeMillis() < requestTime.m_requestTimeMillis;
        }
        friend bool operator<(const RequestTimeCompare& requestTime, const BacklogEntry& entry) {
            return requestTime.m_requestTimeMillis < entry.m_request->getRequestTimeMillis();
        }
    };

    // comparator for set.find

    struct ResendTimeCompare {
        uint64_t m_resendTimeMillis;

        ResendTimeCompare(uint64_t requestTimeMillis = 0) : m_resendTimeMillis(requestTimeMillis) {}

        friend bool operator<(const BacklogEntry& entry, const ResendTimeCompare& resendTime) {
            return entry.m_request->getResendTime() < resendTime.m_resendTimeMillis;
        }
        friend bool operator<(const ResendTimeCompare& resendTime, const BacklogEntry& entry) {
            return resendTime.m_resendTimeMillis < entry.m_request->getResendTime();
        }
    };

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_BACKLOG_H__