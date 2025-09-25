#ifndef __MSG_REQUEST_H__
#define __MSG_REQUEST_H__

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "comm_util_err.h"
#include "comm_util_log.h"
#include "msg/msg.h"

namespace commutil {

/** @var Asynchronous I/O request identifier type. */
typedef uint64_t commutil_request_id_t;

/** @def Invalid requests id. */
#define COMMUTIL_INVALID_REQUEST_ID ((commutil_request_id_t)0xFFFFFFFFFFFFFFFF)

/** @def Infinite timeout constant. */
#define COMMUTIL_MSG_INFINITE_TIMEOUT ((uint64_t)-1)

/** @def Configuration determined timeout constant. */
#define COMMUTIL_MSG_CONFIG_TIMEOUT ((uint64_t)-2)

/**
 * @def Flag denoting the request originates from a full transaction, and therefore a notification
 * for the incoming reply should not be fired.
 */
#define COMMUTIL_REQUEST_FLAG_TX 0x00000001

/** @enum Request status constants. */
enum class MsgRequestStatus : uint32_t { RS_IDLE, RS_PENDING, RS_ARRIVED, RS_ABORTED };

/** @brief Pending request data. */
struct COMMUTIL_API MsgRequestData {
    /** @brief The unique request id. */
    commutil_request_id_t m_requestId;

    /** @brief The reusable request index. */
    uint32_t m_requestIndex;

    /** @brief Alignment. */
    uint32_t m_padding;

    /** @brief Constructor. */
    MsgRequestData() : m_requestId(COMMUTIL_INVALID_REQUEST_ID), m_requestIndex(0) {}
    MsgRequestData(const MsgRequestData&) = delete;
    MsgRequestData(MsgRequestData&&) = delete;
    MsgRequestData& operator=(const MsgRequestData&) = delete;
    ~MsgRequestData() {}
};

/** @struct Single asynchronous request management. */
class COMMUTIL_API MsgRequest {
public:
    /** @brief Default constructor. */
    MsgRequest()
        : m_requestId(0),
          m_requestStatus(MsgRequestStatus::RS_IDLE),
          m_flags(0),
          m_request(nullptr),
          m_response(nullptr),
          m_sendTimeMillis(0),
          m_resendTimeMillis(0) {}

    /** @brief Copy constructor. */
    MsgRequest(const MsgRequest& request)
        : m_requestId(request.m_requestId),
          m_requestStatus(request.m_requestStatus.load()),
          m_flags(request.m_flags),
          m_request(request.m_request),
          m_response(request.m_response),
          m_sendTimeMillis(request.m_sendTimeMillis),
          m_resendTimeMillis(request.m_resendTimeMillis) {}

    MsgRequest(MsgRequest&&) = delete;
    MsgRequest& operator=(const MsgRequest&) = delete;

    ~MsgRequest() {}

    /** @brief Queries whther the request is vacant for use. */
    inline bool isVacant() {
        return m_requestStatus.load(std::memory_order_relaxed) == MsgRequestStatus::RS_IDLE;
    }

    /** @brief Queries whther the request is pending for a response. */
    inline bool isPending() {
        return m_requestStatus.load(std::memory_order_relaxed) == MsgRequestStatus::RS_PENDING;
    }

    /** @brief Race with other threads for taking this request for use. */
    bool tryGrab(uint64_t requestId);

    /** @brief Releases the  */
    inline void release() {
        m_requestStatus.store(MsgRequestStatus::RS_IDLE, std::memory_order_seq_cst);
    }

    /** @brief Installs the request. */
    void setRequest(Msg* request);

    /** @brief Retrieves the request. */
    Msg* getRequest();

    /** @brief Retrieves request flags. */
    uint32_t getFlags();

    /** @brief Sets request flags. */
    void setFlags(uint32_t flags);

    /** @brief Retrieves the request time. */
    uint64_t getRequestTimeMillis();

    /** @brief Clears the request and returns it. */
    Msg* clearRequest();

    /** @brief Retrieves the last resend time. */
    uint64_t getResendTime();

    /** @brief Updates the last resend time. */
    void updateResendTime();

    /**
     * @brief Waits for response to arrive.
     * @param[out] response The resulting response.
     * @param timeoutMillis Optional timeout in milliseconds (by default waits indefinitely). A
     * value of zero will only poll for response state and return immediately.
     * @return E_OK Indicates that the response has arrived.
     * @return E_TIMED_OUT If a finite timeout was specified and the response has not arrived yet.
     * @return E_INVALID_STATE Indicates that there is no request pending for response.
     * @return E_IO_CANCELED If the request has been aborted by a call to @ref abortResponse().
     */
    ErrorCode waitResponse(Msg** response, uint64_t timeoutMillis = COMMUTIL_MSG_INFINITE_TIMEOUT);

    /** @brief Installs the response for this request and notifies anyone waiting. */
    void setResponse(Msg* response);

    /** @brief Queries whether a response for the request has arrived. */
    bool hasResponse();

    /** @brief Notifies waiting for response should be aborted. */
    void abortResponse();

    /** @brief Clears response data and marks the request as vacant for use. */
    void clearResponse();

    inline uint64_t getRequestId() const { return m_requestId; }

private:
    /** @brief The unique request id. */
    uint64_t m_requestId;

    /** @brief The volatile request status. */
    std::atomic<MsgRequestStatus> m_requestStatus;

    /** @brief Flags. */
    uint32_t m_flags;

    /** @brief The outgoing request. */
    Msg* m_request;

    /** @brief The matching response (null until response arrives). */
    Msg* m_response;

    /** @brief The request send time in milliseconds (for expiry). */
    uint64_t m_sendTimeMillis;

    /** @brief The request send time in milliseconds (for resend). */
    uint64_t m_resendTimeMillis;

    /** @brief Lock to synchronize object access. */
    std::mutex m_lock;

    /** @brief Condition variable used to wait for object status change. */
    std::condition_variable m_cv;

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_REQUEST_H__