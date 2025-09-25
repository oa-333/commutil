#ifndef __LOG_BUFFER_H__
#define __LOG_BUFFER_H__

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstring>

#include "comm_util_def.h"

namespace commutil {

/** @def The fixed buffer size used for logging. */
#define COMMUTIL_LOG_BUFFER_SIZE ((size_t)1024)

/** @def The maximum size allowed for a single log message buffer. */
#define COMMUTIL_MAX_BUFFER_SIZE ((size_t)(16 * 1024))

/**
 * @brief A fixed size buffer that may transition to dynamic size buffer. This is required by the
 * logger, which maintains a thread local buffer. Under MinGW this causes crash during application
 * shutdown, since for some reason, TLS destructors being run during DLL unload cannot deallocate
 * buffers when calling free. It is unclear whether there is a collision with the C runtime code.
 * Using a fixed buffer (so we avoid calling free() in buffer's destructor) is a bit limiting. So a
 * fixed buffer is used, but if more space is needed by some long log message, it transitions to a
 * dynamic buffer, and the logger is required to release it as soon as it finished logging.
 */
class LogBuffer {
public:
    /** @brief Constructor. */
    LogBuffer()
        : m_dynamicBuffer(nullptr),
          m_bufferSize(COMMUTIL_LOG_BUFFER_SIZE),
          m_offset(0),
          m_bufferFull(false) {}

    LogBuffer(const LogBuffer& buffer)
        : m_dynamicBuffer(nullptr),
          m_bufferSize(COMMUTIL_LOG_BUFFER_SIZE),
          m_offset(0),
          m_bufferFull(false) {
        assign(buffer.getRef(), buffer.getOffset());
    }

    /** @brief Destructor. */
    ~LogBuffer();

    /** @brief Returns a reference to the internal buffer. */
    inline char* getRef() { return m_dynamicBuffer ? m_dynamicBuffer : (char*)m_fixedBuffer; }

    /** @brief Returns a reference to the internal buffer. */
    inline const char* getRef() const {
        return m_dynamicBuffer ? m_dynamicBuffer : (const char*)m_fixedBuffer;
    }

    /** @brief Retrieves the current capacity of the buffer. */
    inline size_t size() const { return m_bufferSize; }

    /** @brief Retrieves the current offset of data stored in the buffer. */
    inline size_t getOffset() const { return m_offset; }

    /**
     * @brief Increases the current capacity of the buffer. If the buffer's size is already
     * greater than the required size then no action takes place.
     * @param newSize The required new size.
     * @return true If operation succeeded, otherwise false.
     */
    bool resize(size_t newSize);

    /** @brief Resets the buffer to original state. Releases the dynamic buffer if needed. */
    void reset();

    /** @brief Finalizes the log buffer. */
    inline void finalize() {
        if (m_bufferFull) {
            // put terminating null in case buffer got full
            getRef()[size() - 1] = 0;
        }
    }

    /** @brief Assigns a string value to the buffer. Discards previous contents. */
    inline bool assign(const char* msg, size_t len = 0) {
        if (len >= COMMUTIL_MAX_BUFFER_SIZE) {
            return false;
        }
        reset();
        return append(msg, len);
    }

    /** @brief Assigns a log buffer to another buffer. Discards previous contents. */
    inline bool assign(const LogBuffer& logBuffer) {
        return assign(logBuffer.getRef(), logBuffer.getOffset());
    }

    /** @brief Appends a formatted message to the log buffer. */
    bool appendV(const char* fmt, va_list ap);

    /** @brief Appends a string to the log buffer. */
    bool append(const char* msg, size_t len = 0);

    /** @brief Appends a char repeatedly to the log buffer. */
    inline bool append(size_t count, char c) {
        if (m_bufferFull) {
            return false;
        }
        if (!ensureBufferLength(count)) {
            return false;
        }
        memset(getRef() + m_offset, c, count);
        m_offset += count;
        return true;
    }

    /** @brief Ensures the log buffer has enough bytes. */
    inline bool ensureBufferLength(size_t requiredBytes) {
        bool res = true;
        if (size() - m_offset < requiredBytes) {
            res = resize(m_offset + requiredBytes);
            if (!res) {
                m_bufferFull = true;
            }
        }
        return res;
    }

    inline LogBuffer& operator=(const LogBuffer& buffer) {
        assign(buffer.getRef(), buffer.getOffset());
        return *this;
    }

private:
    char m_fixedBuffer[COMMUTIL_LOG_BUFFER_SIZE];
    char* m_dynamicBuffer;
    size_t m_bufferSize;
    size_t m_offset;
    bool m_bufferFull;
};

}  // namespace commutil

#endif  // __LOG_BUFFER_H__