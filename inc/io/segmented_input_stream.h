#ifndef __SEGMENTED_INPUT_STREAM_H__
#define __SEGMENTED_INPUT_STREAM_H__

#include <cassert>
#include <cstdint>
#include <vector>

#include "comm_util_log.h"
#include "input_stream.h"

namespace commutil {

/**
 * @brief Utility class for deserializing message from incoming buffers. On the writer side of the
 * stream, incoming buffers are appended to the segmented input stream, and on the reader side of
 * the stream, messages are being assembled. Buffers are appended to each other in a list, but are
 * presented to the reader as a continuous buffer.
 */
class COMMUTIL_API SegmentedInputStream : public InputStream {
public:
    /**
     * @brief Construct a new segmented input stream object.
     * @param byteOrder Specifies whether the buffer data is usign big endian byte order.
     * @param allocator Optional allocator to use for memory allocations.
     */
    SegmentedInputStream(ByteOrder byteOrder)
        : InputStream(byteOrder), m_head(nullptr), m_tail(nullptr), m_offset(0), m_size(0) {}

    SegmentedInputStream(const SegmentedInputStream&) = delete;
    SegmentedInputStream(SegmentedInputStream&&) = delete;
    SegmentedInputStream& operator=(const SegmentedInputStream&) = delete;

    /** @brief Destructor. */
    ~SegmentedInputStream() { reset(); }

    /** @brief Resets the input stream (drops all buffers). */
    void reset();

    /** @brief Queries the stream size.  */
    inline uint32_t size() const { return m_size; }

    /** @brief Queries whether the stream is empty. */
    inline bool empty() const { return size() == 0; }

    /**
     * @brief Appends a buffer to the stream.
     * @note The stream allocator is responsible for deallocating the buffer.
     * @param buffer The buffer to append.
     * @param length The buffer's length.
     */
    void appendBuffer(char* buffer, uint32_t length);

    /**
     * @brief Peeks for a few bytes in the stream without pulling them.
     * @note If the stream is depleted, then E_END_OF_STREAM is returned, and bytesRead (if
     * supplied) contains the actual number of bytes that were read.
     * @param buffer Received the bytes peek from the stream.
     * @param length The amount of bytes to peek.
     * @param[out] bytesPeeked Optionally on return contains the number of bytes actually peeked.
     * @return ErrorCode The operation result.
     */
    ErrorCode peekBytes(char* buffer, uint32_t length, uint32_t* bytesRead = nullptr) override;

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
     * @note If the stream is depleted, then E_END_OF_STREAM is returned, and bytesRead (if
     * supplied) contains the actual number of bytes that were read.
     * @param length The amount of bytes to skip.
     * @param[out] bytesSkipped Optionally on return contains the number of bytes actually skipped.
     * @return ErrorCode The operation result.
     */
    ErrorCode skipBytes(uint32_t length, uint32_t* bytesRead = nullptr) override;

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

private:
    /** @brief Single buffer node in the incoming buffer list. */
    struct BufferNode {
        char* m_buffer;
        uint32_t m_length;
        BufferNode* m_next;
        BufferNode() : m_buffer(nullptr), m_length(0), m_next(nullptr) {}
        BufferNode(const BufferNode&) = delete;
        BufferNode(char* buffer, uint32_t length, BufferNode* next)
            : m_buffer(buffer), m_length(length), m_next(next) {}
        ~BufferNode() {}
    };

    BufferNode* m_head;
    BufferNode* m_tail;
    uint32_t m_offset;
    uint32_t m_size;

    inline BufferNode* allocBufferNode(char* buffer, uint32_t length, BufferNode* next) {
        return new (std::nothrow) BufferNode(buffer, length, next);
    }

    void copyBytes(char* buffer, uint32_t length);

    void removeHead();

    DECLARE_CLASS_LOGGER(Io)
};

}  // namespace commutil

#endif  // __SEGMENTED_INPUT_STREAM_H__