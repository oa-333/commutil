#ifndef __DATA_ALLOCATOR_H__
#define __DATA_ALLOCATOR_H__

#include <uv.h>

#include "comm_util_def.h"

namespace commutil {

// allow customizing allocations for better performance
class COMMUTIL_API DataAllocator {
public:
    DataAllocator() {}
    DataAllocator(const DataAllocator&) = delete;
    DataAllocator(DataAllocator&&) = delete;
    DataAllocator& operator=(const DataAllocator&) = delete;
    virtual ~DataAllocator() {}

    virtual char* allocateRequestBuffer(size_t size);
    virtual void freeRequestBuffer(char* buffer);

    virtual uv_async_t* allocateAsyncRequest();
    virtual void freeAsyncRequest(uv_async_t* asyncReq);

    virtual uv_write_t* allocateWriteRequest();
    virtual void freeWriteRequest(uv_write_t* writeReq);

    virtual uv_udp_send_t* allocateUdpSendRequest();
    virtual void freeUdpSendRequest(uv_udp_send_t* udpSendReq);
};

}  // namespace commutil

#endif  // __DATA_ALLOCATOR_H__