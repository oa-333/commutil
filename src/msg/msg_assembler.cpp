#include "msg/msg_assembler.h"

#include <cinttypes>

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgAssembler)

ErrorCode MsgAssembler::initialize(uint32_t maxConnections, ByteOrder byteOrder,
                                   DataAllocator* dataAllocator, MsgListener* listener) {
    m_bufferDeallocator.setDataAllocator(dataAllocator);
    m_msgStreams = new (std::nothrow) MsgStream*[maxConnections];
    if (m_msgStreams == nullptr) {
        LOG_ERROR("Failed to allocate %u message streams for assembly, out of memory",
                  maxConnections);
        return ErrorCode::E_NOMEM;
    }
    for (uint32_t i = 0; i < maxConnections; ++i) {
        m_msgStreams[i] = new (std::nothrow) MsgStream(byteOrder, &m_bufferDeallocator);
        if (m_msgStreams[i] == nullptr) {
            LOG_ERROR("Failed to allocate message stream for assembly, out of memory");
            for (uint32_t j = 0; j < i; ++j) {
                delete m_msgStreams[j];
            }
            delete[] m_msgStreams;
            m_msgStreams = nullptr;
            return ErrorCode::E_NOMEM;
        }
    }
    m_streamCount = maxConnections;
    m_listener = listener;
    return ErrorCode::E_OK;
}

ErrorCode MsgAssembler::terminate() {
    if (m_msgStreams != nullptr) {
        for (uint32_t i = 0; i < m_streamCount; ++i) {
            if (m_msgStreams[i] != nullptr) {
                delete m_msgStreams[i];
                m_msgStreams[i] = nullptr;
            }
        }
        delete[] m_msgStreams;
        m_msgStreams = nullptr;
    }
    return ErrorCode::E_OK;
}

bool MsgAssembler::onConnect(const ConnectionDetails& connectionDetails, int status) {
    m_msgStreams[connectionDetails.getConnectionIndex()]->reset();
    return m_listener->onConnect(connectionDetails, status);
}

void MsgAssembler::onDisconnect(const ConnectionDetails& connectionDetails) {
    m_msgStreams[connectionDetails.getConnectionIndex()]->reset();
    m_listener->onDisconnect(connectionDetails);
}

void MsgAssembler::onReadError(const ConnectionDetails& connectionDetails, int status) {
    (void)connectionDetails;
    (void)status;
    // TODO: report statistics
}

void MsgAssembler::onWriteError(const ConnectionDetails& connectionDetails, int status) {
    (void)connectionDetails;
    (void)status;
    // TODO: report statistics
}

commutil::DataAction MsgAssembler::onBytesReceived(const ConnectionDetails& connectionDetails,
                                                   char* buffer, uint32_t length, bool isDatagram) {
    // TODO: isDatagram not used, we can avoid all assembly stuff, right?
    (void)isDatagram;
    LOG_DEBUG("%u bytes received from %s", length, connectionDetails.toString());

    // append buffer
    MsgStream* msgStream = m_msgStreams[connectionDetails.getConnectionIndex()];
    SegmentedInputStream& is = msgStream->m_is;
    is.appendBuffer(buffer, length);

    // now assemble messages until stuck
    bool continueAssembly = true;
    while (continueAssembly && !is.empty()) {
        switch (msgStream->m_state) {
            case MsgStream::MS_WAITING_HEADER:
                onWaitHeader(msgStream);
                break;

            case MsgStream::MS_WAITING_BODY:
                continueAssembly = onWaitBody(msgStream, connectionDetails);
                break;

            case MsgStream::MS_RESYNC:
                onResync(msgStream);
                break;

            default:
                LOG_ERROR("Invalid message stream state");
                msgStream->reset();
                break;
        }
    }

    // the message assembler deletes incoming buffers by itself
    return DataAction::DATA_CANNOT_DELETE;
}

void MsgAssembler::onBytesSent(const ConnectionDetails& connectionDetails, uint32_t length,
                               int status) {
    (void)connectionDetails;
    (void)length;
    (void)status;
    // TODO: report statistics
}

void MsgAssembler::onWaitHeader(MsgStream* msgStream) {
    // check if enough bytes have arrived
    SegmentedInputStream& is = msgStream->m_is;
    if (is.size() < sizeof(Msg)) {
        return;
    }

    // check if we can peek (should be able to succeed)
    ErrorCode rc = is.peek(msgStream->m_msgHeader);
    if (rc != ErrorCode::E_OK) {
        if (rc != ErrorCode::E_END_OF_STREAM) {
            LOG_TRACE("Failed to peek input stream: %s", errorCodeToString(rc));
        }
        msgStream->reset();
        return;
    }

    // convert header from network byte order if needed
    if (msgStream->m_is.isNetworkOrder()) {
        msgStream->m_msgHeader.fromNetOrder();
    }

    // debug print header
    LOG_DEBUG(
        "Peeked message header: { length = %u, msgId = %u, flags = %hu, "
        "requestId = %" PRIu64 ", requestIndex = %u }",
        msgStream->m_msgHeader.getLength(), msgStream->m_msgHeader.getMsgId(),
        msgStream->m_msgHeader.getFlags(), msgStream->m_msgHeader.getRequestId(),
        msgStream->m_msgHeader.getRequestIndex());

    // verify header is valid, and if so we are done
    if (msgStream->m_msgHeader.isValid()) {
        msgStream->m_state = MsgStream::MS_WAITING_BODY;
        return;
    }

    LOG_ERROR("Invalid message header, magic word mismatch, starting transport resync");
    rc = msgStream->m_is.skipBytes(sizeof(MsgHeader));
    if (rc != ErrorCode::E_OK) {
        // TODO: this is probably an internal error, we should be able to
        // order this connection to restart by closing it
        LOG_WARN("Failed to skip bytes after seeing corrupt message headers: %s",
                 errorCodeToString(rc));
        msgStream->reset();
    } else {
        msgStream->m_state = MsgStream::MS_RESYNC;
    }
}

bool MsgAssembler::onWaitBody(MsgStream* msgStream, const ConnectionDetails& connectionDetails) {
    // check if enough bytes have arrived
    SegmentedInputStream& is = msgStream->m_is;
    if (is.size() < msgStream->m_msgHeader.getLength()) {
        // bail out of main loop until more bytes arrive
        return false;
    }

    LOG_DEBUG("Enough bytes are present to deserialize a message");
    Msg* msg = allocMsg(msgStream->m_msgHeader);
    if (msg == nullptr) {
        LOG_ERROR("Failed to allocate message");
        ErrorCode rc = is.skipBytes(msgStream->m_msgHeader.getLength());
        if (rc != ErrorCode::E_OK) {
            LOG_WARN("Failed to skip bytes after failure to allocate a message: %s",
                     errorCodeToString(rc));
            msgStream->reset();
        }
        // bail out of main loop until more bytes arrive
        return false;
    }

    // deserialize message
    LOG_DEBUG("Allocated message buffer at %p", msg);
    ErrorCode rc = msg->deserialize(is);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to deserialize message: %s", errorCodeToString(rc));
        msgStream->reset();
        freeMsg(msg);
        // bail out of main loop until more bytes arrive
        return false;
    }

    // dispatch to user
    if (m_listener->onMsg(connectionDetails, msg, true) == MsgAction::MSG_CAN_DELETE) {
        LOG_DEBUG("Freeing message buffer at %p", msg);
        freeMsg(msg);
    }
    // otherwise user has chosen to keep the message and it will be released by user later

    // move to next message
    msgStream->m_state = MsgStream::MS_WAITING_HEADER;
    return true;
}

void MsgAssembler::onResync(MsgStream* msgStream) {
    // check if enough bytes have arrived
    SegmentedInputStream& is = msgStream->m_is;
    if (is.size() < COMMUTIL_MSG_MAGIC_SIZE) {
        return;
    }

    // search for the pattern within the stream
    ErrorCode rc = is.searchBytes(COMMUTIL_MSG_MAGIC, COMMUTIL_MSG_MAGIC_SIZE);
    if (rc == ErrorCode::E_END_OF_STREAM) {
        return;
    }

    // check for transport layer error
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR(
            "Failed to search for magic word in the message stream, transport layer error: %s",
            errorCodeToString(rc));
        msgStream->reset();
        return;
    }

    // that's it, we are synced, now we can wait for header to arrive
    msgStream->m_state = MsgStream::MS_WAITING_HEADER;
}

}  // namespace commutil
