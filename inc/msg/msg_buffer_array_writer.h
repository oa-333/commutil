#ifndef __MSG_BUFFER_ARRAY_WRITER_H__
#define __MSG_BUFFER_ARRAY_WRITER_H__

#include "comm_util_def.h"
#include "comm_util_err.h"
#include "msg/msg.h"
#include "msg/msg_batch_writer.h"
#include "msg/msg_writer.h"

namespace commutil {

/** @brief Helper class for writing a message batch. */
class MsgBufferArrayWriter : public MsgWriter {
public:
    MsgBufferArrayWriter(const MsgBufferArray& msgBufferArray, ByteOrder byteOrder)
        : m_msgBufferArray(msgBufferArray), m_byteOrder(byteOrder) {
        m_payloadSize = MsgBatchWriter::computePayloadSize(msgBufferArray);
    }
    MsgBufferArrayWriter() = delete;
    MsgBufferArrayWriter(const MsgBufferArrayWriter&) = delete;
    MsgBufferArrayWriter& operator=(const MsgBufferArrayWriter&) = delete;
    ~MsgBufferArrayWriter() override {}

    /** @brief Retrieves the message payload size in bytes. */
    uint32_t getPayloadSizeBytes() override { return m_payloadSize; }

    /** @brief Retrieves the number of messages in a message batch. */
    uint32_t getBatchSize() override { return (uint32_t)m_msgBufferArray.size(); }

    /**
     * @brief Writes the message payload.
     * @param payload The payload buffer.
     * @return The operation's result.
     */
    ErrorCode writeMsg(char* payload) override {
        MsgBatchWriter writer(payload, m_payloadSize, m_byteOrder);
        return writer.writeBatch(m_msgBufferArray);
    }

private:
    const MsgBufferArray& m_msgBufferArray;
    ByteOrder m_byteOrder;
    uint32_t m_payloadSize;
};

}  // namespace commutil

#endif  // __MSG_BUFFER_ARRAY_WRITER_H__