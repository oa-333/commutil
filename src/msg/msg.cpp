#include "msg/msg.h"

#include <new>

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(Msg)

ErrorCode Msg::serialize(OutputStream& os) const {
    ErrorCode rc = m_header.serialize(os);
    if (rc != ErrorCode::E_OK) {
        return rc;
    }
    return os.writeBytes(getPayload(), getPayloadSizeBytes());
}

ErrorCode Msg::deserialize(InputStream& is) {
    ErrorCode rc = m_header.deserialize(is);
    if (rc != ErrorCode::E_OK) {
        return rc;
    }
    return is.readBytes(modifyPayload(), getPayloadSizeBytes());
}

Msg* allocMsg(uint16_t msgId, uint16_t flags, uint64_t requestId, uint32_t requestIndex,
              uint32_t payloadSize) {
    uint32_t msgBufSize = (uint32_t)sizeof(Msg) + payloadSize;
    char* msgBuf = new (std::nothrow) char[msgBufSize];
    if (msgBuf == nullptr) {
        LOG_ERROR(
            "Failed to allocate message with size %zu bytes (payload %zu bytes), out of memory",
            msgBufSize, payloadSize);
        return nullptr;
    }

    return new (msgBuf) Msg(msgBufSize, msgId, flags, requestId, requestIndex);
}

Msg* allocMsg(const MsgHeader& msgHeader) {
    uint32_t payloadSizeBytes = msgHeader.getLength() - sizeof(MsgHeader);
    return allocMsg(msgHeader.getMsgId(), msgHeader.getFlags(), msgHeader.getRequestId(),
                    msgHeader.getRequestIndex(), payloadSizeBytes);
}

void freeMsg(Msg* msg) {
    if (msg != nullptr) {
        msg->~Msg();
        char* buf = (char*)msg;
        delete[] buf;
    }
}

}  // namespace commutil
