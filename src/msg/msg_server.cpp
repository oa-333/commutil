#include "msg/msg_server.h"

#include <cinttypes>

#include "commutil_log_imp.h"
#include "io/fixed_output_stream.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgServer)

ErrorCode MsgServer::initialize(DataServer* dataServer, uint32_t maxConnections,
                                uint32_t concurrency, uint32_t bufferSize,
                                MsgFrameListener* frameListener,
                                MsgSessionListener* sessionListener /* = nullptr */,
                                MsgSessionFactory* sessionFactory /* = nullptr */) {
    ErrorCode rc = m_msgMultiplexer.initialize(&m_msgAssembler, this, maxConnections, concurrency);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize message multiplexer: %s", errorCodeToString(rc));
        return rc;
    }

    // NOTE: we must first initialize the data server so we can get its installed data allocator and
    // pass it to the message assembler. Currently there is no special allocator installed. If this
    // changes in the future, then following this initialization order we will not crash
    rc = dataServer->initialize(&m_msgAssembler, maxConnections, bufferSize);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize transport layer data server: %s", errorCodeToString(rc));
        m_msgAssembler.terminate();
        m_msgMultiplexer.terminate();
        return rc;
    }

    // now we can initialize the message assembler with the data server's allocator
    rc = m_msgAssembler.initialize(maxConnections, dataServer->getByteOrder(),
                                   dataServer->getDataAllocator(), &m_msgMultiplexer);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize message assembler: %s", errorCodeToString(rc));
        m_msgMultiplexer.terminate();
        return rc;
    }

    // save members
    m_dataServer = dataServer;
    m_sessions.resize(maxConnections, nullptr);
    m_frameReader.initialize(m_dataServer->getByteOrder(), frameListener);
    m_sessionListener = sessionListener;
    m_sessionFactory = sessionFactory;
    if (m_sessionFactory == nullptr) {
        m_sessionFactory = &m_defaultSessionFactory;
    }
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
    MsgSession* session = m_sessions[connectionIndex];
    if (session != nullptr) {
        LOG_WARN(
            "Connection %s refused: invalid connection index, attempt to access an already active "
            "session with connection id "
            "%" PRIu64,
            connectionDetails.toString(), session->getConnectionId());
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
    MsgSession* session = m_sessions[connectionIndex];
    if (session == nullptr) {
        LOG_WARN("Attempt to access inactive session during disconnect from %s",
                 connectionDetails.toString());
    } else {
        // allow listener to inspect session before moving on to session destruction
        if (m_sessionListener != nullptr) {
            m_sessionListener->onDisconnectSession(session, connectionDetails);
        }

        // delete session object and remove entry from session map
        m_sessionFactory->deleteMsgSession(session);
        m_sessions[connectionIndex] = nullptr;
    }

    LOG_TRACE("Client %s disconnected", connectionDetails.toString());
}

MsgAction MsgServer::onMsg(const ConnectionDetails& connectionDetails, Msg* msg, bool canSaveMsg) {
    // if message is compressed we would like to take care of it here
    // next we check for a batch
    // finally we call handle message for each message buffer
    (void)canSaveMsg;

    // first handle duplicates

    // unpack frame and dispatch to listener
    ErrorCode rc = m_frameReader.readMsgFrame(connectionDetails, msg);
    if (rc != ErrorCode::E_OK) {
        // allow server to denote message is duplicate without issuing error
        if (rc != ErrorCode::E_ALREADY_EXISTS) {
            LOG_ERROR("Failed to read message frame: %s", errorCodeToString(rc));
        } else {
            LOG_TRACE("Upper layer indicates duplicate message %" PRIu64,
                      msg->getHeader().getRequestId());
        }
    }

    // tell caller the message can be deleted
    return MsgAction::MSG_CAN_DELETE;
}

ErrorCode MsgServer::getSession(const ConnectionDetails& connectionDetails, MsgSession** session) {
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

    if ((*session)->getConnectionId() != connectionDetails.getConnectionId()) {
        // stale session without disconnect
        LOG_WARN("Possible stale session: attempting to access session %u with id %" PRIu64
                 " while session is associated with connection id %" PRIu64,
                 connectionIndex, connectionDetails.getConnectionId(),
                 (*session)->getConnectionId());
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

MsgSession* MsgServer::createSession(uint64_t sessionId,
                                     const ConnectionDetails& connectionDetails) {
    return m_sessionFactory->createMsgSession(sessionId, connectionDetails);
}

}  // namespace commutil
