#include "msg/msg_response_handler.h"

namespace commutil {

ErrorCode MsgResponseHandler::handleMsg(const ConnectionDetails& connectionDetails,
                                        const MsgHeader& msgHeader, const char* msgBuffer,
                                        uint32_t bufferSize, bool lastInBatch, uint32_t batchSize) {
    (void)connectionDetails;
    (void)lastInBatch;
    (void)batchSize;
    return handleResponse(msgHeader, msgBuffer, bufferSize, 0);
}

void MsgResponseHandler::handleMsgError(const ConnectionDetails& connectionDetails,
                                        const MsgHeader& msgHeader, int status) {
    (void)connectionDetails;
    handleResponse(msgHeader, nullptr, 0, status);
}

}  // namespace commutil
