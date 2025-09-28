#include "msg/msg_sender.h"

#include <cassert>
#include <chrono>
#include <cinttypes>

#include "commutil_common.h"
#include "commutil_log_imp.h"
#include "msg/msg_frame_reader.h"

// TODO: implement connect/read/write timeouts in the transport layer with timeout notification
// inside the pool and matching against pending requests (avoid O(n) solution, use some ordered set)
// in case a connect/read/write timeout arrives, the data listener should be notified and decide
// what to do (e.g. restart connection or resend, depending on state/policy)
//

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgSender)

ErrorCode MsgSender::initialize(MsgClient* msgClient, const MsgConfig& msgConfig,
                                const char* serverName, MsgStatListener* statListener,
                                MsgResponseHandler* responseHandler /* = nullptr */) {
    // save configuration
    m_msgClient = msgClient;
    m_config = msgConfig;
    m_logTargetName = serverName;
    m_statListener = statListener;
    m_responseHandler = responseHandler;
    m_frameReader.initialize(m_msgClient->getByteOrder(), m_statListener, m_responseHandler);
    // m_msgClient->getRequestPool().setListener(this);
    m_msgClient->setDataLoopListener(this);
    m_backlog.initialize(&m_msgClient->getRequestPool());
    return ErrorCode::E_OK;
}

ErrorCode MsgSender::terminate() {
    // m_msgClient->getRequestPool().setListener(nullptr);
    return ErrorCode::E_OK;
}

ErrorCode MsgSender::start() {
    m_resendDone.store(false, std::memory_order_relaxed);
    m_stopResend.store(false, std::memory_order_relaxed);
    ErrorCode rc = m_msgClient->start();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to start message client: %s", errorCodeToString(rc));
        return rc;
    }
    return ErrorCode::E_OK;
}

ErrorCode MsgSender::stop() {
    stopResendTimer();
    if (m_config.m_shutdownTimeoutMillis > 0) {
        waitBacklogEmpty();
    }
    ErrorCode rc = m_msgClient->stop();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to stop message client: %s", errorCodeToString(rc));
        return rc;
    }
    LOG_TRACE("Message sender stopped");
    return ErrorCode::E_OK;
}

ErrorCode MsgSender::sendMsg(uint16_t msgId, const char* body, size_t len,
                             bool compress /* = false */, uint16_t flags /* = 0 */) {
    // check state
    if (m_stopResend.load(std::memory_order_relaxed)) {
        return ErrorCode::E_INVALID_STATE;
    }

    Msg* msg = nullptr;
    MsgRequestData requestData;

    ErrorCode rc = sendMsgInternal(msgId, body, len, compress, flags, 0, requestData, &msg);

    // NOTE: the message is saved in the pending requests of the message client
    return rc;
}

ErrorCode MsgSender::sendMsg(uint16_t msgId, MsgWriter* msgWriter, bool compress /* = false */,
                             uint16_t flags /* = 0 */) {
    // check state
    if (m_stopResend.load(std::memory_order_relaxed)) {
        return ErrorCode::E_INVALID_STATE;
    }

    Msg* msg = nullptr;
    MsgRequestData requestData;

    ErrorCode rc = sendMsgInternal(msgId, msgWriter, compress, flags, 0, requestData, &msg);

    // NOTE: the message is saved in the pending requests of the message client
    return rc;
}

ErrorCode MsgSender::transactMsg(uint16_t msgId, const char* body, size_t len,
                                 bool compress /* = false */, uint16_t flags /* = 0 */,
                                 uint64_t timeoutMillis /* = COMMUTIL_MSG_CONFIG_TIMEOUT */) {
    // check state
    if (m_stopResend.load(std::memory_order_relaxed)) {
        return ErrorCode::E_INVALID_STATE;
    }

    // fix timeout
    if (timeoutMillis == COMMUTIL_MSG_CONFIG_TIMEOUT) {
        timeoutMillis = m_config.m_sendTimeoutMillis;
    }

    // send/receive message
    Msg* msg = nullptr;
    MsgRequestData requestData;
    ErrorCode rc = sendMsgInternal(msgId, body, len, compress, flags, COMMUTIL_REQUEST_FLAG_TX,
                                   requestData, &msg);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to transact message, failed sending request: %s", errorCodeToString(rc));
    } else {
        // wait for response
        rc = recvResponse(requestData, msg, timeoutMillis);
    }

    // add to backlog if failed
    if (rc != ErrorCode::E_OK) {
        if (msg->getHeader().getRequestId() == COMMUTIL_MSG_INVALID_REQUEST_ID) {
            LOG_ERROR("Cannot schedule message resend, reached resource limit");
            freeMsg(msg);
        }
    }
    return rc;

#if 0
    rc = transactMsg(msg, timeoutMillis);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to transact message with %s: %s", m_logTargetName.c_str(),
                  errorCodeToString(rc));
        addBacklog(msg);
        // do not free message, it is now in the backlog queue
        return rc;
    }

    freeMsg(msg);
    return ErrorCode::E_OK;
#endif
}

ErrorCode MsgSender::transactMsg(uint16_t msgId, MsgWriter* msgWriter, bool compress /* = false */,
                                 uint16_t flags /* = 0 */,
                                 uint64_t timeoutMillis /* = COMMUTIL_MSG_CONFIG_TIMEOUT */) {
    // check state
    if (m_stopResend.load(std::memory_order_relaxed)) {
        return ErrorCode::E_INVALID_STATE;
    }

    // fix timeout
    if (timeoutMillis == COMMUTIL_MSG_CONFIG_TIMEOUT) {
        timeoutMillis = m_config.m_sendTimeoutMillis;
    }

    // send/receive message
    Msg* msg = nullptr;
    MsgRequestData requestData;
    ErrorCode rc = sendMsgInternal(msgId, msgWriter, compress, flags, COMMUTIL_REQUEST_FLAG_TX,
                                   requestData, &msg);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to transact message, failed sending request: %s", errorCodeToString(rc));
    } else {
        // wait for response
        rc = recvResponse(requestData, msg, timeoutMillis);
    }

    // add to backlog if failed
    if (rc != ErrorCode::E_OK) {
        if (msg->getHeader().getRequestId() == COMMUTIL_MSG_INVALID_REQUEST_ID) {
            LOG_ERROR("Cannot schedule message resend, reached resource limit");
            freeMsg(msg);
        }
    }
    return rc;

#if 0
    rc = transactMsg(msg, timeoutMillis);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to transact message with %s: %s", m_logTargetName.c_str(),
                  errorCodeToString(rc));
        addBacklog(msg);
        // do not free message, it is now in the backlog queue
        return rc;
    }

    freeMsg(msg);
    return ErrorCode::E_OK;
#endif
}

ErrorCode MsgSender::transactMsg(Msg* msg, uint64_t timeoutMillis) {
    return m_msgClient->transactMsg(
        msg, timeoutMillis, [this, msg](Msg* response) { return handleResponse(msg, response); });
}

void MsgSender::onResponseArrived(Msg* request, Msg* response) {
    // just wake up resend thread
    (void)request;
    (void)response;
    // std::unique_lock<std::mutex> lock(m_lock);
    // m_cv.notify_one();
}

void MsgSender::onLoopStart(uv_loop_t* loop) {
    // start resend one-shot timer
    ErrorCode rc = startLoopTimer(loop, m_resendTimerId, m_config.m_resendPeriodMillis);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to start resend timer: %s", errorCodeToString(rc));
    }
    m_dataLoop = loop;
}

void MsgSender::onLoopEnd(uv_loop_t* loop) {
    // TODO: fix this, the last timer round should enable waiting for incoming requests
    // but we cannot wait in a loop...
    // when stop is called the following should be done:
    // raise stop flag, so that timer granularity changes (we also should send an async request for
    // that, so that the loop will change behavior immediately)
    // then wait until backlog is empty or until timed out
    // for this we need to disable incoming messages (via atomic state)
    // the timer should signal that backlog is empty and loop can stop earlier
    // for now lock/cv can be used
    // finally the loop can be closed
    (void)loop;

    // last chance
    processPendingResponses();

    // report any missing response
    uint32_t backlogCount = m_backlog.getEntryCount();
    if (backlogCount > 0) {
        LOG_ERROR("%s log target has failed to resend %u pending messages", m_logTargetName.c_str(),
                  backlogCount);
    }
}

void MsgSender::onLoopSend(uv_loop_t* loop, void* userData) {
    // add to backlog
    (void)loop;
    Msg* request = (Msg*)userData;
    if (request != nullptr) {
        MsgRequest* requestData = m_msgClient->getRequestPool().getRequestData(request);
        if (requestData == nullptr) {
            LOG_ERROR("Failed to add pending request to backlog, request data not found");
        } else {
            if (!(requestData->getFlags() & COMMUTIL_REQUEST_FLAG_REUSE)) {
                ErrorCode rc = m_backlog.addPendingRequest(request);
                if (rc != ErrorCode::E_OK) {
                    LOG_ERROR("Failed to add pending request to backlog: %s",
                              errorCodeToString(rc));
                }
            }
        }
    }
}

void MsgSender::onLoopRecv(uv_loop_t* loop) {
    // process incoming responses
    (void)loop;
    processPendingResponses();
}

void MsgSender::onLoopTimer(uv_loop_t* loop, uint64_t timerId) {
    (void)loop;

    // when the stop flag is raised, the resend timer should stop, letting only more fast paced
    // shutdown timer to work
    if (timerId == m_resendTimerId && m_stopResend.load(std::memory_order_relaxed)) {
        // this in effect will terminate the resend timer callback
        return;
    }

    // if the resend done flag is raised then we are done
    // NOTE: since both timers are continually rearmed one-shot timers this means that all timer
    // jobs end now
    if (m_resendDone.load(std::memory_order_relaxed)) {
        // this in effect will terminate the shutdown timer callback
        return;
    }
    // do resend maintenance (holds true also during shutdown):

    // check for incoming and expired requests and remove from pending backlog.
    processPendingResponses();

    // see if we exceeded limit
    dropExcessBacklog();

    // now retry to send queued back log messages
    uint64_t minRequestTimeoutMillis = COMMUTIL_MSG_INFINITE_TIMEOUT;
    ErrorCode rc = resendShippingBacklog(false, &minRequestTimeoutMillis);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to resend shipping backlog: %s", errorCodeToString(rc));
    }

    // determine next timer timeout
    uint64_t nextResendTimeMillis = 0;
    if (minRequestTimeoutMillis == COMMUTIL_MSG_INFINITE_TIMEOUT) {
        nextResendTimeMillis = m_config.m_resendPeriodMillis;
    } else {
        nextResendTimeMillis = getCurrentTimeMillis() - minRequestTimeoutMillis;
    }

    // during shutdown we poll with different frequency, and also there is a limit to the number of
    // times we trigger the timer again
    bool shouldSetNextTimer = true;
    if (m_stopResend.load(std::memory_order_relaxed)) {
        nextResendTimeMillis =
            std::min(nextResendTimeMillis, m_config.m_shutdownPollingTimeoutMillis);
        if (m_backlog.isEmpty() || getCurrentTimeMillis() > m_stopResendTimeMillis) {
            m_resendDone.store(true, std::memory_order_relaxed);
            shouldSetNextTimer = false;
            LOG_TRACE("Ending shutdown now");
        } else {
            LOG_TRACE("Scheduling shutdown timer");
        }
    }

    if (shouldSetNextTimer) {
        // set next timer timeout
        rc = setNextLoopTimer(timerId, nextResendTimeMillis);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to set next resend timer: %s", errorCodeToString(rc));
        }
        LOG_TRACE("Scheduling next resend timer with %" PRIu64 " millis", nextResendTimeMillis);
    } else {
        // otherwise one-shot timer dies
        /*// stop periodic timer
        rc = stopLoopTimer(m_resendTimerId);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to stop resend timer");
        }
        rc = stopLoopTimer(m_shutdownTimerId);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to stop shutdown timer");
        }
        m_resendDone.store(true, std::memory_order_relaxed);*/
    }
}

void MsgSender::onLoopInterrupt(uv_loop_t* loop, void* userData) {
    (void)loop;
    (void)userData;

    // if shutdown grace period was configured then change to shutdown polling period
    // otherwise close the timer now
    if (m_config.m_shutdownTimeoutMillis > 0) {
        /*ErrorCode rc = setPeriodicLoopTimer(loop, m_config.m_shutdownPollingTimeoutMillis);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to start shutdown loop timer: %s", errorCodeToString(rc));
        }*/
        // stopLoopTimer(m_resendTimerId);
        // uv_print_all_handles(loop, stderr);
        ErrorCode rc =
            startLoopTimer(loop, m_shutdownTimerId, m_config.m_shutdownPollingTimeoutMillis);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to start shutdown loop timer: %s", errorCodeToString(rc));
        }
        m_stopResendTimeMillis = getCurrentTimeMillis() + m_config.m_shutdownTimeoutMillis;
        LOG_TRACE("Starting shutdown now");
    } else {
        ErrorCode rc = stopLoopTimer(m_resendTimerId);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to stop loop timer: %s", errorCodeToString(rc));
        }
    }
}

#if 0
Msg* MsgSender::prepareMsg(uint16_t msgId, const char* body, size_t len,
                           bool compress /* = false */, uint16_t flags /* = 0 */) {
            // compress body if needed
            std::string compressedBody;
            if (compress) {
                gzip::Compressor comp(Z_BEST_COMPRESSION);
                comp.compress(compressedBody, body, len);
                body = compressedBody.c_str();
                len = compressedBody.size();
                flags |= COMMUTIL_MSG_FLAG_COMPRESSED;
            }

            // allocate message buffer
            Msg* msg = allocMsg(msgId, flags, 0, 0, (uint32_t)len);
            if (msg == nullptr) {
                LOG_ERROR("Failed to allocate message with payload size %zu, out of memory", len);
                return nullptr;
            }

            // copy payload
            memcpy(msg->modifyPayload(), body, len);
            return msg;
        }

        Msg* MsgSender::prepareMsg(uint16_t msgId, MsgWriter* msgWriter,
                                   bool compress /* = false */, uint16_t flags /* = 0 */) {
            // if compression is enabled then we have no choice but first to serialize into a
            // temporary buffer and then write into the payload buffer (because we cannot tell
            // required buffer size before compression takes place)
            Msg* msg = nullptr;
            if (compress) {
                // serialize message to buffer
                std::vector<char> buf(msgWriter->getPayloadSizeBytes(), 0);
                ErrorCode rc = msgWriter->writeMsg(&buf[0]);
                if (rc != ErrorCode::E_OK) {
                    LOG_ERROR("Failed to write message into buffer: %s", errorCodeToString(rc));
                    return nullptr;
                }

                // compress serialized message
                std::string compressedBody;
                gzip::Compressor comp(Z_BEST_COMPRESSION);
                comp.compress(compressedBody, &buf[0], buf.size());

                // prepare message frame
                flags |= COMMUTIL_MSG_FLAG_COMPRESSED;
                msg = allocMsg(msgId, flags, 0, 0, (uint32_t)compressedBody.size());
                if (msg == nullptr) {
                    LOG_ERROR(
                        "Failed to allocate message with compressed payload size %zu, out of "
                        "memory",
                        compressedBody.size());
                    return nullptr;
                }
                memcpy(msg->modifyPayload(), compressedBody.data(), compressedBody.size());
            } else {
                msg = allocMsg(msgId, flags, 0, 0, msgWriter->getPayloadSizeBytes());
                if (msg == nullptr) {
                    LOG_ERROR("Failed to allocate message with payload %u, out of memory",
                              msgWriter->getPayloadSizeBytes());
                    return nullptr;
                }

                // serialize directly into payload
                ErrorCode rc = msgWriter->writeMsg(msg->modifyPayload());
                if (rc != ErrorCode::E_OK) {
                    LOG_ERROR("Failed to write message into buffer: %s", errorCodeToString(rc));
                    freeMsg(msg);
                    return nullptr;
                }
            }

            return msg;
        }
#endif

ErrorCode MsgSender::sendMsgInternal(uint16_t msgId, const char* body, size_t len, bool compress,
                                     uint16_t msgFlags, uint32_t requestFlags,
                                     MsgRequestData& requestData, Msg** msg) {
    // start with default headers specified in initialize(), and add additional headers if
    // any
    LOG_TRACE("Sending log data to: %s", m_msgClient->getConnectionDetails().toString());

    ErrorCode rc = m_frameWriter.prepareMsgFrame(msg, msgId, body, len, compress, msgFlags);
    if (rc != ErrorCode::E_OK) {
        return rc;
    }

    // TODO: if we keep uncompressed size in message header (also good for validation) we
    // can refactor out the following part into one line: sendMsg(msg)

    // send message
    rc = m_msgClient->sendMsg(*msg, requestFlags, requestData);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to send message through message client: %s", errorCodeToString(rc));
    }

    // update statistics and return
    if (m_statListener != nullptr) {
        uint32_t compressedPayloadSize = compress ? (*msg)->getPayloadSizeBytes() : 0;
        m_statListener->onSendMsgStats((uint32_t)len, compressedPayloadSize, (int)rc);
    }

    // NOTE: the message is saved in the pending requests of the message client
    return rc;
}

ErrorCode MsgSender::sendMsgInternal(uint16_t msgId, MsgWriter* msgWriter, bool compress,
                                     uint16_t msgFlags, uint32_t requestFlags,
                                     MsgRequestData& requestData, Msg** msg) {
    // start with default headers specified in initialize(), and add additional headers if
    // any
    LOG_TRACE("Sending log data to: %s", m_msgClient->getConnectionDetails().toString());

    ErrorCode rc = m_frameWriter.prepareMsgFrame(msg, msgId, msgWriter, compress, msgFlags);
    if (rc != ErrorCode::E_OK) {
        return rc;
    }

    // send message
    rc = m_msgClient->sendMsg(*msg, requestFlags, requestData);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to send message through message client: %s", errorCodeToString(rc));
    }

    // update statistics and return
    if (m_statListener != nullptr) {
        uint32_t compressedPayloadSize = compress ? (*msg)->getPayloadSizeBytes() : 0;
        m_statListener->onSendMsgStats(msgWriter->getPayloadSizeBytes(), compressedPayloadSize,
                                       (int)rc);
    }

    // NOTE: the message is saved in the pending requests of the message client
    return rc;
}

// TODO: open send callback option in message client, so that all backlog stuff happens in
// IO thread which is mostly idle waiting for IO. This way no lock is required, both for
// backlog and request pool.

ErrorCode MsgSender::recvResponse(MsgRequestData& requestData, Msg* msg, uint64_t timeoutMillis) {
    // wait for response
    Msg* response = nullptr;
    ErrorCode rc =
        m_msgClient->getRequestPool().waitPendingResponse(requestData, &response, timeoutMillis);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to transact message, failed waiting for response: %s",
                  errorCodeToString(rc));
        // reset tx flag in request, so that sender thread can now start resending this
        // request
        MsgRequest* request = m_msgClient->getRequestPool().getRequestData(msg);
        request->setFlags(request->getFlags() & ~COMMUTIL_REQUEST_FLAG_TX);
        return rc;
    }

    rc = handleResponse(msg, response);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR(
            "Failed to transact message, response handler indicates response is not "
            "successful: %s",
            errorCodeToString(rc));
        // obtain a new request id/index, resend will take place in the next resend round
        rc = renewRequestId(msg);
        if (rc == ErrorCode::E_OK) {
            LOG_ERROR(
                "Failed to renew request id for message (after message transaction "
                "failure)");
        }
        // leave it in the backlog...
    } else {
        m_backlog.removePendingRequest(msg);
    }

    // cleanup response
    freeMsg(response);

    return rc;
}

ErrorCode MsgSender::resendMsg(Msg* msg) {
    MsgRequestData requestData;
    return m_msgClient->sendMsg(msg, COMMUTIL_REQUEST_FLAG_REUSE, requestData);
}

/*void MsgSender::resendThread() {
    uint64_t nextResendTimeMillis = m_config.m_resendPeriodMillis;
    while (!shouldStopResend()) {
        // wait the full period until ordered to stop or that we are urged to resend
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_cv.wait_for(lock, std::chrono::milliseconds(nextResendTimeMillis), [this] {
                return m_stopResend || !m_pendingBackLog.empty() ||
                       m_msgClient->getRequestPool().getReadyRequestCount() > 0;
            });
            if (m_stopResend) {
                break;
            }

            // get out all pending back log messages and put in shipping back log queue, so
we can
            // release the lock quickly for more pending messages, otherwise we would be
holding the
            // lock while sending messages from the back log queue
            copyPendingBacklog();
        }

        // check for incoming and expired requests and remove from pending backlog.
        processPendingResponses();

        // see if we exceeded limit
        dropExcessBacklog();

        // now retry to send queued back log messages
        uint64_t minRequestTimeoutMillis = COMMUTIL_MSG_INFINITE_TIMEOUT;
        ErrorCode rc = resendShippingBacklog(false, &minRequestTimeoutMillis);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to resend shipping backlog: %s", errorCodeToString(rc));
        }

        if (minRequestTimeoutMillis == COMMUTIL_MSG_INFINITE_TIMEOUT) {
            nextResendTimeMillis = m_config.m_resendPeriodMillis;
        } else {
            nextResendTimeMillis = getCurrentTimeMillis() - minRequestTimeoutMillis;
        }
    }

    // one last attempt before shutdown
    if (m_config.m_shutdownTimeoutMillis > 0) {
        // copy newly added pending blacklog into shipping blacklog (one last time)
        copyPendingBacklog();

        // attempt resending failed messages
        // NOTE: theoretically, high resolution clock CAN go backwards, resulting in
negative time
        // diff, so we use instead steady clock here, which is guaranteed to be monotonic
        uint64_t start = getCurrentTimeMillis();
        uint64_t timePassedMillis = 0;
        do {
            // check for incoming and expired requests and remove from pending backlog
            processPendingResponses();

            // check if we are done
            if (m_backlog.isEmpty()) {
                break;
            }

            // try to resend backlog
            ErrorCode rc = resendShippingBacklog(true);
            if (rc != ErrorCode::E_OK) {
                LOG_ERROR("Failed to resend shipping backlog: %s", errorCodeToString(rc));
            }

            // wait for incoming request
            {
                std::unique_lock<std::mutex> lock(m_lock);
                m_cv.wait_for(
                    lock,
std::chrono::milliseconds(m_config.m_shutdownPollingTimeoutMillis), [this] { return
m_msgClient->getRequestPool().getReadyRequestCount() > 0; });
            }

            // compute time passed
            uint64_t end = getCurrentTimeMillis();
            // NOTE: due to usage of steady clock, time diff cannot be negative
            assert(end >= start);
            timePassedMillis = end - start;
        } while (!m_backlog.isEmpty() && timePassedMillis <=
m_config.m_shutdownTimeoutMillis);
    }

    // last chance
    processPendingResponses();

    // report any missing response
    uint32_t backlogCount = m_backlog.getEntryCount();
    if (backlogCount > 0) {
        LOG_ERROR("%s log target has failed to resend %u pending messages",
m_logTargetName.c_str(), backlogCount);
    }
}

void MsgSender::copyPendingBacklog() {
    while (!m_pendingBackLog.empty()) {
        Msg* request = m_pendingBackLog.front();
        MsgRequest* requestData = m_msgClient->getRequestPool().getRequestData(request);
        if (requestData == nullptr) {
            LOG_ERROR("Failed to add pending request to backlog, request data not found");
        } else {
            ErrorCode rc =
                m_backlog.addPendingRequest(request, requestData->getRequestTimeMillis());
            if (rc != ErrorCode::E_OK) {
                LOG_ERROR("Failed to add pending request to backlog: %s",
errorCodeToString(rc));
            }
        }
        m_pendingBackLog.pop_front();
    }
}*/

void MsgSender::processPendingResponses() {
    // check whether responses have already arrived or that request have already expired
    m_msgClient->getRequestPool().forEachRequest([this](MsgRequest& requestData) {
        // skip unused requests or requests in transaction
        if (requestData.isVacant() || (requestData.getFlags() & COMMUTIL_REQUEST_FLAG_TX)) {
            return;
        }

        // poll for response
        Msg* request = requestData.getRequest();
        Msg* response = nullptr;
        ErrorCode rc = requestData.waitResponse(&response, 0);
        if (rc == ErrorCode::E_OK) {
            requestData.clearResponse();
            if (response != nullptr) {
                rc = handleResponse(request, response);
                if (rc == ErrorCode::E_OK) {
                    // either there is no message processor or response is good
                    rc = m_backlog.removePendingRequest(request);
                    if (rc != ErrorCode::E_OK) {
                        LOG_ERROR("Failed to remove pending request %" PRIu64
                                  " after response arrived",
                                  requestData.getRequestId());
                    }
                }
                freeMsg(response);
            }
            freeMsg(request);
            return;
        }

        // if response did not arrive or something bad happened, we check for expiry
        if (rc != ErrorCode::E_TIMED_OUT ||
            getCurrentTimeMillis() - requestData.getRequestTimeMillis() >
                m_config.m_expireTimeoutMillis) {
            LOG_WARN("Request %" PRIu64 " expired and is now being discarded",
                     requestData.getRequestId());
            requestData.clearResponse();
            freeMsg(request);
        }
    });
}

ErrorCode MsgSender::handleResponse(Msg* request, Msg* response) {
    (void)request;
    return m_frameReader.readMsgFrame(m_msgClient->getConnectionDetails(), response);
}

void MsgSender::dropExcessBacklog() { m_backlog.pruneBacklog(m_config.m_backlogLimitBytes); }

ErrorCode MsgSender::resendShippingBacklog(bool duringShutdown /* = false */,
                                           uint64_t* minRequestTimeoutMillis /* = nullptr */) {
    LOG_TRACE("Attempting to resend %u pending messages", m_backlog.getEntryCount());
    ErrorCode rc = ErrorCode::E_OK;
    uint64_t currentTimeMillis = getCurrentTimeMillis();
    while (m_backlog.hasReadyResendRequest(currentTimeMillis)) {
        Msg* request = m_backlog.getReadyResendRequest();
        ErrorCode rc2 = resendRequest(request, minRequestTimeoutMillis);
        if (rc2 != ErrorCode::E_OK) {
            LOG_ERROR("Failed to resend request: %s", errorCodeToString(rc));
            if (rc == ErrorCode::E_OK) {
                rc = rc2;
            }
        } else {
            m_backlog.updateReadyResendRequest();
        }
    }
    /*m_backlog.forEachRequest([this, duringShutdown, minRequestTimeoutMillis, &rc](Msg* request) {
        if (duringShutdown || !m_stopResend.load(std::memory_order_relaxed)) {
            ErrorCode rc2 = resendRequest(request, minRequestTimeoutMillis);
            if (rc2 != ErrorCode::E_OK) {
                LOG_ERROR("Failed to resend request: %s", errorCodeToString(rc));
                if (rc == ErrorCode::E_OK) {
                    rc = rc2;
                }
            }
            return true;  // keep traversing pending requests
        }
        return false;  // stop traversing pending requests
    });*/
    return rc;
}

ErrorCode MsgSender::renewRequestId(Msg* msg) {
    ErrorCode rc = ErrorCode::E_OK;
    uint64_t requestId = m_msgClient->getRequestPool().fetchAddRequestId();
    uint32_t requestIndex = 0;
    MsgRequest* request = m_msgClient->getRequestPool().getVacantRequest(requestId, requestIndex);
    if (request == nullptr) {
        LOG_ERROR("Failed to renew request id, failed to allocate request slot");
        // communicate to caller that the message lacks a request slot
        msg->modifyHeader().setRequestId(COMMUTIL_MSG_INVALID_REQUEST_ID);
        rc = ErrorCode::E_RESOURCE_LIMIT;
    } else {
        msg->modifyHeader().setRequestId(requestId);
        msg->modifyHeader().setRequestIndex(requestIndex);
        request->setRequest(msg);
        // now it can be added to the backlog
    }
    return rc;
}

ErrorCode MsgSender::resendRequest(Msg* request, uint64_t* minRequestTimeoutMillis) {
    // It is possible that we do not have request slot allocated for the request (this
    // happens when the response handler indicates the response was bad)
    if (request->getHeader().getRequestId() == COMMUTIL_MSG_INVALID_REQUEST_ID) {
        ErrorCode rc = renewRequestId(request);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to renew request id for pending request message while resending");
            return rc;
        }
    }

    // send only if resend time arrived, update send time and calculate min time
    MsgRequest* requestData = m_msgClient->getRequestPool().getRequestData(request);
    if (requestData == nullptr) {
        LOG_ERROR("Cannot resend request %" PRIu64 ", request not found",
                  request->getHeader().getRequestId());
        return ErrorCode::E_INTERNAL_ERROR;
    }

    ErrorCode rc = ErrorCode::E_OK;
    uint64_t currentTimeMillis = getCurrentTimeMillis();
    uint64_t resendTimeMillis = requestData->getResendTime();
    bool shouldResend = false;
    if (resendTimeMillis == 0) {
        shouldResend = (currentTimeMillis - requestData->getRequestTimeMillis() >=
                        m_config.m_resendPeriodMillis);
    } else {
        shouldResend = (currentTimeMillis - resendTimeMillis >= m_config.m_resendPeriodMillis);
    }
    if (shouldResend) {
        rc = resendMsg(request);
        if (rc != ErrorCode::E_OK) {
            LOG_ERROR("Failed to resend request %" PRIu64 ": %s",
                      request->getHeader().getRequestId(), errorCodeToString(rc));
        }
        // regardless of what happened, we update the resend time
        requestData->updateResendTime();
        // TODO: update min heap in backlog
    }

    // update global resend minimum (even if no resend took place)
    if (minRequestTimeoutMillis != nullptr) {
        if (*minRequestTimeoutMillis == 0 ||
            requestData->getResendTime() < *minRequestTimeoutMillis) {
            *minRequestTimeoutMillis = requestData->getResendTime();
        }
    }

    return rc;
}

/*bool MsgSender::shouldStopResend() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_stopResend;
}*/

void MsgSender::stopResendTimer() {
    m_resendDone.store(false, std::memory_order_relaxed);
    m_stopResend.store(true, std::memory_order_relaxed);
    interruptLoop(m_dataLoop, nullptr);
}

void MsgSender::waitBacklogEmpty() {
    // TODO: make sure there is no division by zero
    uint64_t waitCount = m_config.m_shutdownTimeoutMillis / m_config.m_shutdownPollingTimeoutMillis;
    uint64_t iterCount = 0;
    while (!m_resendDone.load(std::memory_order_relaxed) && iterCount++ < waitCount) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(m_config.m_shutdownPollingTimeoutMillis));
    }
}

}  // namespace commutil