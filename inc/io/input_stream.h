#ifndef __INPUT_STREAM_H__
#define __INPUT_STREAM_H__

#include <cstdint>

#include "comm_util_def.h"
#include "comm_util_err.h"
#include "io/io_def.h"

namespace commutil {

/** @brief Base abstract class for input stream objects. */
class COMMUTIL_API InputStream {
public:
    /** @brief Resets the input stream (drops all buffers). */
    virtual void reset() = 0;

    /** @brief Queries the stream size.  */
    virtual uint32_t size() const = 0;

    /** @brief Queries whether the stream is empty. */
    inline bool empty() const { return size() == 0; }

    /** @brief Specifies wether bytes arriving from this stream have big endian byte order. */
    inline ByteOrder getByteOrder() const { return m_byteOrder; }

    /** @brief Queries whether the input stream has big endian byte order. */
    inline bool isNetworkOrder() const { return m_byteOrder == ByteOrder::NETWORK_ORDER; }

    /** @brief Queries whether the input stream has little endian byte order. */
    inline bool isHostOrder() const { return m_byteOrder == ByteOrder::HOST_ORDER; }

    /**
     * @brief Peeks for a few bytes in the stream without pulling them.
     * @note If the stream is depleted, then E_END_OF_STREAM is returned, and bytesPeeked (if
     * supplied) contains the actual number of bytes that were peeked.
     * @param buffer Received the bytes peek from the stream.
     * @param length The amount of bytes to peek.
     * @param[out] bytesPeeked Optionally on return contains the number of bytes actually peeked.
     * @return ErrorCode The operation result.
     */
    virtual ErrorCode peekBytes(char* buffer, uint32_t length, uint32_t* bytesPeeked = nullptr) = 0;

    /**
     * @brief Reads bytes from the stream.
     * @note If the stream is depleted, then E_END_OF_STREAM is returned, and bytesRead (if
     * supplied) contains the actual number of bytes that were read.
     * @param buffer Received the bytes read from the stream.
     * @param length The amount of bytes to read.
     * @param[out] bytesRead Optionally on return contains the number of bytes actually read.
     * @return ErrorCode The operation result.
     */
    virtual ErrorCode readBytes(char* buffer, uint32_t length, uint32_t* bytesRead = nullptr) = 0;

    /**
     * @brief Skips the number of specified bytes in the stream.
     * @note If the stream is depleted, then E_END_OF_STREAM is returned, and bytesSkipped (if
     * supplied) contains the actual number of bytes that were skipped.
     * @param length The amount of bytes to skip.
     * @param[out] bytesSkipped Optionally on return contains the number of bytes actually skipped.
     * @return ErrorCode The operation result.
     */
    virtual ErrorCode skipBytes(uint32_t length, uint32_t* bytesSkipped = nullptr) = 0;

    /**
     * @brief Search for a specific pattern in the stream, without big-endian conversions.
     * @note If the stream is depleted, then E_END_OF_STREAM is returned, otherwise the number of
     * required bytes are skipped and E_OK is returned. The stream is then positioned such that it
     * starts with the searched pattern.
     * @param pattern The byte pattern to search.
     * @param length The length of the pattern.
     * @return E_OK If the search succeeded.
     * @return E_END_OF_STREAM If the pattern was not found. An appropriate suffix is left in such a
     * case to enable continue searching.
     * @return ErrorCode Any other error by the input stream implementation.
     */
    virtual ErrorCode searchBytes(const char* pattern, uint32_t length) = 0;

    /**
     * @brief Peeks for a value.
     * @tparam T The value type.
     * @param[out] value The value to peek.
     * @return E_OK If the value was read successfully.
     * @return E_END_OF_STREAM If not enough bytes were present to read the value.
     * @return ErrorCode Any other error by the input stream implementation.
     */
    template <typename T>
    inline ErrorCode peek(T& value) {
        if (size() < sizeof(T)) {
            return ErrorCode::E_END_OF_STREAM;
        }
        uint32_t length = 0;
        ErrorCode rc = peekBytes((char*)&value, sizeof(T), &length);
        if (rc != ErrorCode::E_OK) {
            return rc;
        }
        if (length < sizeof(T)) {
            return ErrorCode::E_END_OF_STREAM;
        }
        return ErrorCode::E_OK;
    }

    /**
     * @brief Reads a value.
     * @tparam T The value type.
     * @param[out] value The value to read.
     * @return E_OK If the value was read successfully.
     * @return E_END_OF_STREAM If not enough bytes were present to read the value.
     * @return ErrorCode Any other error by the input stream implementation.
     */
    template <typename T>
    inline ErrorCode read(T& value) {
        if (size() < sizeof(T)) {
            return ErrorCode::E_END_OF_STREAM;
        }
        uint32_t length = 0;
        ErrorCode rc = readBytes(reinterpret_cast<char*>(&value), sizeof(T), &length);
        if (rc != ErrorCode::E_OK) {
            return rc;
        }
        if (length < sizeof(T)) {
            return ErrorCode::E_END_OF_STREAM;
        }
        return ErrorCode::E_OK;
    }

    /**
     * @brief Reads bytes until a condition is met.
     * @tparam F The bytes consumer function type.
     * @param f The function consuming bytes (one at a time). The function should return false to
     * stop consuming bytes.
     * @return ErrorCode The operation result.
     */
    template <typename F>
    inline ErrorCode readUntil(F f) {
        uint8_t byte = 0;
        do {
            uint32_t bytesRead = 0;
            ErrorCode rc = readBytes((char*)&byte, 1, &bytesRead);
            if (rc != ErrorCode::E_OK) {
                return rc;
            }
            if (bytesRead == 0) {
                return ErrorCode::E_END_OF_STREAM;
            }
        } while (f(byte));
        return ErrorCode::E_OK;
    }

protected:
    InputStream(ByteOrder byteOrder) : m_byteOrder(byteOrder) {}
    InputStream(const InputStream&) = delete;
    InputStream(InputStream&&) = delete;
    InputStream& operator=(const InputStream&) = delete;
    virtual ~InputStream() {}

private:
    ByteOrder m_byteOrder;
};

}  // namespace commutil

#endif  // __INPUT_STREAM_H__