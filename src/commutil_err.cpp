#include <cstdlib>

#include "comm_util_err.h"

namespace commutil {

static const char* sErrorCodeStr[] = {
    "No error",                 // E_OK
    "Out of memory",            // E_NOMEM
    "Invalid argument",         // E_INVALID_ARGUMENT
    "Invalid state",            // E_INVALID_STATE
    "Resource limit",           // E_RESOURCE_LIMIT
    "I/O canceled",             // E_IO_CANCELED
    "Concurrent modification",  // E_CONCURRENT_MODIFICATION
    "System failure",           // E_SYSTEM_FAILURE
    "Not found",                // E_NOT_FOUND
    "Internal error",           // E_INTERNAL_ERROR
    "End of file",              // E_EOF
    "Already exists",           // E_ALREADY_EXISTS
    "Operation in progress",    // E_INPROGRESS
    "Network error",            // E_NET_ERROR
    "End of stream",            // E_END_OF_STREAM
    "Protocol error",           // E_PROTOCOL_ERROR
    "Server error",             // E_SERVER_ERROR
    "Not implemented",          // E_NOT_IMPLEMENTED
    "Invalid connection",       // E_INVALID_CONNECTION,
    "Session not found",        // E_SESSION_NOT_FOUND,
    "Session is stale",         // E_SESSION_STALE,
    "Operation aborted",        // E_ABORTED
    "Data corrupted",           // E_DATA_CORRUPT
    "Transport error",          // E_TRANSPORT_ERROR
    "Operation timed out"       // E_TIMED_OUT
};

const char* errorCodeToString(ErrorCode rc) {
    if ((uint32_t)rc < (uint32_t)ErrorCode::E_LAST_ERROR) {
        return sErrorCodeStr[(uint32_t)rc];
    }
    return "Unknown error";
}

}  // namespace commutil