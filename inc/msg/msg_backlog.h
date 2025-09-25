#ifndef __MSG_BACKLOG_H__
#define __MSG_BACKLOG_H__

#include <list>
#include <map>
#include <mutex>
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

    inline uint32_t getEntryCount() const { return (uint32_t)m_backlog.size(); }

    /** @brief Queries whether the backlog is empty. */
    inline bool isEmpty() const { return m_backlog.empty(); }

    /** @brief Adds pending request to the backlog. */
    ErrorCode addPendingRequest(Msg* pendingRequest, uint64_t requestTimeMillis);

    /** @brief Removes a specific request from the backlog (either finished or expired). */
    ErrorCode removePendingRequest(Msg* request);

    /**
     * @brief Ensures backlog fits size limit. Removes oldest requests as needed. Requests in the
     * pool are aborted accordingly.
     */
    ErrorCode pruneBacklog(uint64_t maxBacklogSizeBytes);

    /** @brief Traverses all pending requests with an external visitor. */
    template <typename Visitor>
    inline void forEachRequest(Visitor visitor) const {
        for (const BacklogEntry& entry : m_backlog) {
            if (!visitor(entry.m_request)) {
                break;
            }
        }
    }

private:
    uint64_t m_backlogSizeBytes;
    MsgRequestPool* m_requestPool;

    struct BacklogEntry {
        Msg* m_request;
        uint64_t m_requestId;
        uint64_t m_requestTimeMillis;
    };

    typedef std::vector<BacklogEntry> Backlog;
    Backlog m_backlog;

    // map from request index to backlog entry and request time index
    typedef std::unordered_map<uint64_t, std::pair<uint32_t, uint64_t>> HashIndex;
    HashIndex m_hashIndex;

    // index from request time to backlog entry index, ordered by request time
    // we use multimap because several requests may have same timestamp
    typedef std::multimap<uint64_t, uint32_t> TreeIndex;
    TreeIndex m_treeIndex;

    TreeIndex::iterator findRequestByTime(uint64_t requestTimeMillis, uint32_t entryIndex);

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_BACKLOG_H__