#ifndef __MSG_HEADER_H__
#define __MSG_HEADER_H__

#include <cstdint>

#include "comm_util_def.h"
#include "comm_util_log.h"
#include "io/serializable.h"

// message flags

/** @def Flag denoting payload is compressed. */
#define COMMUTIL_MSG_FLAG_COMPRESSED ((uint16_t)0x0001)

/** @def Flag denoting payload has multiple messages. */
#define COMMUTIL_MSG_FLAG_BATCH ((uint16_t)0x0002)

/** @def Magic word message size. */
#define COMMUTIL_MSG_MAGIC_SIZE sizeof(uint64_t)

/** @def Invalid request id. */
#define COMMUTIL_MSG_INVALID_REQUEST_ID ((uint64_t)-1)

namespace commutil {

/** @brief Magic word for protocol resync. */
extern const char COMMUTIL_MSG_MAGIC[COMMUTIL_MSG_MAGIC_SIZE];

/** @brief Message header. */
class COMMUTIL_API MsgHeader {
public:
    /** @brief Default constructor for deserialization. */
    MsgHeader();

    /** @brief Constructor. */
    MsgHeader(uint32_t length, uint16_t msgId, uint16_t flags, uint64_t requestId,
              uint32_t requestIndex);

    MsgHeader(const MsgHeader&) = delete;
    MsgHeader(MsgHeader&&) = delete;
    MsgHeader& operator=(const MsgHeader&) = delete;

    /** @brief Destructor. */
    ~MsgHeader() {}

    /** @brief Serializes the message header. */
    ErrorCode serialize(OutputStream& os) const;

    /** @brief Deserializes the message header. */
    ErrorCode deserialize(InputStream& is);

    /** @brief Converts the flat message header from network to host byte order. */
    void fromNetOrder();

    /** @brief Checks whether the message is valid. */
    bool isValid() const;

    /** @brief Updates the message length in the message header. */
    inline void setLength(uint32_t length) { m_length = length; }
    inline void setUncompressedLength(uint32_t length) { m_uncompressedLength = length; }

    /** @brief Updates the request index of the message. */
    inline void setRequestIndex(uint32_t requestIndex) { m_requestIndex = requestIndex; }
    inline void setRequestId(uint64_t requestId) { m_requestId = requestId; }

    inline void setCompressed() { m_flags |= COMMUTIL_MSG_FLAG_COMPRESSED; }
    inline void setBatch() { m_flags |= COMMUTIL_MSG_FLAG_BATCH; }
    inline void setBatchSize(uint32_t batchSize) { m_batchSize = batchSize; }

    inline uint32_t getLength() const { return m_length; }
    inline uint32_t getUncompressedLength() const { return m_uncompressedLength; }
    inline uint16_t getMsgId() const { return m_msgId; }
    inline uint64_t getRequestId() const { return m_requestId; }
    inline uint32_t getRequestIndex() const { return m_requestIndex; }
    inline uint16_t getFlags() const { return m_flags; }

    inline bool isCompressed() const { return m_flags & COMMUTIL_MSG_FLAG_COMPRESSED; }
    inline bool isBatch() const { return m_flags & COMMUTIL_MSG_FLAG_BATCH; }
    inline uint32_t getBatchSize() const { return m_batchSize; }

private:
    /**
     * @var Magic word. Used for safety and for resyncing if message boundary lost due to broken
     * protocol issues (current value is COMUMESG in hexadecimal representation, which is 43 4F 4D
     * 55 4D 45 53 47).
     */
    char m_magic[COMMUTIL_MSG_MAGIC_SIZE];

    /**
     * @var The total message length, including header. Must be large enough to accommodate for
     * large payload. We do not expect more than 4GB per message (maximum size corresponds to system
     * block size, currently 64 MB). Subtracting header size from this value yields the payload
     * length.
     */
    uint32_t m_length;

    /** @var Message type identifier. */
    uint16_t m_msgId;

    /** @var Message flags. */
    uint16_t m_flags;

    /** @var The request identifier (unique per client). */
    uint64_t m_requestId;

    /** @var The request index. For client internal use. */
    uint32_t m_requestIndex;

    /** @brief The uncompressed length (for validation). */
    uint32_t m_uncompressedLength;

    /** @brief The number of messages contained in this message frame (for validation). */
    uint32_t m_batchSize;

    /** @var Reserved for future use. */
    uint32_t m_reserved;
};

}  // namespace commutil

#endif  // __MSG_HEADER_H__