#ifndef __MSG_FRAME_LISTENER_H__
#define __MSG_FRAME_LISTENER_H__

#include "msg/msg_header.h"
#include "transport/connection_details.h"

namespace commutil {

class COMMUTIL_API MsgFrameListener {
public:
    virtual ~MsgFrameListener() {}

    /**
     * @brief Handles a message buffer. Subclasses are responsible for actual deserialization. The
     * messaging layer provides only framing services.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param msgBuffer The message buffer.
     * @param bufferSize The buffer length.
     * @param lastInBatch Designates whether this is the last message within a message batch. In
     * case of a single message this is always true.
     * @param batchSize The number of messages in the message batch. In case of a single message
     * this is always 1.
     * @return Message handling result. If message handling within a batch should continue then
     * return @ref E_OK, otherwise deriving sub-classes should return error code (e.g. irrecoverable
     * deserialization error), in which case @ref handleMsgError() is NOT called, and if some error
     * status needs to be sent to the client, then deriving sub-classes are responsible for that.
     * Special meaning is given to return code ErrorCode::E_ALREADY_EXISTS which denotes that the
     * server detected a duplicate message (e.g. due to resend attempt by client). In this case
     * batch processing stops as in other errors.
     */
    virtual ErrorCode handleMsg(const ConnectionDetails& connectionDetails,
                                const MsgHeader& msgHeader, const char* msgBuffer,
                                uint32_t bufferSize, bool lastInBatch, uint32_t batchSize) = 0;

    /**
     * @brief Handle errors during message unpacking.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param status Deserialization error status.
     */
    virtual void handleMsgError(const ConnectionDetails& connectionDetails,
                                const MsgHeader& msgHeader, int status) = 0;

protected:
    MsgFrameListener() {}
    MsgFrameListener(const MsgFrameListener&) = delete;
    MsgFrameListener(MsgFrameListener&&) = delete;
    MsgFrameListener& operator=(const MsgFrameListener&) = delete;
};

}  // namespace commutil

#endif  // __MSG_FRAME_LISTENER_H__