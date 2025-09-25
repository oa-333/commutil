#include "msg/msg_header.h"

namespace commutil {

const char COMMUTIL_MSG_MAGIC[] = {'C', 'O', 'M', 'U', 'M', 'E', 'S', 'G'};

#if (COMMUTIL_CPP_VER >= 201703L)
static_assert(sizeof(COMMUTIL_MSG_MAGIC) == COMMUTIL_MSG_MAGIC_SIZE);
#endif

MsgHeader::MsgHeader()
    : m_length(sizeof(MsgHeader)),
      m_msgId(0),
      m_flags(0),
      m_requestId(0),
      m_requestIndex(0),
      m_uncompressedLength(0),
      m_batchSize(1),
      m_reserved(0) {
    memcpy(&m_magic, COMMUTIL_MSG_MAGIC, COMMUTIL_MSG_MAGIC_SIZE);
}

/** @brief Constructor. */
MsgHeader::MsgHeader(uint32_t length, uint16_t msgId, uint16_t flags, uint64_t requestId,
                     uint32_t requestIndex)
    : m_length(length),
      m_msgId(msgId),
      m_flags(flags),
      m_requestId(requestId),
      m_requestIndex(requestIndex),
      m_uncompressedLength(0),
      m_batchSize(1),
      m_reserved(0) {
    memcpy(&m_magic, COMMUTIL_MSG_MAGIC, COMMUTIL_MSG_MAGIC_SIZE);
}

ErrorCode MsgHeader::serialize(OutputStream& os) const {
    COMM_SERIALIZE_DATA(os, m_magic);
    COMM_SERIALIZE_INT32(os, m_length);
    COMM_SERIALIZE_INT16(os, m_msgId);
    COMM_SERIALIZE_INT16(os, m_flags);
    COMM_SERIALIZE_INT64(os, m_requestId);
    COMM_SERIALIZE_INT32(os, m_requestIndex);
    COMM_SERIALIZE_INT32(os, m_uncompressedLength);
    COMM_SERIALIZE_INT32(os, m_batchSize);
    COMM_SERIALIZE_INT32(os, m_reserved);
    return ErrorCode::E_OK;
}

ErrorCode MsgHeader::deserialize(InputStream& is) {
    COMM_DESERIALIZE_DATA(is, m_magic);
    COMM_DESERIALIZE_INT32(is, m_length);
    COMM_DESERIALIZE_INT16(is, m_msgId);
    COMM_DESERIALIZE_INT16(is, m_flags);
    COMM_DESERIALIZE_INT64(is, m_requestId);
    COMM_DESERIALIZE_INT32(is, m_requestIndex);
    COMM_DESERIALIZE_INT32(is, m_uncompressedLength);
    COMM_DESERIALIZE_INT32(is, m_batchSize);
    COMM_DESERIALIZE_INT32(is, m_reserved);
    return ErrorCode::E_OK;
}

void MsgHeader::fromNetOrder() {
    // m_magic = ntohll(m_magic);
    m_length = ntohl(m_length);
    m_msgId = ntohs(m_msgId);
    m_flags = ntohs(m_flags);
    m_requestId = ntohll(m_requestId);
    m_requestIndex = ntohl(m_requestIndex);
    m_uncompressedLength = ntohl(m_uncompressedLength);
    m_batchSize = ntohl(m_batchSize);
    m_reserved = ntohl(m_reserved);
}

bool MsgHeader::isValid() const {
    return memcmp(&m_magic, COMMUTIL_MSG_MAGIC, COMMUTIL_MSG_MAGIC_SIZE) == 0;
}

}  // namespace commutil
