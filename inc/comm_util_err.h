#ifndef __COMM_UTIL_ERR_H__
#define __COMM_UTIL_ERR_H__

#include <cstdint>

#include "comm_util_def.h"

namespace commutil {

enum class ErrorCode : uint32_t {
    E_OK,
    E_NOMEM,
    E_INVALID_ARGUMENT,
    E_INVALID_STATE,
    E_RESOURCE_LIMIT,
    E_IO_CANCELED,
    E_CONCURRENT_MODIFICATION,
    E_SYSTEM_FAILURE,
    E_NOT_FOUND,
    E_INTERNAL_ERROR,
    E_EOF,
    E_ALREADY_EXISTS,
    E_INPROGRESS,
    E_NET_ERROR,
    E_END_OF_STREAM,
    E_PROTOCOL_ERROR,
    E_SERVER_ERROR,
    E_NOT_IMPLEMENTED,
    E_INVALID_CONNECTION,
    E_SESSION_NOT_FOUND,
    E_SESSION_STALE,
    E_ABORTED,
    E_DATA_CORRUPT,
    E_TRANSPORT_ERROR,
    E_TIMED_OUT,

    // keep this always last
    E_LAST_ERROR
};

/** @brief Converts error code to string. */
extern COMMUTIL_API const char* errorCodeToString(ErrorCode err);

}  // namespace commutil

#endif  // __COMM_UTIL_ERR_H__