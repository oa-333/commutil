#ifndef __DATA_CLIENT_H__
#define __DATA_CLIENT_H__

#include <uv.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include "comm_util_def.h"
#include "comm_util_err.h"
#include "comm_util_log.h"
#include "io/io_def.h"
#include "transport/connection_details.h"
#include "transport/data_allocator.h"
#include "transport/data_listener.h"
#include "transport/data_loop_listener.h"

// TODO: put this in a common place
// TODO: this should be part of client side request entry, and each server side connection as a
// static buffer ready for IO (if required buffer size exceeds static size then resort to dynamic)
/** @brief The maximum buffer size allowed for transport layer (16 MB). */
#define COMMUTIL_MAX_TRANSPORT_BUFFER_SIZE (16ul * 1024 * 1024)

namespace commutil {

// forward declaration
class COMMUTIL_API DataClient;

struct COMMUTIL_API ClientBufferData {
    ClientBufferData(DataClient* dataClient, const uv_buf_t& buf,
                     bool shouldDeallocateBuffer = false)
        : m_dataClient(dataClient), m_buf(buf), m_shouldDeallocateBuffer(shouldDeallocateBuffer) {}
    ClientBufferData(const ClientBufferData&) = delete;
    ClientBufferData(ClientBufferData&&) = delete;
    ClientBufferData& operator=(const ClientBufferData&) = delete;
    ~ClientBufferData() {}

    DataClient* m_dataClient;
    uv_buf_t m_buf;
    bool m_shouldDeallocateBuffer;
    void* m_userData;
};

// allow customizing allocations for better performance
class COMMUTIL_API DataClientAllocator : public DataAllocator {
public:
    DataClientAllocator() {}
    DataClientAllocator(const DataClientAllocator&) = delete;
    DataClientAllocator(DataClientAllocator&&) = delete;
    DataClientAllocator& operator=(const DataClientAllocator&) = delete;
    ~DataClientAllocator() override {}

    ClientBufferData* allocateClientBufferData(DataClient* dataClient, uv_buf_t& buf,
                                               bool shouldDeallocateBuffer);
    void freeClientBufferData(ClientBufferData* cbPair);
};

/**
 * @brief Abstract base class for all network/IPC clients. The data client does not take care of
 * message boundaries, and is responsible only for transporting data buffers to and fro.
 */
class COMMUTIL_API DataClient {
public:
    virtual ~DataClient() {}

    /**
     * @brief Initializes the data client.
     * @param listener The data listener.
     * @param allocator Optional allocator for various phases in the I/O pipeline.
     * @return The operation's result.
     * @note The data client is ready for usage only after
     */
    ErrorCode initialize(DataListener* listener, DataClientAllocator* dataAllocator = nullptr);

    /** @brief Terminates the data client. */
    ErrorCode terminate();

    /** @brief Starts the data client IO thread running. */
    ErrorCode start();

    /** @brief Stops the data client IO thread. */
    ErrorCode stop();

    /** @brief Installs a data loop listener. */
    inline void setDataLoopListener(DataLoopListener* listener) { m_dataLoopListener = listener; }

    /** @brief Queries whether the data client is ready for IO. */
    bool isReady();

    /**
     * @brief Waits for the data client to become ready for IO. This is a blocking call
     * @return int The connection status (zero if succeeded).
     */
    int waitReady();

    /** @brief Retrieves the connection details of the client/server connection. */
    inline const ConnectionDetails& getConnectionDetails() const { return m_connectionDetails; }

    /** @brief Queries whether this data client requires big-endian conversions. */
    inline ByteOrder getByteOrder() const { return m_byteOrder; }

    /**
     * @brief Retrieves the installed data allocator (required for asynchronous buffer
     * deallocation).
     */
    inline DataAllocator* getDataAllocator() { return m_dataAllocator; }

    /**
     * @brief Writes a data buffer through the underlying transport channel.
     * @param buffer The message buffer.
     * @param length The message length.
     * @param syncCall Optionally specifies whether the call is synchronous (i.e. buffer should not
     * be copied but rather passed by reference). By default call is asynchronous and the data
     * buffer is copied before being sent asynchronously through the transport layer.
     * @param userData any user data that will be passed to the loop listener during onLoopSend().
     * @return The operation's result.
     */
    ErrorCode write(const char* buffer, uint32_t length, bool syncCall = false,
                    void* userData = nullptr);

protected:
    DataClient(ByteOrder byteOrder)
        : m_dataListener(nullptr),
          m_dataAllocator(nullptr),
          m_byteOrder(byteOrder),
          m_transport(nullptr),
          m_dataLoopListener(nullptr),
          m_isReady(false),
          m_connectStatus(0) {}
    DataClient(const DataClient&) = delete;
    DataClient(DataClient&&) = delete;
    DataClient& operator=(const DataClient&) = delete;

    /**
     * Transport Layer
     */

    // initialize the transport and connect if this is a stream transport
    // any derived class should redirect incoming data to
    virtual ErrorCode initializeTransport(uv_loop_t* clientLoop, uv_handle_t*& transport) = 0;

    // cleans up resources associated with the transport layer
    virtual ErrorCode terminateTransport() { return ErrorCode::E_OK; }

    // starts the transport machinery running (e.g. issue connect request)
    virtual ErrorCode startTransport() { return ErrorCode::E_OK; }

    // stops the transport
    virtual ErrorCode stopTransport();

    // sends a data buffer through the transport
    // any derived class should redirect the callback to onWriteStatic
    virtual ErrorCode writeTransport(ClientBufferData* clientBufferData) = 0;

    /**
     * Common Callbacks
     */

    // handle request to allocate buffer
    static void onAllocBufferStatic(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

    // handle close event
    static void onCloseStatic(uv_handle_t* handle);

    // terminate loop callback
    static void onStopTransportStatic(void* data);

    // handle read request done event
    void onRead(ssize_t nread, const uv_buf_t* buf, bool isDatagram);

    // handle write request done event
    void onWrite(ClientBufferData* clientBufferData, int status);

    // handle request to allocate buffer
    void onAllocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

    // handle close event
    void onClose(uv_handle_t* handle);

    /**
     * Utility methods for sub-classes
     */

    void setReady(int status);

    inline void setConnectionDetails(const ConnectionDetails& connectionDetails) {
        m_connectionDetails = connectionDetails;
    }

    inline uv_loop_t* getClientLoop() { return &m_clientLoop; }

    // members exposed to derived classes
    DataListener* m_dataListener;
    DataClientAllocator* m_dataAllocator;
    ConnectionDetails m_connectionDetails;

private:
    DataClientAllocator m_defaultDataAllocator;
    ByteOrder m_byteOrder;

    uv_loop_t m_clientLoop;
    uv_handle_t* m_transport;
    std::thread m_ioThread;
    DataLoopListener* m_dataLoopListener;

    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_isReady;
    int m_connectStatus;

    void ioTask();

    ErrorCode sendWriteRequest(ClientBufferData* clientBufferData);
    static void onAsyncWriteStatic(uv_async_t* asyncReq);
    static void onCloseAsyncWriteReqStatic(uv_handle_t* handle);

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __DATA_CLIENT_H__