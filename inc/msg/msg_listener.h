#ifndef __MSG_LISTENER_H__
#define __MSG_LISTENER_H__

#include "msg/msg.h"
#include "transport/connection_details.h"

namespace commutil {

/** @brief Message action constants. */
enum class MsgAction : uint32_t {
    /** @var No specific action for the message is specified. */
    MSG_NO_ACTION,

    /** @var Specifies that the message can be deleted. */
    MSG_CAN_DELETE,

    /** @brief Specifies that the message CANNOT be delete. */
    MSG_CANNOT_DELETE
};

/** @brief Listener for incoming assembled messages. */
class COMMUTIL_API MsgListener {
public:
    virtual ~MsgListener() {}

    /**
     * @brief Notify incoming connection.
     * @param connectionDetails The connection details.
     * @param status The operation status. Zero means success. Any other value denotes an error.
     * @return True if the connection is to be accepted.
     * @return False if the connection is to be rejected. Returning false will cause the connection
     * to be closed.
     */
    virtual bool onConnect(const ConnectionDetails& connectionDetails, int status) = 0;

    /**
     * @brief Notify connection closed.
     * @param connectionDetails The connection details.
     */
    virtual void onDisconnect(const ConnectionDetails& connectionDetails) = 0;

    /**
     * @brief Notify of an incoming message.
     * @param connectionDetails The connection details.
     * @param msg The message.
     * @param canSaveMsg Denotes whether the listener is allowed to keep a reference to the message.
     * If not, then the result value is ignored.
     * @return @ref MSG_CAN_DELETE if the message can be deleted, or rather @ref MSG_CANNOT_DELETE
     * if it is still being used.
     */
    virtual MsgAction onMsg(const ConnectionDetails& connectionDetails, Msg* msg,
                            bool canSaveMsg) = 0;

protected:
    MsgListener() {}
    MsgListener(const MsgListener&) = delete;
    MsgListener(MsgListener&&) = delete;
    MsgListener& operator=(const MsgListener&) = delete;

    DECLARE_CLASS_LOGGER(Msg)
};

// in order to use the macros below, each message class must declare a public static member named
// ID, holding the message id.

/** @def Defines a message handler. */
#define COMM_DECLARE_MSG(MsgType) \
    void on##MsgType(uint32_t connectionIndex, uint64_t connectionId, MsgType* msg);

/** @def Implements a message handler. */
#define COMM_IMPLEMENT_MSG(Class, MsgType) \
    void Class::on##MsgType(uint32_t connectionIndex, uint64_t connectionId, MsgType* msg)

/** @def Ends a message handling declaration. */
#define COMM_DECLARE_UNHANDLED_MSG() \
    void onUnhandledMsg(uint32_t connectionIndex, uint64_t connectionId, Msg* msg);

/** @def Implements a message handler. */
#define COMM_IMPLEMENT_UNHANDLED_MSG(Class) \
    void Class::onUnhandledMsg(uint32_t connectionIndex, uint64_t connectionId, Msg* msg)

/** @def Begins a message handling implementation. */
#define COMM_BEGIN_IMPLEMENT_MSG_MAP()           \
    ErrorCode rc = ErrorCode::E_NOT_IMPLEMENTED; \
    bool shouldSendResponse = true;              \
    switch (msg->getHeader().getMsgId()) {
/** @def Dispatches a message to a message handler. */
#define COMM_ON_MSG(MsgType, MsgId)                         \
    case MsgId:                                             \
        rc = on##MsgType(connectionDetails, (MsgType*)msg); \
        break;

/** @def Dispatches a message to a message handler. */
#define COMM_ON_MSG_NO_RESPONSE(MsgType, MsgId)             \
    case MsgId:                                             \
        rc = on##MsgType(connectionDetails, (MsgType*)msg); \
        shouldSendResponse = false;                         \
        break;

/** @def Ends a message handling implementation. */
#define COMM_END_IMPLEMENT_MSG_MAP()                 \
    default:                                         \
        rc = onUnhandledMsg(connectionDetails, msg); \
        break;                                       \
        }

/** @def Let user code query whether response should be sent. */
#define COMM_MSG_REQUIRE_RESPONSE() shouldSendResponse

/** @def Let user code query message handler execution result. */
#define COMM_MSG_RESULT() rc

}  // namespace commutil

#endif  // __MSG_LISTENER_H__