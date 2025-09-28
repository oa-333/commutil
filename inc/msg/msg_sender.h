#ifndef __MSG_SENDER_H__
#define __MSG_SENDER_H__

#include <list>

#include "comm_util_def.h"
#include "comm_util_err.h"
#include "comm_util_log.h"
#include "msg/msg_backlog.h"
#include "msg/msg_client.h"
#include "msg/msg_config.h"
#include "msg/msg_frame_reader.h"
#include "msg/msg_frame_writer.h"
#include "msg/msg_response_handler.h"
#include "msg/msg_writer.h"

// TODO: add MsgConfig with following members:
// max-concurrent requests

namespace commutil {

/**
 * @brief Message sender utility class. Takes care of putting in backlog and resending when
 * transport layer is down.
 */
class COMMUTIL_API MsgSender : public MsgRequestListener, public DataLoopListener {
public:
    MsgSender()
        : m_statListener(nullptr),
          m_stopResend(false),
          m_resendDone(false),
          m_stopResendTimeMillis(0),
          m_resendTimerId(0),
          m_shutdownTimerId(0) {}
    MsgSender(const MsgSender&) = delete;
    MsgSender(MsgSender&&) = delete;
    MsgSender& operator=(const MsgSender&) = delete;
    ~MsgSender() override {}

    /**
     * @brief Initializes the message sender.
     *
     * @param msgClient The message client (should be passed).
     * @param msgConfig Timeouts and backlog configuration.
     * @param serverName The name of the destination server (for logging purposes only).
     * @param statListener Message statistics listener.
     * @param listener Message listener.
     * @param assistant Object used in deciding whether a response is faulty (required for resend).
     */
    ErrorCode initialize(MsgClient* msgClient, const MsgConfig& msgConfig, const char* serverName,
                         MsgStatListener* statListener, MsgResponseHandler* assistant = nullptr);

    /** @brief Terminates the message client. */
    ErrorCode terminate();

    /** @brief Starts the underlying transport layer running. */
    ErrorCode start();

    /** @brief Stops the message client. */
    ErrorCode stop();

    /**
     * @brief Sends a message through the underlying transport. This is not a blocking call.
     * @param msgId The message id.
     * @param body The message's body.
     * @param len The message's length.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param flags Optional flags to be added to the message header.
     * @return The operation result.
     */
    ErrorCode sendMsg(uint16_t msgId, const char* body, size_t len, bool compress = false,
                      uint16_t flags = 0);

    /**
     * @brief Sends a message through the underlying transport using custom message writer. This is
     * not a blocking call.
     * @param msgId The message id.
     * @param msgWriter The message payload writer.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param flags Optional flags to be added to the message header.
     * @return The operation result.
     * @note The result handler is invoked in order to decide whether to save the message for
     * resending. If none was installed and message sending failed, then the message is discarded
     * and will not be an attempt to resend the message to the server.
     */
    ErrorCode sendMsg(uint16_t msgId, MsgWriter* msgWriter, bool compress = false,
                      uint16_t flags = 0);

    /**
     * @brief Sends a message through the underlying transport using custom message writer. This is
     * not a blocking call.
     * @param msg The message to send.
     * @return The operation result.
     * @note The message is kept for resending until a response arrived, and then the result
     * handler, if installed, is invoked in order to decide whether to save the message for
     * resending.
     */
    // ErrorCode sendMsg(Msg* msg);

    /**
     * @brief Sends a message through the underlying transport and waits for the matching response.
     * This is a blocking call, waiting for the matching response to arrive (matched by unique
     * request id). If an assistant is installed, it will be called to decide whether the
     * transaction was successful.
     * @param msgId The message id.
     * @param body The message's body.
     * @param len The message's length.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param flags Optional flags to be added to the message header.
     * @param timeoutMillis Optional timeout for the message transaction. If not specified then the
     * send timeout passed through the configuration object in the call to @ref initialize() is
     * used. Indefinite timeout can be used with @ref COMMUTIL_MSG_INFINITE_TIMEOUT.
     * @return The operation result.
     * @note The assistant is invoked in order to decide whether the transaction was successful or
     * not. If none was installed then success is assumed. If message sending failed, then the
     * message is appended to the resend backlog.
     */
    ErrorCode transactMsg(uint16_t msgId, const char* body, size_t len, bool compress = false,
                          uint16_t flags = 0, uint64_t timeoutMillis = COMMUTIL_MSG_CONFIG_TIMEOUT);

    /**
     * @brief Sends a message through the underlying transport using custom message writer. This is
     * a blocking call, waiting for the matching response to arrive (matched by unique request id).
     * If an assistant is installed, it will be called to decide whether the transaction was
     * successful.
     * @param msgId The message id.
     * @param msgWriter The message payload writer.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param flags Optional flags to be added to the message header.
     * @param timeoutMillis Optional timeout for the message transaction. If not specified then the
     * send timeout passed through the configuration object in the call to @ref initialize() is
     * used. Indefinite timeout can be used with @ref COMMUTIL_MSG_INFINITE_TIMEOUT.
     * @return The operation result.
     * @note The result handler is invoked in order to decide whether to save the message for
     * resending. If none was installed and message sending failed, then the message is discarded
     * and there will not be an attempt to resend the message to the server.
     */
    ErrorCode transactMsg(uint16_t msgId, MsgWriter* msgWriter, bool compress = false,
                          uint16_t flags = 0, uint64_t timeoutMillis = COMMUTIL_MSG_CONFIG_TIMEOUT);

    /**
     * @brief Sends a message through the underlying transport. This is a blocking call, waiting for
     * the matching response to arrive (matched by unique request id). If an assistant is installed,
     * it will be called to decide whether the transaction was successful.
     * @param msg The message send.
     * @param timeoutMillis Optional timeout for the message transaction. If not specified then the
     * send timeout passed through the configuration object in the call to @ref initialize() is
     * used. Indefinite timeout can be used with @ref COMMUTIL_MSG_INFINITE_TIMEOUT.
     * @return The operation result.
     * @note The result handler is invoked in order to decide whether to save the message for
     * resending. If none was installed and message sending failed, then the message is discarded
     * and there will not be an attempt to resend the message to the server.
     */
    ErrorCode transactMsg(Msg* msg, uint64_t timeoutMillis = COMMUTIL_MSG_CONFIG_TIMEOUT);

    /**
     * @brief Notify incoming response for a pending request.
     * @param request The pending request.
     * @param response The incoming response.
     */
    void onResponseArrived(Msg* request, Msg* response) override;

    /**
     * @brief Notifies of I/O data loop starting (called before @ref uv_run()).
     * @param loop The loop that is to be executed.
     */
    void onLoopStart(uv_loop_t* loop) override;

    /**
     * @brief Notifies of I/O data loop ending (called after @ref uv_run()).
     * @param loop The loop that finished executing.
     */
    void onLoopEnd(uv_loop_t* loop) override;

    /**
     * @brief Notifies of data just being sent.
     * @param loop The loop that was used to send data.
     * @param userData User data passed when calling @ref DataClient::Write().
     */
    void onLoopSend(uv_loop_t* loop, void* userData) override;

    /**
     * @brief Notifies of data just being received.
     * @param loop The loop that was used to receive data.
     */
    void onLoopRecv(uv_loop_t* loop) override;

    /**
     * @brief Notifies of timeout expiring after calling @ref setUpTimer().
     * @param loop The loop associated with the timer.
     * @param timerId The timer id.
     */
    void onLoopTimer(uv_loop_t* loop, uint64_t timerId) override;

    /**
     * @brief Notifies of loop interrupt triggered by calling @ref interruptLoop().
     * @param loop The interrupted loop.
     * @param userData The associated user data passed to @ref interruptLoop().
     */
    void onLoopInterrupt(uv_loop_t* loop, void* userData) override;

private:
#if 0
    /**
     * @brief Prepares a message for sending through the underlying transport.
     * @param msgId The message id.
     * @param body The message's body.
     * @param len The message's length.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param flags Optional flags to be added to the message header.
     * @return The operation result.
     */
    Msg* prepareMsg(uint16_t msgId, const char* body, size_t len, bool compress = false,
                    uint16_t flags = 0);

    /**
     * @brief Prepares a message for sending through the underlying transport.
     * @param msgId The message id.
     * @param msgWriter The message payload writer.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param flags Optional flags to be added to the message header.
     * @return The operation result.
     */
    Msg* prepareMsg(uint16_t msgId, MsgWriter* msgWriter, bool compress = false,
                    uint16_t flags = 0);
#endif

    ErrorCode sendMsgInternal(uint16_t msgId, const char* body, size_t len, bool compress,
                              uint16_t msgFlags, uint32_t requestFlags, MsgRequestData& requestData,
                              Msg** msg);
    ErrorCode sendMsgInternal(uint16_t msgId, MsgWriter* msgWriter, bool compress,
                              uint16_t msgFlags, uint32_t requestFlags, MsgRequestData& requestData,
                              Msg** msg);
    ErrorCode recvResponse(MsgRequestData& requestData, Msg* msg, uint64_t timeoutMillis);

    /** @brief Resends a message pending in the backlog. */
    ErrorCode resendMsg(Msg* msg);

    MsgClient* m_msgClient;
    MsgFrameWriter m_frameWriter;
    MsgFrameReader m_frameReader;
    // TODO: replace this with frame listener?
    MsgResponseHandler* m_responseHandler;

    // message client configuration
    MsgConfig m_config;

    // log target name (for logging)
    std::string m_logTargetName;

    // statistics listener
    MsgStatListener* m_statListener;

    // resend members
    MsgBacklog m_backlog;

    std::atomic<bool> m_stopResend;
    std::atomic<bool> m_resendDone;
    uint64_t m_stopResendTimeMillis;
    uv_loop_t* m_dataLoop;
    uint64_t m_resendTimerId;
    uint64_t m_shutdownTimerId;

    void stopResendTimer();
    void waitBacklogEmpty();
    // void copyPendingBacklog();
    void processPendingResponses();
    void dropExcessBacklog();
    ErrorCode resendShippingBacklog(bool duringShutdown = false,
                                    uint64_t* minRequestTimeoutMillis = nullptr);
    ErrorCode renewRequestId(Msg* msg);
    ErrorCode resendRequest(Msg* request, uint64_t* minRequestTimeoutMillis);
    ErrorCode handleResponse(Msg* request, Msg* response);

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_SENDER_H__