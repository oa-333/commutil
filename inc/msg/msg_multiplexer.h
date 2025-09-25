#ifndef __MSG_MULTIPLEXER_H__
#define __MSG_MULTIPLEXER_H__

#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#include "comm_util_log.h"
#include "msg/msg_assembler.h"

namespace commutil {

/**
 * @brief A message multiplexer utility class that allows increasing or controlling the message
 * handling concurrency. All incoming messages are assigned to a predefined amount of queues, where
 * each queue is handled by a single worker thread. The concurrency level determines the number of
 * queues and worker threads. This allows increasing concurrency level of message handling in case
 * of high burst of incoming messages through just a few communication channels, or conversely,
 * reduce the level of concurrency when there is a large amount of communication channels.
 */
class COMMUTIL_API MsgMultiplexer : public MsgListener {
public:
    MsgMultiplexer()
        : m_assembler(nullptr), m_listener(nullptr), m_concurrency(0), m_nextQueue(0) {}
    MsgMultiplexer(const MsgMultiplexer&) = delete;
    MsgMultiplexer(MsgMultiplexer&&) = delete;
    MsgMultiplexer& operator=(const MsgMultiplexer&) = delete;
    ~MsgMultiplexer() override {}

    /**
     * @brief Initializes the message multiplexer.
     * @param assembler The message assembler that was used for assembling incoming messages. Used
     * only for deallocating messages.
     * @param listener The message dispatch destination.
     * @param maxConnections The maximum number of connections in the underlying communication
     * infrastructure.
     * @param concurrency The level of concurrency to enforce. Determines the number of worker
     * threads.
     * @return The operation's result.
     */
    ErrorCode initialize(MsgAssembler* assembler, MsgListener* listener, uint32_t maxConnections,
                         uint32_t concurrency);

    /** @brief Terminates the multiplexer. */
    ErrorCode terminate();

    /** @brief Starts the message multiplexer's worker threads running. */
    ErrorCode start();

    /** @brief Stops the message multiplexer. */
    ErrorCode stop();

    /**
     * @brief Notify incoming connected.
     * @param connectionDetails The connection details.
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

private:
    MsgAssembler* m_assembler;
    MsgListener* m_listener;
    uint32_t m_concurrency;
    uint32_t m_nextQueue;

    enum class EventType : uint32_t { ET_CONNECT, ET_DISCONNECT, ET_MSG, ET_STOP };
    struct MsgEvent {
        EventType m_eventType;
        ConnectionDetails m_connectionDetails;
        Msg* m_msg;

        MsgEvent(EventType eventType = EventType::ET_STOP,
                 const ConnectionDetails& connectionDetails = ConnectionDetails(),
                 Msg* msg = nullptr)
            : m_eventType(eventType), m_connectionDetails(connectionDetails), m_msg(msg) {}
        MsgEvent(const MsgEvent& msgEvent) = default;
        MsgEvent(MsgEvent&& msgEvent) = default;
        MsgEvent& operator=(const MsgEvent&) = default;
        ~MsgEvent() {}
    };

    struct MsgQueue {
        // NOTE: we must use a lock since the data server may have several IO tasks
        std::list<MsgEvent> m_eventQueue;
        std::mutex m_lock;
        std::condition_variable m_cv;

        MsgQueue() {}
        MsgQueue(const MsgQueue& msgQueue) = delete;
        MsgQueue(MsgQueue&&) = delete;
        MsgQueue& operator=(const MsgQueue&) = delete;
        ~MsgQueue() {}
    };

    std::vector<uint32_t> m_connectionMap;
    std::vector<std::thread> m_msgThreads;
    std::vector<MsgQueue*> m_msgQueues;

    void assignQueueId(uint32_t connectionIndex);
    void pushEvent(const MsgEvent& event);

    void msgTask(uint32_t queueId);
    bool handleEvt(const MsgEvent& evt);

    DECLARE_CLASS_LOGGER(Msg)
};

}  // namespace commutil

#endif  // __MSG_MULTIPLEXER_H__