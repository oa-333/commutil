#include "io/fixed_input_stream.h"

#include <cstring>

#include "commutil_common.h"

namespace commutil {

FixedInputStream::FixedInputStream(const char* buffer, uint32_t bufferSize, bool byRef /* = true */,
                                   ByteOrder byteOrder /* = ByteOrder::HOST_ORDER */)
    : InputStream(byteOrder), m_bufRef(byRef ? buffer : nullptr), m_size(bufferSize), m_offset(0) {
    if (!byRef) {
        m_buf.resize(bufferSize);
        m_bufRef = &m_buf[0];
    }
}

ErrorCode FixedInputStream::peekBytes(char* buffer, uint32_t length,
                                      uint32_t* bytesPeeked /* = nullptr */) {
    uint32_t offset = m_offset;
    ErrorCode rc = readBytes(buffer, length, bytesPeeked);
    m_offset = offset;
    return rc;
}

ErrorCode FixedInputStream::readBytes(char* buffer, uint32_t length,
                                      uint32_t* bytesRead /* = nullptr */) {
    uint32_t bytesCanRead = size();
    if (bytesCanRead == 0) {
        return ErrorCode::E_END_OF_STREAM;
    }
    uint32_t bytesReadLocal = std::min(length, bytesCanRead);
    memcpy(buffer, m_bufRef + m_offset, bytesReadLocal);
    m_offset += bytesReadLocal;
    if (bytesRead != nullptr) {
        *bytesRead = bytesReadLocal;
    }
    return ErrorCode::E_OK;
}

ErrorCode FixedInputStream::skipBytes(uint32_t length, uint32_t* bytesSkipped /* = nullptr */) {
    uint32_t bytesCanRead = size();
    if (bytesCanRead == 0) {
        return ErrorCode::E_END_OF_STREAM;
    }
    uint32_t bytesSkippedLocal = std::min(length, bytesCanRead);
    m_offset += bytesSkippedLocal;
    if (bytesSkipped != nullptr) {
        *bytesSkipped = bytesSkippedLocal;
    }
    return ErrorCode::E_OK;
}

ErrorCode FixedInputStream::searchBytes(const char* pattern, uint32_t length) {
    uint32_t bufLen = size();
    if (length > bufLen) {
        return ErrorCode::E_END_OF_STREAM;
    }

    int res = commutil_strnstr(m_bufRef, bufLen, pattern, length);
    if (res >= 0) {
        if (skipBytes((uint32_t)res) != ErrorCode::E_OK) {
            return ErrorCode::E_INTERNAL_ERROR;
        }
        return ErrorCode::E_OK;
    }

    // otherwise we leave the longest suffix that partially matches the pattern
    uint32_t offset = bufLen - length;
    for (uint32_t i = 0; i < length; ++i) {
        if (strncmp(m_bufRef + offset, pattern, length - i) == 0) {
            break;
        }
        ++offset;
    }

    if (skipBytes((uint32_t)res) != ErrorCode::E_OK) {
        return ErrorCode::E_INTERNAL_ERROR;
    }
    return ErrorCode::E_END_OF_STREAM;
}

}  // namespace commutil
