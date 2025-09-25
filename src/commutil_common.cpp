#include "commutil_common.h"

#include <cassert>
#include <cstring>

namespace commutil {

size_t commutil_strncpy(char* dest, const char* src, size_t destLen, size_t srcLen /* = 0 */) {
    assert(destLen > 0);
    if (srcLen == 0) {
        srcLen = strlen(src);
    }
    if (srcLen + 1 < destLen) {
        // copy terminating null as well (use faster memcpy())
        memcpy(dest, src, srcLen + 1);
        return srcLen;
    }
    // reserve one char for terminating null
    size_t copyLen = destLen - 1;
    memcpy(dest, src, copyLen);

    // add terminating null
    dest[copyLen] = 0;

    // return number of bytes copied, excluding terminating null
    return copyLen;
}

static int* kmp_borders(const char* needle, size_t needleLength) {
    int* borders = new (std::nothrow) int[needleLength + 1];
    if (borders == nullptr) {
        return nullptr;
    }
    int i = 0;
    int j = -1;
    borders[i] = j;
    while ((size_t)i < needleLength) {
        while (j >= 0 && needle[i] != needle[j]) {
            j = borders[j];
        }
        ++i;
        ++j;
        borders[i] = j;
    }
    return borders;
}

static int kmp_search(const char* haystack, size_t haystackLength, const char* needle,
                      size_t needleLength, int* borders) {
    const char* hayStart = haystack;
    int max_index = (int)(haystackLength - needleLength);
    int i = 0, j = 0;
    while (i <= max_index) {
        while (j < (int)needleLength && *haystack && needle[j] == *haystack) {
            ++j;
            ++haystack;
        }
        if (j == (int)needleLength) {
            int offset = (int)((haystack - hayStart) - needleLength);
            return (int)offset;
        }
        if (!(*haystack)) {
            return -1;
        }
        if (j == 0) {
            ++haystack;
            ++i;
        } else {
            do {
                i += j - borders[j];
                j = borders[j];
            } while (j > 0 && needle[j] != *haystack);
        }
    }
    return -1;
}

int commutil_strnstr(const char* haystack, size_t haystackLength, const char* needle,
                     size_t needleLength) {
    if (haystack == nullptr || needle == nullptr) {
        return -1;
    }
    if (haystackLength < needleLength) {
        return -1;
    }
    int* borders = kmp_borders(needle, needleLength);
    if (borders == nullptr) {
        return -1;
    }
    int res = kmp_search(haystack, haystackLength, needle, needleLength, borders);
    delete[] borders;
    return res;
}

}  // namespace commutil
