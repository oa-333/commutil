#ifndef __MSG_BATCH_WRITER_H__
#define __MSG_BATCH_WRITER_H__

#include "io/fixed_output_stream.h"
#include "msg/msg.h"
#include "msg/msg_def.h"

namespace commutil {

/** @brief Utility class for serializing message batches (all of the same type). */
class COMMUTIL_API MsgBatchWriter {
public:
    MsgBatchWriter(Msg* msg, ByteOrder byteOrder)
        : m_os(msg->modifyPayload(), msg->getPayloadSizeBytes(), byteOrder) {}
    MsgBatchWriter(char* buffer, uint32_t length, ByteOrder byteOrder)
        : m_os(buffer, length, byteOrder) {}
    MsgBatchWriter(const MsgBatchWriter&) = delete;
    MsgBatchWriter(MsgBatchWriter&&) = delete;
    MsgBatchWriter& operator=(const MsgBatchWriter&) = delete;
    ~MsgBatchWriter() {}

    /**
     * @brief Computes the required payload size for serializing a buffer array.
     * @param msgBufferArray The buffer array.
     * @return The computed size.
     */
    static uint32_t computePayloadSize(const MsgBufferArray& msgBufferArray);

    /** @brief Writes a full message batch into the meta-message payload. */
    ErrorCode writeBatch(const MsgBufferArray& msgBufferArray);

    /** @brief Starts batch serialization. */
    inline ErrorCode beginBatch(uint32_t batchSize) {
        COMM_SERIALIZE_INT32(m_os, batchSize);
        return ErrorCode::E_OK;
    }

    /** @brief Writes the next message in the batch. */
    inline ErrorCode writeMsg(const MsgBuffer& msgBuffer) {
        COMM_SERIALIZE_BUFFER(m_os, msgBuffer);
        return ErrorCode::E_OK;
    }

private:
    FixedOutputStream m_os;
};

}  // namespace commutil

#endif  // __MSG_BATCH_WRITER_H__