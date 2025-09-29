#ifndef __MSG_SESSION_H__
#define __MSG_SESSION_H__

#include "comm_util_def.h"
#include "transport/connection_details.h"

namespace commutil {

/**
 * @class Base session.
 * @note The  message session cannot be directly created or deleted, only through the session
 * factory or by subclasses. Make sure that derived session classes also control ctor/dtor access to
 * avoid inadvertent session deletion.
 */
class COMMUTIL_API MsgSession {
public:
    inline uint64_t getSessionId() const { return m_sessionId; }
    inline uint64_t getConnectionId() const { return m_connectionId; }
    inline uint64_t getConnectionIndex() const { return m_connectionIndex; }

protected:
    MsgSession() : m_sessionId(0), m_connectionId(0), m_connectionIndex(0) {}
    MsgSession(const MsgSession&) = default;
    MsgSession(MsgSession&&) = delete;
    MsgSession& operator=(const MsgSession&) = default;
    MsgSession(uint64_t sessionId, const ConnectionDetails& connectionDetails)
        : m_sessionId(sessionId),
          m_connectionId(connectionDetails.getConnectionId()),
          m_connectionIndex(connectionDetails.getConnectionIndex()) {}
    virtual ~MsgSession() {}

private:
    /** @brief Unique session id. */
    uint64_t m_sessionId;

    /** @brief Incoming unique connection id (used by connected client). */
    uint64_t m_connectionId;

    /** @brief Incoming connection index (used by connected client). */
    uint64_t m_connectionIndex;

    // give access to factory class
    friend class COMMUTIL_API MsgSessionFactory;
};

/** @brief A message session factory interface. */
class COMMUTIL_API MsgSessionFactory {
public:
    MsgSessionFactory() {}
    MsgSessionFactory(const MsgSessionFactory&) = delete;
    MsgSessionFactory(MsgSessionFactory&&) = delete;
    MsgSessionFactory& operator=(const MsgSessionFactory&) = delete;
    virtual ~MsgSessionFactory() {}

    /** @brief Creates a message session. */
    virtual MsgSession* createMsgSession(uint64_t sessionId,
                                         const ConnectionDetails& connectionDetails);

    /**
     * @brief Deletes a message session. Ensure session is deleted within the module that allocated
     * it.
     */
    virtual void deleteMsgSession(MsgSession* msgSession);
};

/** @brief A message session listener interface. */
class COMMUTIL_API MsgSessionListener {
public:
    virtual ~MsgSessionListener() {}

    /**
     * @brief Notify session connected event.
     * @param session The connecting session.
     * @param connectionDetails The connection details.
     */
    virtual void onConnectSession(MsgSession* session,
                                  const ConnectionDetails& connectionDetails) = 0;

    /**
     * @brief Notify session disconnected event.
     * @param session The disconnecting session.
     * @param connectionDetails The connection details.
     */
    virtual void onDisconnectSession(MsgSession* session,
                                     const ConnectionDetails& connectionDetails) = 0;

protected:
    MsgSessionListener() {}
    MsgSessionListener(const MsgSessionListener&) = delete;
    MsgSessionListener(MsgSessionListener&&) = delete;
    MsgSessionListener& operator=(const MsgSessionListener&) = delete;
};

}  // namespace commutil

#endif  // __MSG_SESSION_H__