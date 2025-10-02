#ifndef __MSG_CONFIG_H__
#define __MSG_CONFIG_H__

#include <cstdint>

#include "comm_util_def.h"

/** @def By default wait for 5 seconds before declaring connection failure. */
#define COMMUTIL_MSG_DEFAULT_CONNECT_TIMEOUT_MILLIS 5000

/** @def By default wait for 1 second before declaring send transaction failure. */
#define COMMUTIL_MSG_DEFAULT_SEND_TIMEOUT_MILLIS 1000

/** @def By default wait for 5 seconds before trying to resend failed messages. */
#define COMMUTIL_MSG_DEFAULT_RESEND_TIMEOUT_MILLIS 5000

/** @def By default wait for 30 seconds before dropping failed messages. */
#define COMMUTIL_MSG_DEFAULT_EXPIRE_TIMEOUT_MILLIS 30000

/** @def By default allow for a total 1 MB of payload to be backlogged for resend. */
#define COMMUTIL_MSG_DEFAULT_BACKLOG_LIMIT_BYTES (1024 * 1024)

/**
 * @def By default wait for 5 seconds before trying to resend failed messages during shutdown.
 */
#define COMMUTIL_MSG_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS 5000

/**
 * @def By default wait for 50 milliseconds for incoming responses during shutdown.
 */
#define COMMUTIL_MSG_DEFAULT_SHUTDOWN_POLLING_TIMEOUT_MILLIS 50

namespace commutil {

/** @brief Pack all message configuration in one place. */
struct COMMUTIL_API MsgConfig {
    /** @brief The timeout for transport connect to be declared as failed. */
    uint64_t m_connectTimeoutMillis;

    /** @brief The timeout for message write to be declared as failed. */
    uint64_t m_sendTimeoutMillis;

    /** @brief The interval between resend attempt of previously failed message send. */
    uint64_t m_resendPeriodMillis;

    /** @brief The time allowed for attempting resending, after which the request is dropped. */
    uint64_t m_expireTimeoutMillis;

    /** @brief The size limit of the backlog used for storing messages after failed send attempt. */
    uint64_t m_backlogLimitBytes;

    /** @brief The timeout used for final attempt to resend all unsent messages. */
    uint64_t m_shutdownTimeoutMillis;

    /**
     * @brief The timeout used for final attempt to resend all unsent messages. Determines the
     * granularity of waiting for incoming responses during shutdown phase.
     */
    uint64_t m_shutdownPollingTimeoutMillis;

    MsgConfig()
        : m_connectTimeoutMillis(COMMUTIL_MSG_DEFAULT_CONNECT_TIMEOUT_MILLIS),
          m_sendTimeoutMillis(COMMUTIL_MSG_DEFAULT_SEND_TIMEOUT_MILLIS),
          m_resendPeriodMillis(COMMUTIL_MSG_DEFAULT_RESEND_TIMEOUT_MILLIS),
          m_expireTimeoutMillis(COMMUTIL_MSG_DEFAULT_EXPIRE_TIMEOUT_MILLIS),
          m_backlogLimitBytes(COMMUTIL_MSG_DEFAULT_BACKLOG_LIMIT_BYTES),
          m_shutdownTimeoutMillis(COMMUTIL_MSG_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS),
          m_shutdownPollingTimeoutMillis(COMMUTIL_MSG_DEFAULT_SHUTDOWN_POLLING_TIMEOUT_MILLIS) {}
};

}  // namespace commutil

#endif  // __MSG_CONFIG_H__