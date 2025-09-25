#include "transport/data_allocator.h"

#include <new>

namespace commutil {

char* DataAllocator::allocateRequestBuffer(size_t size) { return new (std::nothrow) char[size]; }

void DataAllocator::freeRequestBuffer(char* buffer) { delete[] buffer; }

uv_async_t* DataAllocator::allocateAsyncRequest() { return new (std::nothrow) uv_async_t(); }

void DataAllocator::freeAsyncRequest(uv_async_t* asyncReq) { delete asyncReq; }

uv_write_t* DataAllocator::allocateWriteRequest() { return new (std::nothrow) uv_write_t(); }

void DataAllocator::freeWriteRequest(uv_write_t* writeReq) { delete writeReq; }

uv_udp_send_t* DataAllocator::allocateUdpSendRequest() {
    return new (std::nothrow) uv_udp_send_t();
}

void DataAllocator::freeUdpSendRequest(uv_udp_send_t* udpSendReq) { delete udpSendReq; }

}  // namespace commutil