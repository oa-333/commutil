#ifndef __MSG_WRITER_H__
#define __MSG_WRITER_H__

#include "comm_util_def.h"
#include "comm_util_err.h"

namespace commutil {

/**
 * @class Adapter for writing messages, possibly arriving from segmented buffer (save buffer
 * copies).
 */
class COMMUTIL_API MsgWriter {
public:
    virtual ~MsgWriter() {}

    /** @brief Retrieves the message payload size in bytes. */
    virtual uint32_t getPayloadSizeBytes() = 0;

    /** @brief Retrieves the number of messages in a message batch. */
    virtual uint32_t getBatchSize() = 0;

    /**
     * @brief Writes the message payload.
     * @param payload The payload buffer.
     * @return The operation's result.
     */
    virtual ErrorCode writeMsg(char* payload) = 0;

protected:
    MsgWriter() {}
    MsgWriter(const MsgWriter&) = delete;
    MsgWriter(MsgWriter&&) = delete;
    MsgWriter& operator=(const MsgWriter&) = delete;
};

}  // namespace commutil

#endif  // __MSG_WRITER_H__