#ifndef __OUTPUT_STREAM_H__
#define __OUTPUT_STREAM_H__

#include <cstdint>

#include "comm_util_def.h"
#include "comm_util_err.h"
#include "io/io_def.h"

namespace commutil {

/** @brief Base abstract class for output stream objects. */
class COMMUTIL_API OutputStream {
public:
    /**
     * @brief Writes a value.
     * @tparam T The value type.
     * @param value The value to write.
     * @return true If enough bytes were present to read the value.
     * @return false If not enough bytes were present to read the value.
     */
    template <typename T>
    inline ErrorCode write(const T& value) {
        return writeBytes((const char*)&value, sizeof(T));
    }

    /**
     * @brief Writes a buffer to the output stream.
     * @param buffer The buffer to write.
     * @param length The length of the buffer.
     * @return ErrorCode The operation result.
     */
    virtual ErrorCode writeBytes(const char* buffer, uint32_t length) = 0;

    /** @brief Specifies whether bytes sent through this stream require big endian byte order. */
    inline ByteOrder getByteOrder() const { return m_byteOrder; }

    /** @brief Queries whether the output stream has big endian byte order. */
    inline bool isNetworkOrder() const { return m_byteOrder == ByteOrder::NETWORK_ORDER; }

    /** @brief Queries whether the output stream has little endian byte order. */
    inline bool isHostOrder() const { return m_byteOrder == ByteOrder::HOST_ORDER; }

protected:
    OutputStream(ByteOrder byteOrder) : m_byteOrder(byteOrder) {}
    OutputStream(const OutputStream&) = delete;
    OutputStream(OutputStream&&) = delete;
    OutputStream& operator=(const OutputStream&) = delete;
    virtual ~OutputStream() {}

private:
    ByteOrder m_byteOrder;
};

}  // namespace commutil

#endif  // __OUTPUT_STREAM_H__