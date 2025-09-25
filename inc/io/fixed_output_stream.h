#ifndef __FIXED_OUTPUT_STREAM_H__
#define __FIXED_OUTPUT_STREAM_H__

#include <cstring>
#include <vector>

#include "output_stream.h"

namespace commutil {

/** @brief Output stream over a given fixed buffer. */
class COMMUTIL_API FixedOutputStream : public OutputStream {
public:
    /**
     * @brief Construct a new fixed output stream object over internal buffer.
     * @param bufferSize The buffer size.
     * @param byteOrder Optionally specifies whether the buffer data should be usign big
     * endian byte order.
     */
    FixedOutputStream(uint32_t bufferSize, ByteOrder byteOrder)
        : OutputStream(byteOrder), m_bufferPtr(nullptr), m_bufferSize(bufferSize), m_offset(0) {
        m_buffer.resize(bufferSize);
        m_bufferPtr = &m_buffer[0];
    }

    /**
     * @brief Construct a new fixed output stream object over internal buffer.
     * @param bufferPtr An external buffer used to write streamed data.
     * @param bufferSize The buffer size.
     * @param byteOrder Optionally specifies whether the buffer data should be usign big
     * endian byte order.
     */
    FixedOutputStream(char* bufferPtr, uint32_t bufferSize, ByteOrder byteOrder)
        : OutputStream(byteOrder), m_bufferPtr(bufferPtr), m_bufferSize(bufferSize), m_offset(0) {}

    FixedOutputStream(const FixedOutputStream&) = delete;
    FixedOutputStream(FixedOutputStream&&) = delete;
    FixedOutputStream& operator=(const FixedOutputStream&) = delete;

    ~FixedOutputStream() override {}

    /**
     * @brief Writes a buffer to the output stream.
     * @param buffer The buffer to write.
     * @param length The length of the buffer.
     * @return ErrorCode The operation result.
     */
    ErrorCode writeBytes(const char* buffer, uint32_t length) final {
        if (length > m_bufferSize - m_offset) {
            return ErrorCode::E_NOMEM;
        }
        memcpy(m_bufferPtr + m_offset, buffer, length);
        m_offset += length;
        return ErrorCode::E_OK;
    }

    /** @brief Retrieves the underlying buffer. */
    inline const char* getBuffer() const { return m_bufferPtr; }

    /** @brief Retrieves the number of bytes written to the underlying buffer. */
    inline uint32_t getLength() const { return m_offset; }

private:
    std::vector<char> m_buffer;
    char* m_bufferPtr;
    uint32_t m_bufferSize;
    uint32_t m_offset;
};

}  // namespace commutil

#endif  // __FIXED_OUTPUT_STREAM_H__