#ifndef __DATA_STREAM_SERVER_H__
#define __DATA_STREAM_SERVER_H__

#include "comm_util_log.h"
#include "transport/data_allocator.h"
#include "transport/data_server.h"

namespace commutil {

/**
 * @brief TCP server class, using configured concurrency level, such that each worker thread polls
 * for a set of assigned sockets. A single worker thread receives incoming connections and passes
 * them to the worker threads for I/O.
 */
class COMMUTIL_API DataStreamServer : public DataServer {
public:
    ~DataStreamServer() override {}

protected:
    DataStreamServer(ByteOrder byteOrder) : DataServer(byteOrder) {}
    DataStreamServer(const DataStreamServer&) = delete;
    DataStreamServer(DataStreamServer&&) = delete;
    DataStreamServer& operator=(const DataStreamServer&) = delete;

    // sends a data buffer through the transport
    ErrorCode writeTransport(ServerBufferData* serverBufferData) override;

    // allocation callback when handle data is a connection data
    static void onAllocConnBufferStatic(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

private:
    // handle async write request event
    // static void onAsyncWriteStatic(uv_async_t* asyncReq);

    // handle write request done event
    static void onWriteStatic(uv_write_t* req, int status);

    // handle async write request
    // void onAsyncWrite(ServerBufferData* serverBufferData);

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __TCP_SERVER_H__