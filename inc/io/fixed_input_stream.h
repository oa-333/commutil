#ifndef __FIXED_INPUT_STREAM_H__
#define __FIXED_INPUT_STREAM_H__

#include <vector>

#include "io/input_stream.h"

namespace commutil {

/** @brief Input stream over a given fixed buffer. */
class COMMUTIL_API FixedInputStream : public InputStream {
public:
    /**
     * @brief Construct a new fixed input stream object.
     * @param buffer The buffer over which the input stream is built.
     * @param bufferSize The buffer size.
     * @param byRef Optionally specifies whether a reference to the buffer should be used. If false,
     * then the buffer is copied.
     * @param byteOrder Optionally specifies whether the buffer data is usign big endian byte order.
     */
    FixedInputStream(const char* buffer, uint32_t bufferSize, bool byRef = true,
                     ByteOrder byteOrder = ByteOrder::HOST_ORDER);
    FixedInputStream(const FixedInputStream&) = delete;
    FixedInputStream(FixedInputStream&&) = delete;
    FixedInputStream& operator=(const FixedInputStream&) = delete;
    ~FixedInputStream() override {}

    /** @brief Retrieves the current offset of the stream. */
    inline uint32_t getOffset() const { return m_offset; }

    /** @brief Resets the input stream (drops all buffers). */
    void reset() final {}

    /** @brief Queries the stream size (how many bytes left to read). */
    uint32_t size() const final { return m_size - m_offset; }

    /**
     * @brief Peeks for a few bytes in the stream without pulling them.
     * @note If the stream is depleted, then E_END_OF_STREAM is returned, and bytesPeeked (if
     * supplied) contains the actual number of bytes that were peeked.
     * @param buffer Received the bytes peek from the stream.
     * @param length The amount of bytes to peek.
     * @param[out] bytesPeeked Optionally on return contains the number of bytes actually peeked.
     * @return ErrorCode The operation result.
     */
    ErrorCode peekBytes(char* buffer, uint32_t length, uint32_t* bytesPeeked = nullptr) override;

    /**
     * @brief Reads bytes from the stream.
     * @note If the stream is depleted, then E_END_OF_STREAM is returned, and bytesRead (if
     * supplied) contains the actual number of bytes that were read.
     * @param buffer Received the bytes read from the stream.
     * @param length The amount of bytes to read.
     * @param[out] bytesRead Optionally on return contains the number of bytes actually read.
     * @return ErrorCode The operation result.
     */
    ErrorCode readBytes(char* buffer, uint32_t length, uint32_t* bytesRead = nullptr) override;

    /**
     * @brief Skips the number of specified bytes in the stream.
     * @note If the stream is depleted, then E_END_OF_STREAM is returned, otherwise the number of
     * possible bytes are skipped and E_OK is returned.
     * @param length The amount of bytes to skip.
     * @param[out] bytesSkipped Optionally on return contains the number of bytes actually skipped.
     * @return ErrorCode The operation result.
     */
    ErrorCode skipBytes(uint32_t length, uint32_t* bytesSkipped = nullptr) override;

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
     */
    ErrorCode searchBytes(const char* pattern, uint32_t length) override;

    /** @brief Retrieves a reference to the underlying buffer. */
    inline const char* getBufRef() const { return m_bufRef ? m_bufRef : &m_buf[0]; }

private:
    std::vector<char> m_buf;
    const char* m_bufRef;
    uint32_t m_size;
    uint32_t m_offset;
};

}  // namespace commutil

#endif  // __FIXED_INPUT_STREAM_H__