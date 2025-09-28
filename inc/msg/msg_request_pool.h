#ifndef __MSB_REQUEST_POOL_H__
#define __MSB_REQUEST_POOL_H__

#include <atomic>
#include <vector>

#include "comm_util_log.h"
#include "msg/msg_request.h"

namespace commutil {

/** @brief A listener for receiving notifications of arriving responses. */
class COMMUTIL_API MsgRequestListener {
public:
    virtual ~MsgRequestListener() {}

    /**
     * @brief Notify incoming response for a pending request.
     * @param request The pending request.
     * @param response The incoming response.
     */
    virtual void onResponseArrived(Msg* request, Msg* response) = 0;

protected:
    MsgRequestListener() {}
    MsgRequestListener(const MsgRequestListener&) = delete;
    MsgRequestListener(MsgRequestListener&&) = delete;
    MsgRequestListener& operator=(const MsgRequestListener&) = delete;
};

class COMMUTIL_API MsgRequestPool {
public:
    MsgRequestPool() : m_listener(nullptr) {}
    MsgRequestPool(const MsgRequestPool&) = delete;
    MsgRequestPool(MsgRequestPool&&) = delete;
    MsgRequestPool& operator=(const MsgRequestPool&) = delete;
    ~MsgRequestPool() {}

    void initialize(uint32_t poolSize);
    void terminate();
    void setListener(MsgRequestListener* listener);

    /** @brief Allocates a unique request id (atomic). */
    inline uint64_t fetchAddRequestId() {
        return m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Searches for a vacant request and mark it as in use (race with other threads).
     * @param requestId The request id to use.
     * @param[out] requestIndex The resulting request index in case of success.
     * @return MsgRequest* The resulting request object, or null if no vacant request was found.
     */
    MsgRequest* getVacantRequest(uint64_t requestId, uint32_t& requestIndex);

    /** @brief Retrieves the request entry for the given request. */
    MsgRequest* getRequestData(Msg* request);

    /** @brief Aborts all pending requests. */
    void abortPendingRequests();

    /**
     * @brief Executes an operation on all requests.
     * @tparam F The operation function type.
     * @param f The operation to execute (can be lambda expression).
     */
    template <typename F>
    void forEachRequest(F f) {
        for (auto& request : m_requests) {
            f(request);
        }
    }

    /**
     * @brief Waits for a pending response.
     * @param requestData The request data used to identify the pending request.
     * @param[out] response The resulting response.
     * @param timeoutMillis Optional timeout for waiting.
     * @return The operation result.
     */
    ErrorCode waitPendingResponse(const MsgRequestData& requestData, Msg** response,
                                  uint64_t timeoutMillis = COMMUTIL_MSG_INFINITE_TIMEOUT);

    /**
     * @brief Clears the pending request entry for a given response.
     * @param response The incoming response message.
     */
    ErrorCode clearPendingRequest(Msg* response);

    /**
     * @brief Sets the response for a pending request.
     * @param msg The response message.
     * @param[out] requestFlags The original request flags.
     * @return ErrorCode The operation result.
     */
    ErrorCode setResponse(Msg* msg, uint32_t& requestFlags);

    /** @brief Retrieves the original request for the given response. */
    ErrorCode getRequest(Msg* response, Msg** request);

    /** @brief Retrieves the number of ready requests. */
    uint32_t getReadyRequestCount();

private:
    // pending requests members
    std::vector<MsgRequest> m_requests;

    /** @var The next unique request id. */
    std::atomic<uint64_t> m_nextRequestId;

    MsgRequestListener* m_listener;

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSB_REQUEST_POOL_H__