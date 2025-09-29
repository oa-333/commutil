#include "msg/msg_session.h"

namespace commutil {

MsgSession* MsgSessionFactory::createMsgSession(uint64_t sessionId,
                                                const ConnectionDetails& connectionDetails) {
    return new (std::nothrow) MsgSession(sessionId, connectionDetails);
}

void MsgSessionFactory::deleteMsgSession(MsgSession* msgSession) { delete msgSession; }

}  // namespace commutil
