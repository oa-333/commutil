#ifndef __DATA_SERVER_H__
#define __DATA_SERVER_H__

#include <uv.h>

#include <atomic>
#include <list>
#include <mutex>
#include <string>
#include <thread>

#include "comm_util_err.h"
#include "comm_util_log.h"
#include "io/io_def.h"
#include "transport/data_allocator.h"
#include "transport/data_listener.h"

namespace commutil {

// forward declaration
class COMMUTIL_API DataServer;

struct COMMUTIL_API ServerBufferData {
    ServerBufferData(DataServer* dataServer, uint64_t connectionIndex, const uv_buf_t& buf,
                     bool shouldDeallocateBuffer = false)
        : m_dataServer(dataServer),
          m_connectionIndex(connectionIndex),
          m_buf(buf),
          m_shouldDeallocateBuffer(shouldDeallocateBuffer) {}
    ServerBufferData(const ServerBufferData&) = delete;
    ServerBufferData(ServerBufferData&&) = delete;
    ServerBufferData& operator=(const ServerBufferData&) = delete;
    ~ServerBufferData() {}

    DataServer* m_dataServer;
    uint64_t m_connectionIndex;
    uv_buf_t m_buf;
    bool m_shouldDeallocateBuffer;
};

// allow customizing allocations for better performance
class COMMUTIL_API DataServerAllocator : public DataAllocator {
public:
    DataServerAllocator() {}
    DataServerAllocator(const DataServerAllocator&) = delete;
    DataServerAllocator(DataServerAllocator&&) = delete;
    DataServerAllocator& operator=(const DataServerAllocator&) = delete;
    ~DataServerAllocator() override {}

    virtual ServerBufferData* allocateServerBufferData(DataServer* dataServer,
                                                       uint64_t connectionIndex, uv_buf_t& buf,
                                                       bool shouldDeallocateBuffer) {
        return new (std::nothrow)
            ServerBufferData(dataServer, connectionIndex, buf, shouldDeallocateBuffer);
    }
    virtual void freeServerBufferData(ServerBufferData* sbPair) { delete sbPair; }
};

/**
 * @brief TCP server class, using configured concurrency level, such that each worker thread polls
 * for a set of assigned sockets. A single worker thread receives incoming connections and passes
 * them to the worker threads for I/O.
 */
class COMMUTIL_API DataServer {
public:
    virtual ~DataServer() {}

    /**
     * @brief Initializes the data server.
     * @param listener The listener that will receive incoming bytes from connected servers.
     * @param maxConnections The maximum number of connections the server can handle concurrently.
     * This holds true also for datagram server, in which case there is a limit to the number of
     * different servers sending datagrams to the server, along with some expiry control. @see
     * UdpServer for more information.
     * @param bufferSize The buffer size used for each server connection I/O. Specify a buffer size
     * large enough to hold both incoming and outgoing messages, in order to avoid message
     * segmentation and reassembly at the application level.
     * @param dataAllocator Optional allocator for various phases in the I/O pipeline.
     * @return ErrorCode The operation result.
     */
    ErrorCode initialize(DataListener* listener, uint32_t maxConnections, uint32_t bufferSize,
                         DataServerAllocator* dataAllocator = nullptr);

    /**
     * @brief Terminates the data server. Since termination can be rather complex, this method is
     * made virtual.
     */
    ErrorCode terminate();

    /** @brief Starts the data server running. */
    ErrorCode start();

    /** @brief Starts the data server. */
    ErrorCode stop();

    /** @brief Retrieves the maximum allowed number of connections. */
    inline uint32_t getMaxConnection() const { return m_maxConnections; }

    /** @brief Queries whether this data client requires big-endian conversions. */
    inline ByteOrder getByteOrder() const { return m_byteOrder; }

    /**
     * @brief Retrieves the installed data allocator (required for asynchronous buffer
     * deallocation).
     */
    inline DataAllocator* getDataAllocator() { return m_dataAllocator; }

    /**
     * @brief Sends a reply message through a server. This is a utility method for the listener,
     * such that it can respond to incoming messages from some server easily.
     * @param connectionDetails The connection details, as received in @ref
     * DataListener::onBytesReceived().
     * @param buffer The data to send (serialized response). The buffer is copied before sending, so
     * the caller can recycle the buffer just right after this call.
     * @param length The buffer length.
     * @param directBuffer Optionally specifies whether to avoid copying the input buffer, but
     * rather using it directly. By default output buffer is copied.
     * @param disposeBuffer Optionally specifies whether to dispose of the buffer after writing is
     * complete (regardless of whether it is used as is without copying or if duplicated
     * internally). By default the buffer will be disposed after using it.
     * @return The operation result.
     * @note When specifying to dispose of the input buffer, in case of failure, the caller is
     * responsible for deallocating the buffer.
     */
    ErrorCode replyMsg(const ConnectionDetails& connectionDetails, const char* buffer,
                       uint32_t length, bool directBuffer = false, bool disposeBuffer = true);

protected:
    DataServer(ByteOrder byteOrder)
        : m_dataAllocator(nullptr),
          m_connDataArray(nullptr),
          m_maxConnections(0),
          m_dataListener(nullptr),
          m_byteOrder(byteOrder),
          m_bufferSize(0),
          m_transport(nullptr),
          m_nextConnectionId(0),
          m_runState(RunState::RS_IDLE) {
        m_dataAllocator = &m_defaultDataAllocator;
    }
    DataServer(const DataServer&) = delete;
    DataServer(DataServer&&) = delete;
    DataServer& operator=(const DataServer&) = delete;

    struct ConnectionData {
        DataServer* m_server;
        uv_handle_t* m_connectionHandle;
        uint64_t m_connectionIndex;
        std::atomic<uint64_t> m_isUsed;
        uint64_t m_connectionId;
        ConnectionDetails m_connectionDetails;
        uv_shutdown_t m_shutdownReq;

        ConnectionData();
        ConnectionData(const ConnectionData& connData);
        ConnectionData(ConnectionData&& connData);
        virtual ~ConnectionData();
        ConnectionData& operator=(const ConnectionData& connData);
        ConnectionData& operator=(ConnectionData&& connData);
    };

    // initialize the transport and connect if this is a stream transport
    // any derived class should redirect incoming data to
    virtual ErrorCode initializeTransport(uv_loop_t* serverLoop, uv_handle_t*& transport) = 0;

    // terminates the transport layer
    virtual ErrorCode terminateTransport() { return ErrorCode::E_OK; }

    // start the transport layer
    virtual ErrorCode startTransport() { return ErrorCode::E_OK; }

    // called after transport layer is stopped
    virtual ErrorCode stopTransport() { return ErrorCode::E_OK; }

    // sends a data buffer through the transport
    // any derived class should redirect the callback to onWriteStatic
    virtual ErrorCode writeTransport(ServerBufferData* serverBufferData) = 0;

    // allow customizing connection data
    virtual ConnectionData* createConnectionData();

    // handle request to allocate buffer
    static void onAllocBufferStatic(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

    // handle close event of a server connection (stream-only, so this should be moved from here)
    static void onCloseStatic(uv_handle_t* handle);

    // handle read request done event
    void onRead(ConnectionData* connData, ssize_t nread, const uv_buf_t* buf, bool isDatagram);

    // handle write request done event
    void onWrite(ServerBufferData* serverBufferData, int status);

    // handle request to allocate buffer
    void onAllocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

    // handle close event
    void onClose(uv_handle_t* handle);

    // members exposed to derived classes
    DataServerAllocator* m_dataAllocator;
    ConnectionData** m_connDataArray;
    uint32_t m_maxConnections;
    uv_loop_t m_serverLoop;
    DataListener* m_dataListener;

    ConnectionData* grabConnectionData();

    enum class RunState : uint32_t { RS_IDLE, RS_STARTING_UP, RS_RUNNING, RS_SHUTTING_DOWN };
    RunState getRunState();

private:
    ByteOrder m_byteOrder;
    uint32_t m_bufferSize;
    DataServerAllocator m_defaultDataAllocator;
    uv_handle_t* m_transport;
    std::atomic<uint64_t> m_nextConnectionId;

    bool initConnDataArray(uint32_t maxConnections);
    void termConnDataArray();

    std::atomic<RunState> m_runState;

    bool changeRunState(RunState from, RunState to);
    void setRunState(RunState runState);

    std::thread m_serverThread;
    void serverTask();

    ErrorCode sendWriteResponse(ServerBufferData* serverBufferData);
    static void onAsyncWriteStatic(uv_async_t* asyncReq);
    static void onCloseAsyncWriteReqStatic(uv_handle_t* handle);

    static void onStopTransportStatic(void* data);
    virtual ErrorCode onStopTransport() { return ErrorCode::E_OK; }

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __TCP_SERVER_H__