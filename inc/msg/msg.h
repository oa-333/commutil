#ifndef __MSG_H__
#define __MSG_H__

#include <cstdint>

#include "comm_util_def.h"
#include "comm_util_log.h"
#include "io/serializable.h"
#include "msg/msg_header.h"

namespace commutil {

/**
 * @brief Meta message. This is a container for messages, with message boundary and message id. The
 * actual message in binary form is serialized within the binary message payload.
 */
class COMMUTIL_API Msg {
public:
    /** @brief Serializes the message header. */
    ErrorCode serialize(OutputStream& os) const;

    /** @brief Deserializes the message header. */
    ErrorCode deserialize(InputStream& is);

    inline const MsgHeader& getHeader() const { return m_header; }
    inline MsgHeader& modifyHeader() { return m_header; }

    inline const char* getPayload() const { return (char*)(this + 1); }
    inline char* modifyPayload() { return (char*)(this + 1); }
    inline uint32_t getPayloadSizeBytes() const {
        return m_header.getLength() - (uint32_t)sizeof(Msg);
    }

private:
    /** @brief Default constructor for deserialization. */
    Msg() {}

    /** @brief Constructor. */
    Msg(uint32_t length, uint16_t msgId, uint16_t flags, uint64_t requestId, uint32_t requestIndex)
        : m_header(length, msgId, flags, requestId, requestIndex) {}

    Msg(const Msg&) = delete;
    Msg(Msg&&) = delete;
    Msg& operator=(const Msg&) = delete;

    /** @brief Destructor. */
    ~Msg() {}

    /** @brief The message header, followed by payload. */
    MsgHeader m_header;

    // TODO: why not make these public static?
    // give special access to friend functions so that user cannot create Msg manually
    friend COMMUTIL_API Msg* allocMsg(uint16_t msgId, uint16_t flags, uint64_t requestId,
                                      uint32_t requestIndex, uint32_t payloadSize);
    friend COMMUTIL_API Msg* allocMsg(const MsgHeader& msgHeader);
    friend COMMUTIL_API void freeMsg(Msg* msg);

    DECLARE_CLASS_LOGGER(Msg)
};

// allocates a message
extern COMMUTIL_API Msg* allocMsg(uint16_t msgId, uint16_t flags, uint64_t requestId,
                                  uint32_t requestIndex, uint32_t payloadSize);

// allocates a message, copy header fields from given message
extern COMMUTIL_API Msg* allocMsg(const MsgHeader& msgHeader);

// deallocates a message
extern COMMUTIL_API void freeMsg(Msg* msg);

}  // namespace commutil

#endif  // __MSG_H__