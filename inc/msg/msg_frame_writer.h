#ifndef __MSG_FRAME_WRITER_H__
#define __MSG_FRAME_WRITER_H__

#include "io/io_def.h"
#include "msg/msg.h"
#include "msg/msg_def.h"
#include "msg/msg_writer.h"

namespace commutil {

class COMMUTIL_API MsgFrameWriter {
public:
    MsgFrameWriter() {}
    MsgFrameWriter(const MsgFrameWriter&) = delete;
    MsgFrameWriter(MsgFrameWriter&&) = delete;
    MsgFrameWriter& operator=(const MsgFrameWriter&) = delete;
    ~MsgFrameWriter() {}

    /**
     * @brief Prepares a message frame for sending through the underlying transport.
     * @param[out] msg The resulting message frame.
     * @param msgId The message id.
     * @param body The message's body.
     * @param len The message's length.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param flags Optional flags to be added to the message header.
     * @return The operation result.
     */
    static ErrorCode prepareMsgFrame(Msg** msg, uint16_t msgId, const char* body, size_t len,
                                     bool compress = false, uint16_t flags = 0);

    /**
     * @brief Prepares a message for sending through the underlying transport.
     * @param[out] msg The resulting message frame.
     * @param msgId The message id.
     * @param msgWriter The message payload writer.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param flags Optional flags to be added to the message header.
     * @return The operation result.
     */
    static ErrorCode prepareMsgFrame(Msg** msg, uint16_t msgId, MsgWriter* msgWriter,
                                     bool compress = false, uint16_t flags = 0);

    /**
     * @brief Prepares a message frame for a batch of serialized messages.
     * @param[out] msg The resulting message frame.
     * @param msgId The message id.
     * @param bufferArray The buffer array of serialized messages.
     * @param byteOrder The byte order used by the transport layer.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param flags Optional flags to be added to the message header.
     * @return The operation result.
     */
    static ErrorCode prepareMsgBatchFrame(Msg** msg, uint16_t msgId,
                                          const MsgBufferArray& bufferArray, ByteOrder byteOrder,
                                          bool compress = false, uint16_t flags = 0);

private:
    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_FRAME_WRITER_H__