#include "msg/msg_loggers.h"

#include "msg/msg.h"
#include "msg/msg_assembler.h"
#include "msg/msg_backlog.h"
#include "msg/msg_client.h"
#include "msg/msg_frame_reader.h"
#include "msg/msg_frame_writer.h"
#include "msg/msg_multiplexer.h"
#include "msg/msg_request.h"
#include "msg/msg_sender.h"
#include "msg/msg_server.h"

namespace commutil {

void registerMsgLoggers() {
    Msg::registerClassLogger();
    MsgAssembler::registerClassLogger();
    MsgBacklog::registerClassLogger();
    MsgClient::registerClassLogger();
    MsgFrameReader::registerClassLogger();
    MsgFrameWriter::registerClassLogger();
    MsgMultiplexer::registerClassLogger();
    MsgRequest::registerClassLogger();
    MsgSender::registerClassLogger();
    MsgServer::registerClassLogger();
}

void unregisterMsgLoggers() {
    Msg::unregisterClassLogger();
    MsgAssembler::unregisterClassLogger();
    MsgBacklog::unregisterClassLogger();
    MsgClient::unregisterClassLogger();
    MsgFrameReader::unregisterClassLogger();
    MsgFrameWriter::unregisterClassLogger();
    MsgMultiplexer::unregisterClassLogger();
    MsgRequest::unregisterClassLogger();
    MsgSender::unregisterClassLogger();
    MsgServer::unregisterClassLogger();
}

}  // namespace commutil