#ifndef __MSG_SERVER_H__
#define __MSG_SERVER_H__

#include "comm_util_log.h"
#include "msg/msg_assembler.h"
#include "msg/msg_frame_reader.h"
#include "msg/msg_listener.h"
#include "msg/msg_multiplexer.h"
#include "msg/msg_session.h"
#include "transport/data_server.h"

namespace commutil {

/**
 * @brief Utility class for server-side messaging. Derive from it and implement the handleMsg()
 * virtual method. Utility macros defined in msg.h can be used as follows:
 *
 */
class COMMUTIL_API MsgServer : public MsgListener {
public:
    MsgServer()
        : m_dataServer(nullptr),
          m_sessionListener(nullptr),
          m_sessionFactory(nullptr),
          m_nextSessionId(0) {}
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
     * @param frameListener The frame listener that is used to handle messages within a frame.
     * @param sessionListener Optional session listener.
     * @param sessionFactory Optional session factory.
     * @return The operation result.
     */
    virtual ErrorCode initialize(DataServer* dataServer, uint32_t maxConnections,
                                 uint32_t concurrency, uint32_t bufferSize,
                                 MsgFrameListener* frameListener,
                                 MsgSessionListener* sessionListener = nullptr,
                                 MsgSessionFactory* sessionFactory = nullptr);

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

    /**
     * @brief Sends a reply to a specific client. Use this API for server initiated communication
     * (e.g. push notifications).
     * @param connectionDetails The connection details of the recipient.
     * @param msg The message to send.
     * @return ErrorCode
     */
    ErrorCode replyMsg(const ConnectionDetails& connectionDetails, Msg* msg);

    /**
     * @brief Retrieves the session matching client connection details.
     * @param connectionDetails The client's connection details.
     * @param[out] session The resulting session
     * @return E_OK if operation succeeded successfully.
     * @return E_INVALID_CONNECTION If the connection details are corrupt.
     * @return E_SESSION_NOT_FOUND If no such session was found.
     * @return E_SESSION_STALE If the matching session was found, but some details are wrong.
     */
    ErrorCode getSession(const ConnectionDetails& connectionDetails, MsgSession** session);

private:
    /** @var TCP server for managing incoming connections. */
    DataServer* m_dataServer;

    /** @var Helper message assembler. */
    MsgAssembler m_msgAssembler;

    /** @var Helper message multiplexer for concurrency management. */
    MsgMultiplexer m_msgMultiplexer;

    /** @var The frame reader. */
    MsgFrameReader m_frameReader;

    /** @var Session listener */
    MsgSessionListener* m_sessionListener;

    /** @var Session factory/ */
    MsgSessionFactory* m_sessionFactory;
    MsgSessionFactory m_defaultSessionFactory;

    /** @var Session vector. */
    std::vector<MsgSession*> m_sessions;

    /** @var Unique session id generator. */
    std::atomic<uint64_t> m_nextSessionId;

    /**
     * @brief Override this factory method to create custom session object with additional fields.
     * @param sessionId The session's unique id.
     * @param connectionDetails The incoming connection details.
     * @return Session* The resulting session object or null if failed or request denied.
     */
    MsgSession* createSession(uint64_t sessionId, const ConnectionDetails& connectionDetails);

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_SERVER_H__