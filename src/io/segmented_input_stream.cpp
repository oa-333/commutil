#include "io/segmented_input_stream.h"

#include <string.h>

#include <cstdlib>

#include "commutil_common.h"
#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(SegmentedInputStream)

void SegmentedInputStream::reset() {
    while (m_head != nullptr) {
        BufferNode* node = m_head;
        m_head = m_head->m_next;
        m_bufferDeallocator->deallocateBuffer(node->m_buffer);
        delete node;
    }
    m_tail = nullptr;
    m_size = 0;
}

void SegmentedInputStream::appendBuffer(char* buffer, uint32_t length) {
    if (m_head == nullptr) {
        assert(m_tail == nullptr);
        m_head = allocBufferNode(buffer, length, nullptr);
        m_tail = m_head;
    } else {
        assert(m_head != nullptr);
        m_tail->m_next = allocBufferNode(buffer, length, nullptr);
        m_tail = m_tail->m_next;
    }
    m_size += length;
}

ErrorCode SegmentedInputStream::peekBytes(char* buffer, uint32_t length,
                                          uint32_t* bytesPeeked /* = nullptr*/) {
    // truncate length if needed
    length = std::min(m_size, length);

    // copy bytes while preserving segments and offset
    uint32_t bytesCopied = 0;
    BufferNode* itr = m_head;
    uint32_t offset = m_offset;
    uint32_t bytesToCopy = itr->m_length - offset;
    while (bytesCopied + bytesToCopy < length) {
        memcpy(buffer + bytesCopied, itr->m_buffer + offset, bytesToCopy);
        bytesCopied += bytesToCopy;
        itr = itr->m_next;
        offset = 0;
        bytesToCopy = itr->m_length;
    }

    // copy last part
    assert(itr != nullptr);
    assert(bytesCopied + bytesToCopy >= length);
    memcpy(buffer + bytesCopied, itr->m_buffer + offset, length - bytesCopied);
    if (bytesPeeked != nullptr) {
        *bytesPeeked = length;
    }
    return ErrorCode::E_OK;
}

ErrorCode SegmentedInputStream::readBytes(char* buffer, uint32_t length,
                                          uint32_t* bytesRead /* = nullptr */) {
    uint32_t bytesReadLocal = std::min(m_size, length);
    copyBytes(buffer, bytesReadLocal);
    if (bytesRead != nullptr) {
        *bytesRead = bytesReadLocal;
    }
    return ErrorCode::E_OK;
}

ErrorCode SegmentedInputStream::skipBytes(uint32_t length, uint32_t* bytesSkipped /* = nullptr */) {
    uint32_t bytesSkippedLocal = std::min(m_size, length);
    copyBytes(nullptr, bytesSkippedLocal);
    if (bytesSkipped != nullptr) {
        *bytesSkipped = bytesSkippedLocal;
    }
    return ErrorCode::E_OK;
}

ErrorCode SegmentedInputStream::searchBytes(const char* pattern, uint32_t length) {
    while (m_head != nullptr) {
        uint32_t bufLen = m_head->m_length;
        if (length > bufLen) {
            return ErrorCode::E_END_OF_STREAM;
        }

        int res = commutil_strnstr(m_head->m_buffer, bufLen, pattern, length);
        if (res >= 0) {
            if (res > 0) {
                ErrorCode rc = skipBytes((uint32_t)res);
                if (rc != ErrorCode::E_OK) {
                    LOG_ERROR("Failed to skip bytes after seeing enough bytes: %s",
                              errorCodeToString(rc));
                    return ErrorCode::E_INTERNAL_ERROR;
                }
            }
            return ErrorCode::E_OK;
        }

        // if we have a next segment then we need to search carefully within the borderline area
        // if next segment does not have enough chars we stop
        // otherwise (no next segment) we need to leave an appropriate suffix
        if (m_head->m_next == nullptr) {
            uint32_t offset = bufLen - length;
            for (uint32_t i = 0; i < length; ++i) {
                if (strncmp(m_head->m_buffer + offset, pattern, length - i) == 0) {
                    break;
                }
                ++offset;
            }

            // skip unused prefix
            ErrorCode rc = skipBytes(offset);
            if (rc != ErrorCode::E_OK) {
                LOG_ERROR("Failed to skip unused suffix: %s", errorCodeToString(rc));
                return ErrorCode::E_INTERNAL_ERROR;
            }
            return ErrorCode::E_END_OF_STREAM;
        }

        // search in borderline area
        uint32_t nextLen = m_head->m_next->m_length;
        uint32_t offset = bufLen - length;
        for (uint32_t i = 0; i < length; ++i) {
            // check if next buffer does not have enough bytes
            if (nextLen < i) {
                // shave off disqualified prefix thus far
                ErrorCode rc = skipBytes(offset);
                if (rc != ErrorCode::E_OK) {
                    LOG_ERROR("Failed to skip disqualified prefix: %s", errorCodeToString(rc));
                    return ErrorCode::E_INTERNAL_ERROR;
                }
                return ErrorCode::E_END_OF_STREAM;
            }

            // check for a match
            if (strncmp(m_head->m_buffer + offset, pattern, length - i) == 0 &&
                strncmp(m_head->m_next->m_buffer, pattern + length - i, i) == 0) {
                // match found, shave off unused prefix
                ErrorCode rc = skipBytes(offset);
                if (rc != ErrorCode::E_OK) {
                    LOG_ERROR("Failed to skip unused prefix: %s", errorCodeToString(rc));
                    return ErrorCode::E_INTERNAL_ERROR;
                }
                return ErrorCode::E_OK;
            }

            // otherwise continue search in borderline
            ++offset;
        }

        // move on to next buffer
        removeHead();
    }
    return ErrorCode::E_END_OF_STREAM;
}

void SegmentedInputStream::copyBytes(char* buffer, uint32_t length) {
    // NOTE: buffer could be null when skipping
    assert(length <= m_size);
    uint32_t bytesCopied = 0;
    uint32_t bytesToCopy = m_head->m_length - m_offset;
    while (bytesCopied + bytesToCopy < length) {
        if (buffer != nullptr) {
            memcpy(buffer + bytesCopied, m_head->m_buffer + m_offset, bytesToCopy);
        }
        bytesCopied += bytesToCopy;
        m_size -= bytesToCopy;
        m_offset += bytesToCopy;

        // free head node
        removeHead();
        bytesToCopy = m_head->m_length;
    }

    // copy last part
    assert(m_head != nullptr);
    assert(bytesCopied + bytesToCopy >= length);
    bytesToCopy = length - bytesCopied;
    if (buffer != nullptr) {
        memcpy(buffer + bytesCopied, m_head->m_buffer + m_offset, bytesToCopy);
    }
    m_size -= bytesToCopy;
    m_offset += bytesToCopy;

    // remove first buffer if used fully
    if (m_offset == m_head->m_length) {
        removeHead();
    }
}

void SegmentedInputStream::removeHead() {
    if (m_head != nullptr) {
        assert(m_head->m_buffer);
        m_bufferDeallocator->deallocateBuffer(m_head->m_buffer);
        BufferNode* node = m_head;
        m_head = m_head->m_next;
        delete node;
        if (m_head == nullptr) {
            m_tail = nullptr;
        }
        m_offset = 0;
    }
}

}  // namespace commutil
