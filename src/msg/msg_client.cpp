#include "msg/msg_client.h"

#include <cinttypes>

#include "commutil_log_imp.h"
#include "io/fixed_output_stream.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgClient)

ErrorCode MsgClient::initialize(DataClient* dataClient, uint32_t maxConcurrentRequests,
                                MsgListener* msgListener /* = nullptr */) {
    if (maxConcurrentRequests > COMMUTIL_MAX_CONCURRENT_REQUESTS) {
        LOG_ERROR(
            "Failed to initialize message client, concurrent request count %u exceeds maximum "
            "allowed %u",
            maxConcurrentRequests, (uint32_t)COMMUTIL_MAX_CONCURRENT_REQUESTS);
        return ErrorCode::E_INVALID_ARGUMENT;
    }

    // we need to initialize the data client before the assembler, because we need the data
    // allocator of the data client
    ErrorCode rc = dataClient->initialize(&m_msgAssembler);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize message client, transport layer error: %s",
                  errorCodeToString(rc));
        return rc;
    }

    // now we can initialize the assembler
    rc = m_msgAssembler.initialize(1, dataClient->getByteOrder(), dataClient->getDataAllocator(),
                                   this);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to initialize message client, message assembler error: %s",
                  errorCodeToString(rc));
        return rc;
    }

    // save members
    m_dataClient = dataClient;
    m_requestPool.initialize(maxConcurrentRequests);
    m_listener = msgListener;
    return ErrorCode::E_OK;
}

ErrorCode MsgClient::terminate() {
    m_requestPool.terminate();
    ErrorCode rc = m_dataClient->terminate();
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to terminate message client, transport layer error: %s",
                  errorCodeToString(rc));
        return rc;
    }
    return m_msgAssembler.terminate();
}

ErrorCode MsgClient::start() {
    if (m_dataClient == nullptr) {
        LOG_ERROR("Cannot start message client, transport layer not initialized");
        return ErrorCode::E_INVALID_STATE;
    }
    return m_dataClient->start();
}

ErrorCode MsgClient::stop() {
    if (m_dataClient == nullptr) {
        LOG_ERROR("Cannot stop message client, transport layer not initialized");
        return ErrorCode::E_INVALID_STATE;
    }
    return m_dataClient->stop();
}

ErrorCode MsgClient::sendMsg(Msg* msg, uint32_t requestFlags /* = 0 */,
                             MsgRequestData* requestData /* = nullptr */) {
    // allocate unique request id if needed
    bool reuseRequest = (requestFlags & COMMUTIL_REQUEST_FLAG_REUSE);
    uint64_t requestId =
        reuseRequest ? msg->getHeader().getRequestId() : m_requestPool.fetchAddRequestId();

    // grab a slot in the request array at the client data
    uint32_t requestIndex = 0;
    MsgRequest* request = reuseRequest ? m_requestPool.getRequestData(msg)
                                       : m_requestPool.getVacantRequest(requestId, requestIndex);
    if (request == nullptr) {
        LOG_ERROR(reuseRequest ? "Cannot send request: ran out of vacant slots in client"
                               : "Cannot send request: reused request slot mismatch");
        return ErrorCode::E_RESOURCE_LIMIT;
    }
    if (requestData != nullptr) {
        requestData->m_requestId = requestId;
        requestData->m_requestIndex = requestIndex;
    }
    request->setFlags(requestFlags);
    if (!reuseRequest) {
        request->setRequest(msg);

        // update message header
        msg->modifyHeader().setRequestIndex(requestIndex);
        msg->modifyHeader().setRequestId(requestId);
    }

    // serialize message into buffer
    // NOTE: we must use a dynamic buffer, since the transport layer uses it asynchronously in
    // another thread
    // TODO: we can use a static buffer up to some size in the request struct, and a flag denoting
    // whether a static or dynamic buffer si being used
    uint32_t msgLength = msg->getHeader().getLength();
    char* buffer = new (std::nothrow) char[msgLength];
    if (buffer == nullptr) {
        LOG_ERROR("Failed to allocate %u bytes for message buffer", msgLength);
        request->clearRequest();
        return ErrorCode::E_NOMEM;
    }

    FixedOutputStream os(buffer, msgLength, m_dataClient->getByteOrder());
    ErrorCode rc = msg->serialize(os);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to serialize message: %s", errorCodeToString(rc));
        delete[] buffer;
        request->clearRequest();
        return rc;
    }

    // send serialized message through the transport layer
    // NOTE: actual sending is done in another thread, so the buffer must be copied
    // also note that the message object is passed as user data, so that message sender can get back
    // the message being sent in the data loop listener interface. this is really awkward and future
    // versions may as well fix this
    rc = m_dataClient->write(os.getBuffer(), os.getLength(), false, reuseRequest ? nullptr : msg);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to send message, transport layer error: %s", errorCodeToString(rc));
        delete[] buffer;
        request->clearRequest();
        return rc;
    }
    return ErrorCode::E_OK;
}

ErrorCode MsgClient::transactMsgResponse(
    Msg* msg, Msg** response, uint64_t timeoutMillis /* = COMMUTIL_MSG_INFINITE_TIMEOUT */) {
    // send message
    MsgRequestData requestData;
    ErrorCode rc = sendMsg(msg, COMMUTIL_REQUEST_FLAG_TX, &requestData);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to transact message, failed sending request: %s", errorCodeToString(rc));
        return rc;
    }

    // wait for response
    rc = m_requestPool.waitPendingResponse(requestData, response, timeoutMillis);
    if (rc != ErrorCode::E_OK) {
        LOG_ERROR("Failed to transact message, failed waiting for response: %s",
                  errorCodeToString(rc));
    }
    return rc;
}

ErrorCode MsgClient::isReady() {
    if (m_dataClient == nullptr) {
        LOG_ERROR("Cannot access message client, transport layer not initialized");
        return ErrorCode::E_INVALID_STATE;
    }
    if (m_dataClient->isReady()) {
        return ErrorCode::E_OK;
    }
    return ErrorCode::E_INPROGRESS;
}

ErrorCode MsgClient::waitReady() {
    if (m_dataClient == nullptr) {
        LOG_ERROR("Cannot access message client, transport layer not initialized");
        return ErrorCode::E_INVALID_STATE;
    }
    m_dataClient->waitReady();
    return ErrorCode::E_OK;
}

ErrorCode MsgClient::waitConnect(int* status) {
    if (m_dataClient == nullptr) {
        LOG_ERROR("Cannot access message client, transport layer not initialized");
        return ErrorCode::E_INVALID_STATE;
    }
    return m_connectData.waitConnect(status);
}

bool MsgClient::onConnect(const ConnectionDetails& connectionDetails, int status) {
    m_connectData.notifyConnect(connectionDetails, status);
    if (m_listener != nullptr) {
        return m_listener->onConnect(connectionDetails, status);
    }
    return true;
}

void MsgClient::onDisconnect(const ConnectionDetails& connectionDetails) {
    (void)connectionDetails;
    m_requestPool.abortPendingRequests();
    m_connectData.resetConnectData();
    if (m_listener != nullptr) {
        m_listener->onDisconnect(connectionDetails);
    }
}

MsgAction MsgClient::onMsg(const ConnectionDetails& connectionDetails, Msg* msg, bool canSaveMsg) {
    (void)connectionDetails;
    assert(canSaveMsg == true);
    uint32_t requestFlags = 0;
    ErrorCode rc = m_requestPool.setResponse(msg, requestFlags);

    // before dealing with any error, we first notify the listener if it is installed
    bool shouldFireNotification = (requestFlags & COMMUTIL_REQUEST_FLAG_TX) ? false : true;
    if (shouldFireNotification && m_listener != nullptr) {
        // NOTE: listener is not allowed to keep a reference to the message because it is kept in
        // the request pool and may be deleted at any time
        m_listener->onMsg(connectionDetails, msg, false);
    }

    // now decide what to do
    if (rc == ErrorCode::E_OK) {
        // let caller know incoming message CANNOT be deleted
        return MsgAction::MSG_CANNOT_DELETE;

        // NOTE: the request/reply pair is not removed from the request pool until the upper layer
        // decides to do so, normally that would happen either through MsgClient::transactMsg() or
        // by waiting for the response via MsgRequestPool::waitResponse()
    }

    // let caller know incoming message can be deleted
    LOG_ERROR("Failed to process incoming message, could not set response: %s",
              errorCodeToString(rc));
    return MsgAction::MSG_CAN_DELETE;
}

void MsgClient::ConnectData::resetConnectData() {
    std::unique_lock<std::mutex> lock(m_connectLock);
    m_connectArrived = 0;
    m_connectStatus = 0;
}

ErrorCode MsgClient::ConnectData::waitConnect(int* status) {
    std::unique_lock<std::mutex> lock(m_connectLock);
    m_connectCv.wait(lock, [this]() { return m_connectArrived != 0; });
    if (status != nullptr) {
        *status = m_connectStatus;
    }
    return ErrorCode::E_OK;
}

void MsgClient::ConnectData::notifyConnect(const ConnectionDetails& connectionDetails, int status) {
    std::unique_lock<std::mutex> lock(m_connectLock);
    m_connectArrived = 1;
    m_connectStatus = status;
    m_connectionDetails = connectionDetails;
    m_connectCv.notify_one();
}

}  // namespace commutil
