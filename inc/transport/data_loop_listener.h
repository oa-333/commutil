#ifndef __DATA_LOOP_LISTENER__
#define __DATA_LOOP_LISTENER__

#include <uv.h>

#include <unordered_map>

#include "comm_util_def.h"
#include "comm_util_err.h"
#include "comm_util_log.h"

namespace commutil {

/**
 * @brief A listener that receives notificaitons during the life-cycle of a libuv loop and allows
 * participating and affecting the loop life-cycle. The same listener can be used with mode than one
 * loop, although for the sake of simplicitly this is not advised.
 */
class COMMUTIL_API DataLoopListener {
public:
    virtual ~DataLoopListener() {}

    /**
     * @brief Notifies of I/O data loop starting (called before @ref uv_run()).
     * @param loop The loop that is to be executed.
     */
    virtual void onLoopStart(uv_loop_t* loop) = 0;

    /**
     * @brief Notifies of I/O data loop ending (called after @ref uv_run()).
     * @param loop The loop that finished executing.
     */
    virtual void onLoopEnd(uv_loop_t* loop) = 0;

    /**
     * @brief Notifies of data just being sent.
     * @param loop The loop that was used to send data.
     * @param userData User data passed when calling @ref DataClient::Write().
     */
    virtual void onLoopSend(uv_loop_t* loop, void* userData) = 0;

    /**
     * @brief Notifies of data just being received.
     * @param loop The loop that was used to receive data.
     */
    virtual void onLoopRecv(uv_loop_t* loop) = 0;

    /**
     * @brief Notifies of timeout expiring after calling @ref setUpTimer().
     * @param loop The loop associated with the timer.
     * @param timerId The timer id.
     */
    virtual void onLoopTimer(uv_loop_t* loop, uint64_t timerId) = 0;

    /**
     * @brief Notifies of loop interrupt triggered by calling @ref interruptLoop().
     * @param loop The interrupted loop.
     * @param userData The associated user data passed to @ref interruptLoop().
     */
    virtual void onLoopInterrupt(uv_loop_t* loop, void* userData) = 0;

protected:
    DataLoopListener() : m_nextTimerId(1) {}
    DataLoopListener(const DataLoopListener&) = delete;
    DataLoopListener(DataLoopListener&&) = delete;
    DataLoopListener& operator=(const DataLoopListener&) = delete;

    /**
     * @brief Starts a loop timer. This could be a one-shot timer or periodic timer. The wake-up
     * timeout is used for the first timeout notification. If no periodic timeout is specified then
     * this is a one-shot timer, and if a periodic timeout is specified as well, then this would
     * become a periodic timer. A one shot timer can call @ref setNextLoopTimer() to set up the
     * next timeout period, thus in effect implementing a periodic timer with a varying timeout
     * period.
     * @note This utility method can only be called from a loop context (i.e. any loop callback
     * onLoopXXX() above).
     * @param loop The I/O data loop.
     * @param[out] timerId The resulting timer id.
     * @param wakeUpMillis The timer timeout in milliseconds. In case of a periodic timer, this is
     * the timeout used for the first timer notification.
     * @param periodMillis Optional periodic timer timeout. For periodic timer use non-zero value.
     * This timeout period comes in effect starting from the second round of timeout notifications.
     * @return The operation's result.
     */
    ErrorCode startLoopTimer(uv_loop_t* loop, uint64_t& timerId, uint64_t wakeUpMillis,
                             uint64_t periodMillis = 0);

    /**
     * @brief Sets the next timeout for a one-shot loop timer.
     * @note This utility method can only be called from a loop context (i.e. any loop callback
     * onLoopXXX() above).
     * @param timerId The timer id.
     * @param wakeUpMillis The timer timeout in milliseconds.
     * @return The operation's result.
     */
    ErrorCode setNextLoopTimer(uint64_t timerId, uint64_t wakeUpMillis);

    /**
     * @brief Updates the timeout period for a periodic loop timer.
     * @note This utility method can only be called from a loop context (i.e. any loop callback
     * onLoopXXX() above).
     * @param timerId The timer id.
     * @param periodMillis The periodic timer timeout in milliseconds.
     * @return The operation's result.
     */
    ErrorCode setPeriodicLoopTimer(uint64_t timerId, uint64_t periodMillis);

    /**
     * @brief Stops a periodic timer.
     * @note This utility method can only be called from a loop context (i.e. any loop callback
     * onLoopXXX() above).
     * @param timerId The timer id.
     * @note A one-shot timer is not required to be explicitly stopped.
     */
    ErrorCode stopLoopTimer(uint64_t timerId);

    /**
     * @brief Interrupts a loop.
     * @note This utility method CAN be called from outside the loop context (it is actually
     * designed for being called from another context).
     * @param loop The loop to interrupt.
     * @param userData User data to be passed to @ref onLoopInterrupt() callback.
     * @return ErrorCode The operation's result.
     */
    ErrorCode interruptLoop(uv_loop_t* loop, void* userData);

private:
    // manage a set of timers per loop
    enum class TimerType : uint32_t { TT_NONE, TT_ONE_SHOT, TT_PERIODIC };
    struct TimerData {
        // the timer handle, also contains a loop pointer when initialized.
        uint64_t m_timerId;
        uv_timer_t m_timer;
        TimerType m_timerType;
        DataLoopListener* m_listener;  // required for callback rewiring
    };
    typedef std::unordered_map<uint64_t, TimerData> TimerMap;
    TimerMap m_timerMap;
    uint64_t m_nextTimerId;

    static void onTimerStatic(uv_timer_t* handle);

    static void onInterruptStatic(uv_async_t* handle);

    DECLARE_CLASS_LOGGER(Transport)
};

}  // namespace commutil

#endif  // __DATA_LOOP_LISTENER__