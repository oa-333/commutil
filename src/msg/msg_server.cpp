#include "msg/msg_server.h"

#include <cinttypes>

#include "commutil_log_imp.h"
#include "io/fixed_output_stream.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgServer)

ErrorCode MsgServer::initialize(DataServer* dataServer, uint32_t maxConnections,
                                uint32_t concurrency, uint32_t bufferSize) {
    ErrorCode rc = m_msgMultiplexer.initialize(&m_msgAssembler, this, maxConnections, concurrency);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize message multiplexer: %s", errorCodeToString(rc));
        return rc;
    }

    rc = m_msgAssembler.initialize(maxConnections, dataServer->getByteOrder(), &m_msgMultiplexer);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize message assembler: %s", errorCodeToString(rc));
        m_msgMultiplexer.terminate();
        return rc;
    }
    rc = dataServer->initialize(&m_msgAssembler, maxConnections, bufferSize);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize transport layer data server: %s", errorCodeToString(rc));
        m_msgAssembler.terminate();
        m_msgMultiplexer.terminate();
        return rc;
    }
    m_dataServer = dataServer;
    m_sessions.resize(maxConnections, nullptr);
    m_frameReader.initialize(m_dataServer->getByteOrder(), nullptr, this);
    return ErrorCode::E_OK;
}

ErrorCode MsgServer::terminate() {
    ErrorCode rc = m_dataServer->terminate();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to terminate message server, transport layer error: %s",
                  errorCodeToString(rc));
        return rc;
    }

    rc = m_msgAssembler.terminate();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to terminate message server, message assembler error: %s",
                  errorCodeToString(rc));
        return rc;
    }

    rc = m_msgMultiplexer.terminate();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to terminate message server, message multiplexer error: %s",
                  errorCodeToString(rc));
        return rc;
    }
    return ErrorCode::E_OK;
}

ErrorCode MsgServer::start() {
    ErrorCode rc = m_msgMultiplexer.start();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to start message server, message multiplexer error: %s",
                  errorCodeToString(rc));
        return rc;
    }
    rc = m_dataServer->start();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to start message server, transport layer error: %s",
                  errorCodeToString(rc));
        m_msgMultiplexer.stop();
        return rc;
    }
    return ErrorCode::E_OK;
}

ErrorCode MsgServer::stop() {
    ErrorCode rc = m_dataServer->stop();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to stop message server, transport layer error: %s",
                  errorCodeToString(rc));
        return rc;
    }
    rc = m_msgMultiplexer.stop();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to stop message server, message multiplexer error: %s",
                  errorCodeToString(rc));
        return rc;
    }
    return ErrorCode::E_OK;
}

bool MsgServer::onConnect(const ConnectionDetails& connectionDetails, int status) {
    if (status != 0) {
        LOG_ERROR("Failed to accept incoming connection: %d", status);
        return false;
    }

    // add to active sessions
    // note: each connection id is unique, so there is no race over the sessions object
    // also the multiplexer guarantees to always pass the same connection id in the same thread
    uint64_t connectionIndex = connectionDetails.getConnectionIndex();
    Session* session = m_sessions[connectionIndex];
    if (session != nullptr) {
        LOG_WARN(
            "Connection %s refused: invalid connection index, attempt to access an already active "
            "session with connection id "
            "%" PRIu64,
            connectionDetails.toString(), session->m_connectionId);
        return false;
    }

    // create new session
    uint64_t sessionId = m_nextSessionId.fetch_add(1, std::memory_order_relaxed);
    session = createSession(sessionId, connectionDetails);
    if (session == nullptr) {
        LOG_ERROR("Connection %s refused: Failed to allocate session object (out of memory)",
                  connectionDetails.toString());
        return false;
    }
    m_sessions[connectionIndex] = session;

    // print connection info
    LOG_TRACE("Incoming client connection: %s", connectionDetails.toString());
    return true;
}

void MsgServer::onDisconnect(const ConnectionDetails& connectionDetails) {
    // get session object
    uint64_t connectionIndex = connectionDetails.getConnectionIndex();
    Session* session = m_sessions[connectionIndex];
    if (session == nullptr) {
        LOG_WARN("Attempt to access inactive session during disconnect from %s",
                 connectionDetails.toString());
    } else {
        // allow subclass to inspect session before moving on to session destruction
        onDisconnectSession(session, connectionDetails);

        // delete session object and remove entry from session map
        delete session;
        m_sessions[connectionIndex] = nullptr;
    }

    LOG_TRACE("Client %s disconnected", connectionDetails.toString());
}

MsgAction MsgServer::onMsg(const ConnectionDetails& connectionDetails, Msg* msg, bool canSaveMsg) {
    // if message is compressed we would like to take care of it here
    // next we check for a batch
    // finally we call handle message for each message buffer
    (void)canSaveMsg;

    ErrorCode rc = m_frameReader.readMsgFrame(connectionDetails, msg);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to read message frame: %s", errorCodeToString(rc));
    }

#if 0
    // decompress if needed
    const char* buffer = msg->getPayload();
    uint32_t bufferSize = msg->getPayloadSizeBytes();
    std::string decompressedPayload;
    if (msg->getHeader().isCompressed()) {
        // first uncompress message data buffer
        gzip::Decompressor decompressor;
        decompressor.decompress(decompressedPayload, buffer, bufferSize);
        buffer = decompressedPayload.data();
        bufferSize = (uint32_t)decompressedPayload.size();
    }

    // take care of batch if needed
    // otherwise
    if (msg->getHeader().isBatch()) {
        MsgBatchReader batchReader(buffer, bufferSize, m_dataServer->getByteOrder());
        uint32_t batchSize = 0;
        ErrorCode rc = batchReader.readBatchSize(batchSize);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to deserialize record batch size: %s\n", errorCodeToString(rc));
            handleMsgError(connectionDetails, msg->getHeader(), (int)rc);
        } else {
            for (uint32_t i = 0; i < batchSize; ++i) {
                const char* msgBuffer = nullptr;
                uint32_t length = 0;
                rc = batchReader.readMsg(&msgBuffer, length);
                if (rc != ErrorCode::E_OK) {
                    LOG_ERROR("Failed to deserialize record in batch: %s\n", errorCodeToString(rc));
                    handleMsgError(connectionDetails, msg->getHeader(), (int)rc);
                    break;
                }
                if (!handleMsg(connectionDetails, msg->getHeader(), msgBuffer, length,
                               i + 1 == batchSize, batchSize)) {
                    break;
                }
            }
        }
    } else {
        handleMsg(connectionDetails, msg->getHeader(), buffer, bufferSize, true, 1);
    }
#endif

    // tell caller the message can be deleted
    return MsgAction::MSG_CAN_DELETE;
}

MsgServer::Session* MsgServer::createSession(uint64_t sessionId,
                                             const ConnectionDetails& connectionDetails) {
    return new (std::nothrow) Session(sessionId, connectionDetails);
}

void MsgServer::onDisconnectSession(Session* session, const ConnectionDetails& connectionDetails) {
    (void)session;
    (void)connectionDetails;
}

ErrorCode MsgServer::getSession(const ConnectionDetails& connectionDetails, Session** session) {
    uint64_t connectionIndex = connectionDetails.getConnectionIndex();
    if (connectionIndex >= m_sessions.size()) {
        LOG_WARN("Attempt to access session with invalid connection index (out of range): %u",
                 connectionIndex);
        return ErrorCode::E_INVALID_CONNECTION;
    }

    *session = m_sessions[connectionIndex];
    if (*session == nullptr) {
        LOG_WARN("Attempt to access session with invalid connection index (no session present): %u",
                 connectionIndex);
        return ErrorCode::E_SESSION_NOT_FOUND;
    }

    if ((*session)->m_connectionId != connectionDetails.getConnectionId()) {
        // stale session without disconnect
        LOG_WARN("Possible stale session: attempting to access session %u with id %" PRIu64
                 " while session is associated with connection id %" PRIu64,
                 connectionIndex, connectionDetails.getConnectionId(), (*session)->m_connectionId);
        return ErrorCode::E_SESSION_STALE;
    }

    return ErrorCode::E_OK;
}

ErrorCode MsgServer::replyMsg(const ConnectionDetails& connectionDetails, Msg* msg) {
    uint32_t msgLength = msg->getHeader().getLength();
    char* buffer = new (std::nothrow) char[msgLength];
    if (buffer == nullptr) {
        LOG_ERROR("Failed to allocate %u bytes for reply message", msgLength);
        return ErrorCode::E_NOMEM;
    }
    FixedOutputStream os(buffer, msgLength, m_dataServer->getByteOrder());
    ErrorCode rc = msg->serialize(os);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to serialize response message: %s", errorCodeToString(rc));
        delete[] buffer;
        return rc;
    }

    rc = m_dataServer->replyMsg(connectionDetails, os.getBuffer(), os.getLength(), true, true);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to send response message: %s", errorCodeToString(rc));
        delete[] buffer;
        return rc;
    }
    // buffer deallocated by transport after write is finished
    return ErrorCode::E_OK;
}

}  // namespace commutil
