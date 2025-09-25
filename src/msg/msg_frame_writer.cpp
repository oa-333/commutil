#include "msg/msg_frame_writer.h"

#include <gzip/compress.hpp>
#include <vector>

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgFrameWriter)

ErrorCode MsgFrameWriter::prepareMsgFrame(Msg** msg, uint16_t msgId, const char* body, size_t len,
                                          bool compress /* = false */, uint16_t flags /* = 0 */) {
    // compress body if needed
    std::string compressedBody;
    uint32_t uncompressedLength = (uint32_t)len;
    if (compress) {
        gzip::Compressor comp(Z_BEST_COMPRESSION);
        comp.compress(compressedBody, body, len);
        body = compressedBody.c_str();
        len = compressedBody.size();
        flags |= COMMUTIL_MSG_FLAG_COMPRESSED;
    }

    // allocate message buffer
    *msg = allocMsg(msgId, flags, 0, 0, (uint32_t)len);
    if (*msg == nullptr) {
        LOG_ERROR("Failed to allocate message with payload size %zu, out of memory", len);
        return ErrorCode::E_NOMEM;
    }

    // copy payload
    memcpy((*msg)->modifyPayload(), body, len);
    if (compress) {
        (*msg)->modifyHeader().setUncompressedLength(uncompressedLength);
    }
    return ErrorCode::E_OK;
}

ErrorCode MsgFrameWriter::prepareMsgFrame(Msg** msg, uint16_t msgId, MsgWriter* msgWriter,
                                          bool compress /* = false */, uint16_t flags /* = 0 */) {
    // if compression is enabled then we have no choice but first to serialize into a temporary
    // buffer and then write into the payload buffer (because we cannot tell required buffer size
    // before compression takes place)
    if (compress) {
        // serialize message to buffer
        std::vector<char> buf(msgWriter->getPayloadSizeBytes(), 0);
        ErrorCode rc = msgWriter->writeMsg(&buf[0]);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to write message into buffer: %s", errorCodeToString(rc));
            return rc;
        }

        // compress serialized message
        std::string compressedBody;
        gzip::Compressor comp(Z_BEST_COMPRESSION);
        comp.compress(compressedBody, &buf[0], buf.size());

        // prepare message frame
        flags |= COMMUTIL_MSG_FLAG_COMPRESSED;
        *msg = allocMsg(msgId, flags, 0, 0, (uint32_t)compressedBody.size());
        if (*msg == nullptr) {
            LOG_ERROR("Failed to allocate message with compressed payload size %zu, out of memory",
                      compressedBody.size());
            return ErrorCode::E_NOMEM;
        }
        memcpy((*msg)->modifyPayload(), compressedBody.data(), compressedBody.size());
        (*msg)->modifyHeader().setUncompressedLength(msgWriter->getPayloadSizeBytes());
    } else {
        *msg = allocMsg(msgId, flags, 0, 0, msgWriter->getPayloadSizeBytes());
        if (*msg == nullptr) {
            LOG_ERROR("Failed to allocate message with payload %u, out of memory",
                      msgWriter->getPayloadSizeBytes());
            return ErrorCode::E_NOMEM;
        }

        // serialize directly into payload
        ErrorCode rc = msgWriter->writeMsg((*msg)->modifyPayload());
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to write message into buffer: %s", errorCodeToString(rc));
            freeMsg(*msg);
            return rc;
        }
    }

    (*msg)->modifyHeader().setBatchSize(msgWriter->getBatchSize());

    return ErrorCode::E_OK;
}

}  // namespace commutil