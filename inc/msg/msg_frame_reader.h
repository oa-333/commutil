#ifndef __MSG_FRAME_READER_H__
#define __MSG_FRAME_READER_H__

#include "msg/msg.h"
#include "msg/msg_frame_listener.h"
#include "msg/msg_stat_listener.h"

namespace commutil {

class COMMUTIL_API MsgFrameReader {
public:
    MsgFrameReader()
        : m_byteOrder(ByteOrder::HOST_ORDER), m_statListener(nullptr), m_frameListener(nullptr) {}
    MsgFrameReader(const MsgFrameReader&) = delete;
    MsgFrameReader(MsgFrameReader&&) = delete;
    MsgFrameReader& operator=(const MsgFrameReader&) = delete;
    ~MsgFrameReader() {}

    /**
     * @brief Initializes the message frame reader.
     * @param byteOrder The byte order to use (derived from the communication channel).
     * @param listener The listener that receives the raw message buffers packed within the message
     * frame.
     */
    inline void initialize(ByteOrder byteOrder, MsgStatListener* statListener,
                           MsgFrameListener* frameListener) {
        m_byteOrder = byteOrder;
        m_statListener = statListener;
        m_frameListener = frameListener;
    }

    /**
     * @brief Reads a message frame, possibly compressed or containing a message batch.
     * @param connectionDetails The details of the connection from which the message frame arrived.
     * @param msg The message frame to unpack.
     * @return The operation result.
     */
    ErrorCode readMsgFrame(const ConnectionDetails& connectionDetails, Msg* msg);

private:
    ByteOrder m_byteOrder;
    MsgStatListener* m_statListener;
    MsgFrameListener* m_frameListener;

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_FRAME_READER_H__