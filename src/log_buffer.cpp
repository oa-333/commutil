#include "log_buffer.h"

#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "commutil_common.h"

namespace commutil {

LogBuffer::~LogBuffer() {
#ifndef __MINGW32__
    reset();
#endif
}

bool LogBuffer::resize(size_t newSize) {
    if (m_bufferSize < newSize) {
        if (newSize > COMMUTIL_MAX_BUFFER_SIZE) {
            fprintf(stderr, "Cannot resize log buffer to size %zu, exceeding maximum allowed %u",
                    newSize, (unsigned)COMMUTIL_MAX_BUFFER_SIZE);
            return false;
        }
        // allocate a bit more so we avoid another realloc and copy if possible
        size_t actualNewSize = m_bufferSize;
        while (actualNewSize < newSize) {
            actualNewSize *= 2;
        }
        bool shouldCopy = (m_dynamicBuffer == nullptr);
        char* newBuffer = (char*)realloc(m_dynamicBuffer, actualNewSize);
        if (newBuffer == nullptr) {
            return false;
        }
        m_dynamicBuffer = newBuffer;
        if (shouldCopy) {
            commutil_strncpy(m_dynamicBuffer, m_fixedBuffer, m_bufferSize);
        }
        m_bufferSize = actualNewSize;
    }
    return true;
}

void LogBuffer::reset() {
    if (m_dynamicBuffer != nullptr) {
        free(m_dynamicBuffer);
        m_dynamicBuffer = nullptr;
    }
    m_bufferSize = COMMUTIL_LOG_BUFFER_SIZE;
}

bool LogBuffer::appendV(const char* fmt, va_list args) {
    if (m_bufferFull) {
        return false;
    }

    // NOTE: if buffer is too small then args is corrupt and cannot be reused, so we have no option
    // but prepare a copy in advance, even though mostly it will not be used
    va_list apCopy;
    va_copy(apCopy, args);
    size_t sizeLeft = size() - m_offset;
    int res = vsnprintf(getRef() + m_offset, sizeLeft, fmt, args);
    if (res < 0) {
        // output error occurred, report this, this is highly unexpected
        fprintf(stderr, "Failed to format message buffer\n");
        return false;
    }

    // now we can safely make the cast
    uint32_t res32 = (uint32_t)res;

    // NOTE: return value does not include the terminating null, and number of copied characters,
    // including the terminating null, will not exceed size, so if res==size it means size - 1
    // characters were copied and one more terminating null, meaning one character was lost.
    // if res > size if definitely means buffer was too small, and res shows the required size

    // NOTE: cast to int is safe (since size is limited to COMMUTIL_MAX_BUFFER_SIZE = 16KB)
    if (res32 < sizeLeft) {
        va_end(apCopy);
        m_offset += res32;
        return true;
    }

    // buffer too small
    if (!ensureBufferLength(res32)) {
        return false;
    }
    // this time we must succeed
    sizeLeft = size() - m_offset;
    res = vsnprintf(getRef() + m_offset, sizeLeft, fmt, apCopy);
    va_end(apCopy);

    // check again for error
    if (res < 0) {
        // output error occurred, report this, this is highly unexpected
        fprintf(stderr, "Failed to format message buffer (second time)\n");
        return false;
    }

    // now we can safely make the cast
    res32 = (uint32_t)res;

    // NOTE: cast to int is safe (since size is limited to COMMUTIL_MAX_BUFFER_SIZE = 16KB)
    if (res32 >= sizeLeft) {
        fprintf(stderr, "Failed to format string second time\n");
        return false;
    }
    m_offset += res32;
    return true;
}

bool LogBuffer::append(const char* msg, size_t len /* = 0 */) {
    if (m_bufferFull) {
        return false;
    }
    if (len == 0) {
        len = strlen(msg);
    }
    if (!ensureBufferLength(len)) {
        return false;
    }
    m_offset += commutil_strncpy(getRef() + m_offset, msg, size() - m_offset, len);
    return true;
}

}  // namespace commutil
