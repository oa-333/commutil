#include "msg/msg_frame_reader.h"

#include <gzip/decompress.hpp>

#include "commutil_log_imp.h"
#include "msg/msg_batch_reader.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgFrameReader)

ErrorCode MsgFrameReader::readMsgFrame(const ConnectionDetails& connectionDetails, Msg* msg) {
    // decompress if needed
    const char* buffer = msg->getPayload();
    uint32_t bufferSize = msg->getPayloadSizeBytes();
    std::string decompressedPayload;
    if (msg->getHeader().isCompressed()) {
        // first uncompress message data buffer
        gzip::Decompressor decompressor;
        decompressor.decompress(decompressedPayload, buffer, bufferSize);
        if (decompressedPayload.size() != msg->getHeader().getUncompressedLength()) {
            LOG_ERROR(
                "Invalid message compression: expecting uncompressed size %u, instead got %zu",
                msg->getHeader().getUncompressedLength(), decompressedPayload.size());
            m_frameListener->handleMsgError(connectionDetails, msg->getHeader(),
                                            (int)ErrorCode::E_DATA_CORRUPT);
            return ErrorCode::E_DATA_CORRUPT;
        }

        // notify statistics
        if (m_statListener) {
            m_statListener->onRecvMsgStats(bufferSize, (uint32_t)decompressedPayload.size());
        }

        // update buffer
        buffer = decompressedPayload.data();
        bufferSize = (uint32_t)decompressedPayload.size();
    } else {
        if (m_statListener) {
            m_statListener->onRecvMsgStats(bufferSize, bufferSize);
        }
    }

    // take care of single message
    if (!msg->getHeader().isBatch()) {
        return m_frameListener->handleMsg(connectionDetails, msg->getHeader(), buffer, bufferSize,
                                          true, 1);
    }

    // take care of batch
    MsgBatchReader batchReader(buffer, bufferSize, m_byteOrder);
    uint32_t batchSize = 0;
    ErrorCode rc = batchReader.readBatchSize(batchSize);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to deserialize record batch size: %s\n", errorCodeToString(rc));
        m_frameListener->handleMsgError(connectionDetails, msg->getHeader(), (int)rc);
        return rc;
    }
    if (batchSize != msg->getHeader().getBatchSize()) {
        LOG_ERROR("Invalid batch size in message frame, expecting %u, instead got %u",
                  msg->getHeader().getBatchSize(), batchSize);
        m_frameListener->handleMsgError(connectionDetails, msg->getHeader(),
                                        (int)ErrorCode::E_DATA_CORRUPT);
        return ErrorCode::E_DATA_CORRUPT;
    }

    for (uint32_t i = 0; i < batchSize; ++i) {
        const char* msgBuffer = nullptr;
        uint32_t length = 0;
        rc = batchReader.readMsg(&msgBuffer, length);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to deserialize record in batch: %s\n", errorCodeToString(rc));
            m_frameListener->handleMsgError(connectionDetails, msg->getHeader(), (int)rc);
            break;
        }
        rc = m_frameListener->handleMsg(connectionDetails, msg->getHeader(), msgBuffer, length,
                                        i + 1 == batchSize, batchSize);
        if (rc != ErrorCode::E_OK) {
            if (rc != ErrorCode::E_ALREADY_EXISTS) {
                LOG_ERROR("Failed to handle record in batch: %s\n", errorCodeToString(rc));
            } else {
                LOG_TRACE("Upper layer indicates duplicate message %" PRIu64,
                          msg->getHeader().getRequestId());
            }
            break;
        }
    }

    return rc;
}

}  // namespace commutil