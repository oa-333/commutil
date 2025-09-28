#ifndef __MSG_BATCH_READER_H__
#define __MSG_BATCH_READER_H__

#include "io/fixed_input_stream.h"
#include "msg/msg.h"

namespace commutil {

/** @brief Utility class for deserializing message batches. */
class COMMUTIL_API MsgBatchReader {
public:
    MsgBatchReader(const Msg* msg, ByteOrder byteOrder)
        : m_is(msg->getPayload(), msg->getPayloadSizeBytes(), true, byteOrder) {}
    MsgBatchReader(const char* buffer, uint32_t length, ByteOrder byteOrder)
        : m_is(buffer, length, true, byteOrder) {}
    MsgBatchReader(const MsgBatchReader&) = delete;
    MsgBatchReader(MsgBatchReader&&) = delete;
    MsgBatchReader& operator=(const MsgBatchReader&) = delete;
    ~MsgBatchReader() {}

    /** @brief Retrieves the batch size. */
    inline ErrorCode readBatchSize(uint32_t& batchSize) {
        COMM_DESERIALIZE_UINT32(m_is, batchSize);
        return ErrorCode::E_OK;
    }

    /** @brief Reads a message from the batch. */
    inline ErrorCode readMsg(const char** msg, uint32_t& length) {
        COMM_DESERIALIZE_UINT32(m_is, length);
        // NOTE: getting pointer instead of copying
        *msg = m_is.getBufRef() + m_is.getOffset();
        return m_is.skipBytes(length);
    }

private:
    FixedInputStream m_is;
};

}  // namespace commutil

#endif  // __MSG_BATCH_READER_H__