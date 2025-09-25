#ifndef __MSG_RESPONSE_HANDLER_H__
#define __MSG_RESPONSE_HANDLER_H__

#include <string>

#include "comm_util_def.h"
#include "comm_util_err.h"
#include "msg/msg.h"
#include "msg/msg_frame_listener.h"

namespace commutil {

/** @brief An assistant to carry out message client operations. */
class COMMUTIL_API MsgResponseHandler : public MsgFrameListener {
public:
    virtual ~MsgResponseHandler() {}
    MsgResponseHandler(const MsgResponseHandler&) = delete;
    MsgResponseHandler(MsgResponseHandler&&) = delete;
    MsgResponseHandler& operator=(const MsgResponseHandler&) = delete;

    /**
     * @brief Handles incoming message buffer. Subclasses are responsible for actual
     * deserialization. The messaging layer provides only framing services.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message frame.
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
     */
    ErrorCode handleMsg(const ConnectionDetails& connectionDetails, const MsgHeader& msgHeader,
                        const char* msgBuffer, uint32_t bufferSize, bool lastInBatch,
                        uint32_t batchSize) final;

    /**
     * @brief Handle errors during message unpacking.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param status Deserialization error status.
     */
    void handleMsgError(const ConnectionDetails& connectionDetails, const MsgHeader& msgHeader,
                        int status) final;

    /**
     * @brief Handles message response.
     * @param msgHeader The header of the incoming meta-message frame.
     * @param responseBuffer The response message buffer.
     * @param bufferSize The buffer length.
     * @param status Deserialization error status. Zero denotes success.
     * @return E_OK If the transaction was successful, otherwise an error code, in which case the
     * message will be stored in a backlog for future attempt to resend to the server.
     */
    virtual ErrorCode handleResponse(const MsgHeader& msgHeader, const char* responseBuffer,
                                     uint32_t bufferSize, int status) = 0;

protected:
    /**
     * @brief Construct a new assistant object.
     *
     * @param logTargetName The log target name (for error reporting purposes).
     * @param status Optionally specify the expected response status. By default it is HTTP 200 OK.
     */
    MsgResponseHandler(const char* logTargetName) : m_logTargetName(logTargetName) {}

private:
    std::string m_logTargetName;
};

}  // namespace commutil

#endif  // __MSG_RESPONSE_HANDLER_H__