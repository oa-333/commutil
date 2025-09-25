#ifndef __SERIALIZABLE_H__
#define __SERIALIZABLE_H__

#include "input_stream.h"
#include "output_stream.h"

#ifdef COMMUTIL_WINDOWS
#include "winsock2.h"
#else
#include <endian.h>
#include <netinet/in.h>
#ifndef htons
#define htons htobe16
#endif
#ifndef htonl
#define htonl htobe32
#endif
#ifndef htonll
#define htonll htobe64
#endif
#ifndef ntohs
#define ntohs be16toh
#endif
#ifndef ntohl
#define ntohl be32toh
#endif
#ifndef ntohll
#define ntohll be64toh
#endif
#endif

namespace commutil {

/** @brief Serializable interface. */
class COMMUTIL_API Serializable {
public:
    virtual ~Serializable() {}

    /** @brief Serialize this object into an output stream. */
    virtual ErrorCode serialize(OutputStream& os) const = 0;

    /** @brief Deserialize this object from an input stream. */
    virtual ErrorCode deserialize(InputStream& is) = 0;

protected:
    Serializable() {}
    Serializable(const Serializable&) = delete;
    Serializable(Serializable&&) = delete;
    Serializable& operator=(const Serializable&) = delete;
};

/** @brief Serializes 1-byte integer. */
#define COMM_SERIALIZE_INT8(os, value)            \
    {                                             \
        commutil::ErrorCode rc = os.write(value); \
        if (rc != commutil::ErrorCode::E_OK) {    \
            return rc;                            \
        }                                         \
    }

/** @brief Serializes 2-byte integer. */
#define COMM_SERIALIZE_INT16(os, value)                                                \
    {                                                                                  \
        commutil::ErrorCode rc = os.write(os.isNetworkOrder() ? htons(value) : value); \
        if (rc != commutil::ErrorCode::E_OK) {                                         \
            return rc;                                                                 \
        }                                                                              \
    }

/** @brief Serializes 4-byte integer. */
#define COMM_SERIALIZE_INT32(os, value)                                                \
    {                                                                                  \
        commutil::ErrorCode rc = os.write(os.isNetworkOrder() ? htonl(value) : value); \
        if (rc != commutil::ErrorCode::E_OK) {                                         \
            return rc;                                                                 \
        }                                                                              \
    }

/** @brief Serializes 8-byte integer. */
#define COMM_SERIALIZE_INT64(os, value)                                                 \
    {                                                                                   \
        commutil::ErrorCode rc = os.write(os.isNetworkOrder() ? htonll(value) : value); \
        if (rc != commutil::ErrorCode::E_OK) {                                          \
            return rc;                                                                  \
        }                                                                               \
    }

/** @brief Serializes boolean value. */
#define COMM_SERIALIZE_BOOL(os, value)             \
    {                                              \
        uint8_t value8 = value ? 1 : 0;            \
        commutil::ErrorCode rc = os.write(value8); \
        if (rc != commutil::ErrorCode::E_OK) {     \
            return rc;                             \
        }                                          \
    }

/** @brief Serializes enumerated value. */
#define COMM_SERIALIZE_ENUM(os, value)                          \
    {                                                           \
        switch (sizeof(value)) {                                \
            case 1:                                             \
                COMM_SERIALIZE_INT8(os, (uint8_t)(value));      \
                break;                                          \
            case 2:                                             \
                COMM_SERIALIZE_INT16(os, (uint16_t)(value));    \
                break;                                          \
            case 4:                                             \
                COMM_SERIALIZE_INT32(os, (uint32_t)(value));    \
                break;                                          \
            case 8:                                             \
                COMM_SERIALIZE_INT64(os, (uint64_t)(value));    \
                break;                                          \
            default:                                            \
                return commutil::ErrorCode::E_INVALID_ARGUMENT; \
        }                                                       \
    }

// TODO: this does not take care of byte order
/** @brief Serializes generic data (e.g. flat struct. No byte ordering takes place). */
#define COMM_SERIALIZE_DATA(os, value)            \
    {                                             \
        commutil::ErrorCode rc = os.write(value); \
        if (rc != commutil::ErrorCode::E_OK) {    \
            return rc;                            \
        }                                         \
    }

/** @def Serializes a serializable object. */
#define COMM_SERIALIZE(os, value)                       \
    {                                                   \
        commutil::ErrorCode rc = (value).serialize(os); \
        if (rc != commutil::ErrorCode::E_OK) {          \
            return rc;                                  \
        }                                               \
    }

/** @def Serializes length-prepended std::string. */
#define COMM_SERIALIZE_STRING(os, value)                                       \
    {                                                                          \
        COMM_SERIALIZE_INT32(os, (uint32_t)value.length());                    \
        commutil::ErrorCode rc = os.writeBytes(value.c_str(), value.length()); \
        if (rc != commutil::ErrorCode::E_OK) {                                 \
            return rc;                                                         \
        }                                                                      \
    }

/** @def Serializes null-terminated string. */
#define COMM_SERIALIZE_NT_STRING(os, value, len)            \
    {                                                       \
        commutil::ErrorCode rc = os.writeBytes(value, len); \
        if (rc != commutil::ErrorCode::E_OK) {              \
            return rc;                                      \
        }                                                   \
        COMM_SERIALIZE_INT8(os, 0);                         \
    }

/** @def Serializes length-prepended std::vector<char>. */
#define COMM_SERIALIZE_BUFFER(os, buffer)                           \
    {                                                               \
        uint32_t length = (uint32_t)buffer.size();                  \
        COMM_SERIALIZE_INT32(os, length);                           \
        commutil::ErrorCode rc = os.writeBytes(&buffer[0], length); \
        if (rc != commutil::ErrorCode::E_OK) {                      \
            return rc;                                              \
        }                                                           \
    }

/** @brief Deserializes 1-byte integer value. */
#define COMM_DESERIALIZE_INT8(is, value)         \
    {                                            \
        commutil::ErrorCode rc = is.read(value); \
        if (rc != commutil::ErrorCode::E_OK) {   \
            return rc;                           \
        }                                        \
    }

/** @brief Deserializes 2-byte integer value. */
#define COMM_DESERIALIZE_INT16(is, value)        \
    {                                            \
        commutil::ErrorCode rc = is.read(value); \
        if (rc != commutil::ErrorCode::E_OK) {   \
            return rc;                           \
        }                                        \
        if (is.isNetworkOrder()) {               \
            value = ntohs(value);                \
        }                                        \
    }

/** @brief Deserializes 4-byte integer value. */
#define COMM_DESERIALIZE_INT32(is, value)        \
    {                                            \
        commutil::ErrorCode rc = is.read(value); \
        if (rc != commutil::ErrorCode::E_OK) {   \
            return rc;                           \
        }                                        \
        if (is.isNetworkOrder()) {               \
            value = ntohl(value);                \
        }                                        \
    }

/** @brief Deserializes 8-byte integer value. */
#define COMM_DESERIALIZE_INT64(is, value)        \
    {                                            \
        commutil::ErrorCode rc = is.read(value); \
        if (rc != commutil::ErrorCode::E_OK) {   \
            return rc;                           \
        }                                        \
        if (is.isNetworkOrder()) {               \
            value = ntohll(value);               \
        }                                        \
    }

/** @brief Deserializes boolean value. */
#define COMM_DESERIALIZE_BOOL(is, value)          \
    {                                             \
        uint8_t value8 = 0;                       \
        commutil::ErrorCode rc = is.read(value8); \
        if (rc != commutil::ErrorCode::E_OK) {    \
            return rc;                            \
        }                                         \
        value = (value8 != 0);                    \
    }

/** @brief Deserializes enumerated value. */
#define COMM_DESERIALIZE_ENUM(is, value)                        \
    {                                                           \
        switch (sizeof(value)) {                                \
            case 1:                                             \
                COMM_DESERIALIZE_INT8(is, (uint8_t&)(value));   \
                break;                                          \
            case 2:                                             \
                COMM_DESERIALIZE_INT16(is, (uint16_t&)(value)); \
                break;                                          \
            case 4:                                             \
                COMM_DESERIALIZE_INT32(is, (uint32_t&)(value)); \
                break;                                          \
            case 8:                                             \
                COMM_DESERIALIZE_INT64(is, (uint64_t&)(value)); \
                break;                                          \
            default:                                            \
                return commutil::ErrorCode::E_INVALID_ARGUMENT; \
        }                                                       \
    }

/** @brief Deserializes generic data (e.g. falt struct. No byte ordering takes place). */
#define COMM_DESERIALIZE_DATA(is, value)         \
    {                                            \
        commutil::ErrorCode rc = is.read(value); \
        if (rc != commutil::ErrorCode::E_OK) {   \
            return rc;                           \
        }                                        \
    }

/** @brief Deserializes serializable object. */
#define COMM_DESERIALIZE(is, value)                       \
    {                                                     \
        commutil::ErrorCode rc = (value).deserialize(is); \
        if (rc != commutil::ErrorCode::E_OK) {            \
            return rc;                                    \
        }                                                 \
    }

/** @brief Deserializes length-prepended string into std::string. */
#define COMM_DESERIALIZE_STRING(is, value)                                 \
    {                                                                      \
        uint32_t length = 0;                                               \
        COMM_DESERIALIZE_INT32(is, length);                                \
        std::vector<char> buf(length + 1, 0);                              \
        uint32_t bytesRead = 0;                                            \
        commutil::ErrorCode rc = is.readBytes(&buf[0], length, bytesRead); \
        if (rc != commutil::ErrorCode::E_OK) {                             \
            return rc;                                                     \
        }                                                                  \
        value = (const char*)&buf[0];                                      \
    }

/** @brief Deserializes null-terminated string into std::string. */
#define COMM_DESERIALIZE_NT_STRING(is, value)                          \
    {                                                                  \
        commutil::ErrorCode rc = is.readUntil([&value](uint8_t byte) { \
            if (byte == 0) {                                           \
                return false;                                          \
            }                                                          \
            value += (char)byte;                                       \
            return true;                                               \
        });                                                            \
        if (rc != commutil::ErrorCode::E_OK) {                         \
            return rc;                                                 \
        }                                                              \
    }

/** @def Deserializes length-prepended std::vector<char>. */
#define COMM_DESERIALIZE_BUFFER(os, buffer)                        \
    {                                                              \
        uint32_t length = 0;                                       \
        COMM_DESERIALIZE_INT32(is, length);                        \
        buffer.resize(length, 0);                                  \
        commutil::ErrorCode rc = is.readBytes(&buffer[0], length); \
        if (rc != commutil::ErrorCode::E_OK) {                     \
            return rc;                                             \
        }                                                          \
    }

}  // namespace commutil

#endif  // __SERIALIZABLE_H__