#ifndef __MSG_CLIENT_H__
#define __MSG_CLIENT_H__

#include <condition_variable>
#include <mutex>

#include "comm_util_err.h"
#include "comm_util_log.h"
#include "msg/msg.h"
#include "msg/msg_assembler.h"
#include "msg/msg_listener.h"
#include "msg/msg_request_pool.h"
#include "msg/msg_stat_listener.h"
#include "transport/data_client.h"

/** @def Maximum number of concurrent requests per client. */
#define COMMUTIL_MAX_CONCURRENT_REQUESTS 1024

namespace commutil {

/** @brief A messaging client that runs on top of a data client (transport layer provider). */
class COMMUTIL_API MsgClient : public MsgListener {
public:
    MsgClient() : m_dataClient(nullptr), m_listener(nullptr) {}
    MsgClient(const MsgClient&) = delete;
    MsgClient(MsgClient&&) = delete;
    MsgClient& operator=(const MsgClient&) = delete;
    ~MsgClient() override {}

    /**
     * @brief Initializes the message client (synchronous mode).
     *
     * @param dataClient The transport data client.
     * @param maxConcurrentRequests The maximum number of pending requests allowed for this client.
     * @param msgListener Optional message listener. The listener is notified of incoming messages
     * only when using the asynchronous API sendMsg().
     * @return The operation's result.
     */
    ErrorCode initialize(DataClient* dataClient, uint32_t maxConcurrentRequests,
                         MsgListener* msgListener = nullptr);

    /** @brief Terminates the message client. */
    ErrorCode terminate();

    /** @brief Starts the underlying transport layer running. */
    ErrorCode start();

    /** @brief Stops the message client. */
    ErrorCode stop();

    /**
     * @brief Installs a data loop listener, which allows integrating with the IO loop, and
     * reacting to loop events.
     */
    inline void setDataLoopListener(DataLoopListener* listener) {
        m_dataClient->setDataLoopListener(listener);
    }

    /**
     * @brief Sends a message through a client connection.
     * @param msg The message to send.
     * @param requestFlags Optionally specifies additional request flags.
     * @param[out] requestData Optionally return the request details If the call is synchronous,
     * then a request data is returned to the caller, for waiting on the response.
     * @return True if the message was sent successfully.
     */
    ErrorCode sendMsg(Msg* msg, uint32_t requestFlags = 0, MsgRequestData* requestData = nullptr);

    /**
     * @brief Transacts with a server (sends a message and waits for the corresponding reply).
     *
     * @param msg The message to send to the server.
     * @param[out] response The resulting response when transaction succeeds.
     * @param timeoutMillis Optional timeout in milliseconds.
     * @return ErrorCode
     */
    ErrorCode transactMsgResponse(Msg* msg, Msg** response,
                                  uint64_t timeoutMillis = COMMUTIL_MSG_INFINITE_TIMEOUT);

    /**
     * @brief Transacts with a server
     * @tparam F Response processing function type.
     * @param msg The message to send to the server.
     * @param timeoutMillis Transaction timeout in milliseconds. Pass @ref
     * COMMUTIL_MSG_INFINITE_TIMEOUT for indefinite timeout.
     * @param f The function processing server response.
     * @return The operation's result.
     */
    template <typename F>
    inline ErrorCode transactMsg(Msg* msg, uint64_t timeoutMillis, F f) {
        // transact request-response
        Msg* response = nullptr;
        ErrorCode rc = transactMsgResponse(msg, &response, timeoutMillis);
        if (rc != ErrorCode::E_OK) {
            return rc;
        }

        // process response
        rc = f(response);

        // cleanup
        if (response != nullptr) {
            freeMsg(response);
            response = nullptr;
        }
        return rc;
    }

    /**
     * Transport Ready API
     */

    /**
     * @brief Queries whether the data client is ready for IO.
     * @return E_OK The client is ready for I/O.
     * @return E_INPROGRESS The client is not ready yet for I/O.
     * @return Any other error code.
     */
    ErrorCode isReady();

    /**
     * @brief Waits for the data client to become ready for IO. This is a blocking call
     * @return false if connection is ready.
     * @return false if connection attempt failed.
     */
    ErrorCode waitReady();

    /**
     * @brief Waits fot the TCP connection to connect.
     * @param status Optionally receives the connection status. Valid only if call returns with
     * ErrorCode::E_OK.
     * @return E_OK If wait finished successfully, in which case the status output parameter is
     * returned if provided by the caller. Otherwise returns an error code.
     */
    ErrorCode waitConnect(int* status);

    /** @brief Retrieves the connection details of the transport layer. */
    inline const ConnectionDetails& getConnectionDetails() const {
        return m_connectData.getConnectionDetails();
    }

    /**
     * @brief Notify incoming connection.
     * @param connectionDetails The connection details.
     * @param status The operation status. Zero means success. Any other value denotes an error.
     * @return True if the connection is to be accepted.
     * @return False if the connection is to be rejected. Returning false will cause the connection
     * to be closed.
     */
    bool onConnect(const ConnectionDetails& connectionDetails, int status) override;

    /**
     * @brief Notify connection closed.
     * @param connectionDetails The connection details.
     */
    void onDisconnect(const ConnectionDetails& connectionDetails) override;

    /**
     * @brief Notify of an incoming message.
     * @param connectionDetails The connection details.
     * @param msg The message.
     * @param canSaveMsg Denotes whether the listener is allowed to keep a reference to the message.
     * If not, then the result value is ignored.
     * @return @ref MSG_CAN_DELETE if the message can be deleted, or rather @ref MSG_CANNOT_DELETE
     * if it is still being used.
     */
    MsgAction onMsg(const ConnectionDetails& connectionDetails, Msg* msg, bool canSaveMsg) override;

    /** @brief Retrieves the request pool. */
    inline MsgRequestPool& getRequestPool() { return m_requestPool; }

    /** @brief Queries whether this data client requires big-endian conversions. */
    inline ByteOrder getByteOrder() { return m_dataClient->getByteOrder(); }

private:
    /** @var The data client of the transport layer. */
    DataClient* m_dataClient;

    /** @brief Framing protocol assembler. */
    MsgAssembler m_msgAssembler;

    // pending requests members
    MsgRequestPool m_requestPool;

    /** @brief Handle connect notification. */
    struct ConnectData {
        ConnectData() : m_connectArrived(0), m_connectStatus(0) {}
        ConnectData(const ConnectData&) = delete;
        ConnectData(ConnectData&&) = delete;
        ConnectData& operator=(const ConnectData&) = delete;
        ~ConnectData() {}

        /** @brief Resets connection data. */
        void resetConnectData();

        /**
         * @brief Waits fot the TCP connection to connect.
         * @param status Optionally receives the connection status. Valid only if call returns with
         * ErrorCode::E_OK.
         * @return E_OK If wait finished successfully, in which case the status output parameter is
         * returned if provided by the caller. Otherwise returns an error code.
         */
        ErrorCode waitConnect(int* status);

        /**
         * @brief Notifies of connection event.
         * @param connectionDetails The connection details.
         * @param status The connection status.
         */
        void notifyConnect(const ConnectionDetails& connectionDetails, int status);

        /** @brief Retrieves the connection details. */
        inline const ConnectionDetails& getConnectionDetails() const { return m_connectionDetails; }

    private:
        /** @var Lock for synchronizing connection data access (arrived flag and status). */
        std::mutex m_connectLock;

        /** @var Condition variable used for waiting for state change. */
        std::condition_variable m_connectCv;

        /** @var Flag designating whether a connection has arrived. */
        uint32_t m_connectArrived;

        /** @var The connection status as reported by libuv. */
        int m_connectStatus;

        /** @var The details of the server connection. */
        ConnectionDetails m_connectionDetails;
    } m_connectData;

    /** @var External message listener (optional). */
    MsgListener* m_listener;

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_CLIENT_H__