#ifndef __MSG_STAT_LISTENER_H__
#define __MSG_STAT_LISTENER_H__

#include <cstdint>

#include "comm_util_def.h"

namespace commutil {

/** @brief Message statistics listener. */
class COMMUTIL_API MsgStatListener {
public:
    virtual ~MsgStatListener() {}

    /**
     * @brief Notifies on sent message statistics.
     * @param msgSizeBytes The payload size (not including framing protocol header).
     * @param compressedMsgSizeBytes The compressed pay load size, in case compression is used,
     * otherwise zero.
     * @param status Denotes send result status. Zero means success.
     */
    virtual void onSendMsgStats(uint32_t msgSizeBytes, uint32_t compressedMsgSizeBytes,
                                int status) = 0;

    /**
     * @brief Notifies on received message statistics.
     * @param msgSizeBytes The payload size (not including framing protocol header).
     * @param compressedMsgSizeBytes The compressed pay load size, in case compression is used,
     * otherwise zero.
     */
    virtual void onRecvMsgStats(uint32_t msgSizeBytes, uint32_t compressedMsgSizeBytes) = 0;

protected:
    MsgStatListener() {}
    MsgStatListener(const MsgStatListener&) = delete;
    MsgStatListener(MsgStatListener&&) = delete;
    MsgStatListener& operator=(const MsgStatListener&) = delete;
};

}  // namespace commutil

#endif  // __MSG_STAT_LISTENER_H__