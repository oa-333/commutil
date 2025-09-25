#include "msg/msg_multiplexer.h"

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(MsgMultiplexer)

ErrorCode MsgMultiplexer::initialize(MsgAssembler* assembler, MsgListener* listener,
                                     uint32_t maxConnections, uint32_t concurrency) {
    m_assembler = assembler;
    m_listener = listener;
    m_concurrency = concurrency;
    m_connectionMap.resize(maxConnections, (uint32_t)-1);
    m_msgQueues.resize(concurrency);
    for (uint32_t i = 0; i < m_concurrency; ++i) {
        m_msgQueues[i] = new (std::nothrow) MsgQueue();
        if (m_msgQueues[i] == nullptr) {
            LOG_ERROR("Failed to allocate a message queue, out of memory");
            for (uint32_t j = 0; j < i; ++j) {
                delete m_msgQueues[j];
            }
            m_msgQueues.clear();
            return ErrorCode::E_NOMEM;
        }
    }

    return ErrorCode::E_OK;
}

ErrorCode MsgMultiplexer::terminate() {
    for (uint32_t i = 0; i < m_concurrency; ++i) {
        delete m_msgQueues[i];
    }
    m_msgQueues.clear();
    return ErrorCode::E_OK;
}

ErrorCode MsgMultiplexer::start() {
    for (uint32_t i = 0; i < m_concurrency; ++i) {
        m_msgThreads.emplace_back(std::thread(&MsgMultiplexer::msgTask, this, i));
    }
    return ErrorCode::E_OK;
}

ErrorCode MsgMultiplexer::stop() {
    ConnectionDetails connectionDetails;
    for (uint32_t i = 0; i < m_concurrency; ++i) {
        connectionDetails.setConnectionIndex(i);
        std::unique_lock<std::mutex> lock(m_msgQueues[i]->m_lock);
        m_msgQueues[i]->m_eventQueue.push_front({EventType::ET_STOP, connectionDetails, nullptr});
        m_msgQueues[i]->m_cv.notify_one();
    }
    for (uint32_t i = 0; i < m_concurrency; ++i) {
        m_msgThreads[i].join();
    }
    m_msgThreads.clear();
    return ErrorCode::E_OK;
}

bool MsgMultiplexer::onConnect(const ConnectionDetails& connectionDetails, int status) {
    // NOTE: cast is OK due to max connections limit
    if (status == 0) {
        assignQueueId((uint32_t)connectionDetails.getConnectionIndex());
        pushEvent({EventType::ET_CONNECT, connectionDetails, (Msg*)(int64_t)status});
    }
    return true;
}

void MsgMultiplexer::onDisconnect(const ConnectionDetails& connectionDetails) {
    pushEvent({EventType::ET_DISCONNECT, connectionDetails, nullptr});
}

MsgAction MsgMultiplexer::onMsg(const ConnectionDetails& connectionDetails, Msg* msg,
                                bool canSaveMsg) {
    if (!canSaveMsg) {
        LOG_ERROR("Cannot save message, internal error (invalid setup)");
        return MsgAction::MSG_CAN_DELETE;
    }
    pushEvent({EventType::ET_MSG, connectionDetails, msg});
    return MsgAction::MSG_CANNOT_DELETE;
}

void MsgMultiplexer::assignQueueId(uint32_t connectionIndex) {
    uint32_t queueId = m_nextQueue;
    m_nextQueue = (m_nextQueue + 1) % m_concurrency;
    m_connectionMap[connectionIndex] = queueId;
}

void MsgMultiplexer::pushEvent(const MsgEvent& event) {
    uint32_t queueId = m_connectionMap[event.m_connectionDetails.getConnectionIndex()];
    MsgQueue& msgQueue = *m_msgQueues[queueId];

    std::unique_lock<std::mutex> lock(msgQueue.m_lock);
    msgQueue.m_eventQueue.push_front(event);
    msgQueue.m_cv.notify_one();
}

void MsgMultiplexer::msgTask(uint32_t queueId) {
    MsgQueue& msgQueue = *m_msgQueues[queueId];

    bool done = false;
    while (!done) {
        // wait for incoming events
        std::unique_lock<std::mutex> lock(msgQueue.m_lock);
        msgQueue.m_cv.wait(lock, [&msgQueue]() { return !msgQueue.m_eventQueue.empty(); });

        // drain queue and dispatch
        while (!msgQueue.m_eventQueue.empty()) {
            MsgEvent& evt = msgQueue.m_eventQueue.back();
            done = handleEvt(evt);
            msgQueue.m_eventQueue.pop_back();
        }
    }
}

bool MsgMultiplexer::handleEvt(const MsgEvent& evt) {
    switch (evt.m_eventType) {
        case EventType::ET_CONNECT:
            m_listener->onConnect(evt.m_connectionDetails, (int)(int64_t)evt.m_msg);
            break;

        case EventType::ET_DISCONNECT:
            m_listener->onDisconnect(evt.m_connectionDetails);
            break;

        case EventType::ET_MSG:
            if (m_listener->onMsg(evt.m_connectionDetails, evt.m_msg, true) ==
                MsgAction::MSG_CAN_DELETE) {
                LOG_DEBUG("Freeing message buffer at %p", evt.m_msg);
                freeMsg(evt.m_msg);
            }
            break;

        case EventType::ET_STOP:
            return true;

        default:
            // drop unknow events
            break;
    }

    // don't stop
    return false;
}

}  // namespace commutil
