#ifndef __COMMUTIL_COMMON_H__
#define __COMMUTIL_COMMON_H__

#include <chrono>
#include <cstdint>
#include <string>

#include "comm_util_def.h"
#include "comm_util_err.h"
#include "comm_util_log.h"

namespace commutil {

/**
 * @brief Safer and possibly/hopefully faster version of strncpy() (not benchmarked yet). Unlike
 * strncpy(), this implementation has three notable differences:
 * (1) The resulting destination always has a terminating null
 * (2) In case of a short source string, the resulting destination is not padded with many nulls
 * up to the size limit, but rather only one terminating null is added (3) The result value is
 * the number of characters copied, not including the terminating null.
 * @param dest The destination string.
 * @param src The source string.
 * @param destLen The destination length.
 * @param srcLen The source length (optional, can run faster if provided).
 * @return The number of characters copied.
 */
extern size_t commutil_strncpy(char* dest, const char* src, size_t destLen, size_t srcLen = 0);

/**
 * @brief Searches for a pattern within a data buffer. Unlike strstr(), nulls are allowed within
 * both haystack and needle, and both of which are not required to be null-terminated.
 * @param haystack The haystack to search in.
 * @param haystackLength The haystack length.
 * @param needle The needle to search for within the haystack.
 * @param needleLength The needle length.
 * @return Returns the first occurrence of the needle within the haystack (non-negative number)
 * if the needle was found within the haystack, otherwise -1.
 */
extern int commutil_strnstr(const char* haystack, size_t haystackLength, const char* needle,
                            size_t needleLength);

/** @brief Retrieves a time stamp. */
inline uint64_t getCurrentTimeMillis() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace commutil

#endif  // __COMMUTIL_COMMON_H__