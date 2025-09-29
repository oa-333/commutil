#ifndef __MSG_FRAME_READER_H__
#define __MSG_FRAME_READER_H__

#include "msg/msg.h"
#include "msg/msg_frame_listener.h"
#include "msg/msg_stat_listener.h"

namespace commutil {

class COMMUTIL_API MsgFrameReader {
public:
    MsgFrameReader()
        : m_byteOrder(ByteOrder::HOST_ORDER), m_frameListener(nullptr), m_statListener(nullptr) {}
    MsgFrameReader(const MsgFrameReader&) = delete;
    MsgFrameReader(MsgFrameReader&&) = delete;
    MsgFrameReader& operator=(const MsgFrameReader&) = delete;
    ~MsgFrameReader() {}

    /**
     * @brief Initializes the message frame reader.
     * @param byteOrder The byte order to use (derived from the communication channel).
     * @param frameListener The listener that receives the raw message buffers packed within the
     * message
     * @param statListener Optional statistics listener.
     * frame.
     */
    inline void initialize(ByteOrder byteOrder, MsgFrameListener* frameListener,
                           MsgStatListener* statListener = nullptr) {
        m_byteOrder = byteOrder;
        m_frameListener = frameListener;
        m_statListener = statListener;
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
    MsgFrameListener* m_frameListener;
    MsgStatListener* m_statListener;

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_FRAME_READER_H__