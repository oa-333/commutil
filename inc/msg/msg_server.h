#ifndef __MSG_SERVER_H__
#define __MSG_SERVER_H__

#include "comm_util_log.h"
#include "msg/msg_assembler.h"
#include "msg/msg_frame_reader.h"
#include "msg/msg_listener.h"
#include "msg/msg_multiplexer.h"
#include "transport/data_server.h"

namespace commutil {

/**
 * @brief Utility class for server-side messaging. Derive from it and implement the handleMsg()
 * virtual method. Utility macros defined in msg.h can be used as follows:
 *
 */
class COMMUTIL_API MsgServer : public MsgListener, public MsgFrameListener {
public:
    MsgServer() : m_dataServer(nullptr), m_nextSessionId(0) {}
    MsgServer(const MsgServer&) = delete;
    MsgServer(MsgServer&&) = delete;
    MsgServer& operator=(const MsgServer&) = delete;
    ~MsgServer() override {}

    /**
     * @brief Initializes the message server.
     * @param dataServer The transport layer's data server.
     * @param maxConnections The maximum number of connections the server can handle concurrently.
     * This holds true also for datagram server, in which case there is a limit to the number of
     * different servers sending datagrams to the server, along with some expiry control. @see
     * UdpServer for more information.
     * @param concurrency The level of concurrency to enforce. Determines the number of worker
     * threads.
     * @param bufferSize The buffer size used for each server connection I/O. Specify a buffer size
     * large enough to hold both incoming and outgoing messages, in order to avoid message
     * segmentation and reassembly at the application level.
     * @return The operation result.
     */
    virtual ErrorCode initialize(DataServer* dataServer, uint32_t maxConnections,
                                 uint32_t concurrency, uint32_t bufferSize);

    /** @brief Releases all resources allocated for recovery. */
    virtual ErrorCode terminate();

    /** @brief Starts the message server. */
    virtual ErrorCode start();

    /** @brief Stops the message server. */
    virtual ErrorCode stop();

    /**
     * @brief Notify incoming connected.
     * @param connectionDetails The connection details.
     * @return True if the connection is to be accepted.
     * @return False if the connection is to be rejected. Returning false will cause the connection
     * to be closed.
     */
    bool onConnect(const ConnectionDetails& connectionDetails, int status) override;

    /**
     * @brief Notify connection closed.
     * @param connectionDetails The connection details.
     */
    void onDisconnect(const ConnectionDetails& connectionDetails) override;

    /**
     * @brief Notify of an incoming message.
     * @note Default implementation unpacks the incoming message according to the framing protocol,
     * and dispatches contained message buffers according to @ref MsgFrameListener API. Sub-classes
     * can override this default behavior. Also note that handling duplicates due to resending by a
     * client is NOT being handled by the @ref MsgServer.
     * @param connectionDetails The connection details.
     * @param msg The message.
     * @param canSaveMsg Denotes whether the listener is allowed to keep a reference to the message.
     * If not, then the result value is ignored.
     * @return @ref MSG_CAN_DELETE if the message can be deleted, or rather @ref MSG_CANNOT_DELETE
     * if it is still being used.
     */
    MsgAction onMsg(const ConnectionDetails& connectionDetails, Msg* msg, bool canSaveMsg) override;

    /** @struct Base session. */
    struct Session {
        /** @brief Unique session id. */
        uint64_t m_sessionId;

        /** @brief Incoming unique connection id (used by connected client). */
        uint64_t m_connectionId;

        /** @brief Incoming connection index (used by connected client). */
        uint64_t m_connectionIndex;

        Session() : m_sessionId(0), m_connectionId(0), m_connectionIndex(0) {}

        Session(uint64_t sessionId, const ConnectionDetails& connectionDetails)
            : m_sessionId(sessionId),
              m_connectionId(connectionDetails.getConnectionId()),
              m_connectionIndex(connectionDetails.getConnectionIndex()) {}

        virtual ~Session() {}
    };

protected:
    /**
     * @brief Override this factory method to create custom session object with additional fields.
     * @param sessionId The session's unique id.
     * @param connectionDetails The incoming connection details.
     * @return Session* The resulting session object or null if failed or request denied.
     */
    virtual Session* createSession(uint64_t sessionId, const ConnectionDetails& connectionDetails);

    /**
     * @brief Notify session disconnected event.
     * @param session The disconnecting session.
     */
    virtual void onDisconnectSession(Session* session, const ConnectionDetails& connectionDetails);

    /**
     * @brief Retrieves the session matching client connection details.
     * @param connectionDetails The client's connection details.
     * @param[out] session The resulting session
     * @return E_OK if operation succeeded successfully.
     * @return E_INVALID_CONNECTION If the connection details are corrupt.
     * @return E_SESSION_NOT_FOUND If no such session was found.
     * @return E_SESSION_STALE If the matching session was found, but some details are wrong.
     */
    ErrorCode getSession(const ConnectionDetails& connectionDetails, Session** session);

#if 0
    /**
     * @brief Handles a message. Subclasses are responsible for actual deserialization. The message
     * server provides only framing services.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param msgBuffer The message buffer. Valid only if status is zero.
     * @param bufferSize The buffer length. Valid only if status is zero.
     * @param lastInBatch Designates whether this is the last message within a message batch. In
     * case of a single message this is always true.
     * @param batchSize The number of messages in the message batch. In case of a single message
     * this is always 1.
     * @return True if message handling within a batch should continue, otherwise deriving
     * sub-classes should return false (e.g. irrecoverable deserialization error), in which case
     * @ref handleMsgError() is NOT called, and if some error status needs to be sent to the client,
     * then deriving sub-classes are responsible for that.
     */
    virtual bool handleMsg(const ConnectionDetails& connectionDetails, const MsgHeader& msgHeader,
                           const char* msgBuffer, uint32_t bufferSize, bool lastInBatch,
                           uint32_t batchSize) = 0;

    /**
     * @brief Handle errors during message unpacking.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param status Deserialization error status.
     */
    virtual void handleMsgError(const ConnectionDetails& connectionDetails,
                                const MsgHeader& msgHeader, int status) = 0;
#endif

    /**
     * @brief Sends a reply to a specific client. Use this API for server initiated communication
     * (e.g. push notifications).
     * @param connectionDetails The connection details of the recipient.
     * @param msg The message to send.
     * @return ErrorCode
     */
    ErrorCode replyMsg(const ConnectionDetails& connectionDetails, Msg* msg);

private:
    /** @var TCP server for managing incoming connections. */
    DataServer* m_dataServer;

    /** @var Helper message assembler. */
    MsgAssembler m_msgAssembler;

    /** @var Helper message multiplexer for concurrency management. */
    MsgMultiplexer m_msgMultiplexer;

    MsgFrameReader m_frameReader;

    /** @var Session vector. */
    std::vector<Session*> m_sessions;

    /** @var Unique session id generator. */
    std::atomic<uint64_t> m_nextSessionId;

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_SERVER_H__