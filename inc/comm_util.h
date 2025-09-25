#ifndef __COMM_UTIL_H__
#define __COMM_UTIL_H__

#include "comm_util_def.h"
#include "comm_util_err.h"
#include "comm_util_log.h"

namespace commutil {

/**
 * @brief Initializes the communication utility library with internal logging enabled.
 *
 * @param logHandler Optional log handler to receive commutil internal log messages.
 * @param severity Optionally control the log severity of reported log messages. Any log messages
 * below this severity will be discarded. By default only fatal messages are sent to log.
 * @return ErrorCode::E_OK If succeeded, otherwise an error code.
 */
extern COMMUTIL_API ErrorCode initCommUtil(LogHandler* logHandler = nullptr,
                                           LogSeverity severity = LS_FATAL);

/** @brief Terminates the communication utility library. */
extern COMMUTIL_API ErrorCode termCommUtil();

/** @brief Queries whether the communication utility library is initialized. */
extern COMMUTIL_API bool isCommUtilInitialized();

}  // namespace commutil

#endif  // __COMM_UTIL_H__