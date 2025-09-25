#include "msg/msg_batch_writer.h"

namespace commutil {

uint32_t MsgBatchWriter::computePayloadSize(const MsgBufferArray& msgBufferArray) {
    uint32_t payloadSize = sizeof(uint32_t);
    for (const MsgBuffer& msgBuffer : msgBufferArray) {
        // each buffer is prepended by its size
        payloadSize += sizeof(uint32_t);
        payloadSize += (uint32_t)msgBuffer.size();
    }
    return payloadSize;
}

ErrorCode MsgBatchWriter::writeBatch(const MsgBufferArray& msgBufferArray) {
    COMM_SERIALIZE_INT32(m_os, (uint32_t)msgBufferArray.size());
    for (const MsgBuffer& msgBuffer : msgBufferArray) {
        COMM_SERIALIZE_BUFFER(m_os, msgBuffer);
    }
    return ErrorCode::E_OK;
}

}  // namespace commutil
