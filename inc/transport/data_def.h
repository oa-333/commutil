#ifndef __DATA_DEF_H__
#define __DATA_DEF_H__

// TODO: this should be part of client side request entry, and each server side connection as a
// static buffer ready for IO (if required buffer size exceeds static size then resort to dynamic)

/** @brief The maximum buffer size allowed for transport layer (16 MB). */
#define COMMUTIL_MAX_TRANSPORT_BUFFER_SIZE (16ul * 1024 * 1024)

/**
 * @def Write flag denoting the write buffer is passed by reference, and therefore can be used as
 * is, rather than being copied to another dynamically allocated buffer.
 */
#define COMMUTIL_MSG_WRITE_BY_REF ((uint32_t)0x00000001)

/** @def Write flag denoting the write buffer should be disposed when write is done. */
#define COMMUTIL_MSG_WRITE_DISPOSE_BUFFER ((uint32_t)0x00000002)

#endif  // __DATA_DEF_H__