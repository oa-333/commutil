#include "transport/data_loop_listener.h"

#include <cassert>

#include "commutil_log_imp.h"

namespace commutil {

IMPLEMENT_CLASS_LOGGER(DataLoopListener)

ErrorCode DataLoopListener::startLoopTimer(uv_loop_t* loop, uint64_t& timerId,
                                           uint64_t wakeUpMillis, uint64_t periodMillis /* = 0*/) {
    timerId = m_nextTimerId++;
    std::pair<TimerMap::iterator, bool> resPair =
        m_timerMap.insert(TimerMap::value_type(timerId, TimerData()));
    if (!resPair.second) {
        LOG_ERROR("Internal error, duplicate timer id %" PRIu64, timerId);
        return ErrorCode::E_INTERNAL_ERROR;
    }

    TimerData& timerData = resPair.first->second;
    int res = uv_timer_init(loop, &timerData.m_timer);
    if (res < 0) {
        LOG_UV_ERROR(uv_timer_init, res, "Failed to initialize loop timer");
        m_timerMap.erase(resPair.first);
        return ErrorCode::E_SYSTEM_FAILURE;
    }

    timerData.m_timer.data = &timerData;
    timerData.m_listener = this;
    timerData.m_timerId = timerId;
    res = uv_timer_start(&timerData.m_timer, onTimerStatic, wakeUpMillis, periodMillis);
    if (res < 0) {
        LOG_UV_ERROR(uv_timer_start, res, "Failed to start loop timer");
        uv_close((uv_handle_t*)&timerData.m_timer, nullptr);
        m_timerMap.erase(resPair.first);
        return ErrorCode::E_SYSTEM_FAILURE;
    }

    timerData.m_timerType = (periodMillis == 0) ? TimerType::TT_ONE_SHOT : TimerType::TT_PERIODIC;
    return ErrorCode::E_OK;
}

ErrorCode DataLoopListener::setNextLoopTimer(uint64_t timerId, uint64_t wakeUpMillis) {
    // one shot timer does not require calling stop, but rather only calling start again
    TimerMap::iterator itr = m_timerMap.find(timerId);
    if (itr == m_timerMap.end()) {
        LOG_ERROR("Cannot set one-shot timer next timeout, timer %" PRIu64 " not found", timerId);
        return ErrorCode::E_NOT_FOUND;
    }
    TimerData& timerData = itr->second;
    if (timerData.m_timerType != TimerType::TT_ONE_SHOT) {
        LOG_ERROR("Cannot set one-shot timer next timeout, timer %" PRIu64
                  " is not a one-shot timer",
                  timerId);
        return ErrorCode::E_INVALID_ARGUMENT;
    }
    int res = uv_timer_start(&timerData.m_timer, onTimerStatic, wakeUpMillis, 0);
    if (res < 0) {
        LOG_UV_ERROR(uv_timer_start, res, "Failed to reset one-shot loop timer %" PRIu64, timerId);
        return ErrorCode::E_SYSTEM_FAILURE;
    }
    return ErrorCode::E_OK;
#if 0
    int res = uv_timer_stop(&m_timer);
    if (res < 0) {
        LOG_UV_ERROR(uv_timer_stop, res, "Failed to stop loop timer");
        return ErrorCode::E_SYSTEM_FAILURE;
    }

    res = uv_timer_start(&m_timer, onTimerStatic, wakeUpMillis, 0);
    if (res < 0) {
        LOG_UV_ERROR(uv_timer_start, res, "Failed to start loop timer");
        return ErrorCode::E_SYSTEM_FAILURE;
    }
    return ErrorCode::E_OK;
#endif
}

ErrorCode DataLoopListener::setPeriodicLoopTimer(uint64_t timerId, uint64_t periodMillis) {
    TimerMap::iterator itr = m_timerMap.find(timerId);
    if (itr == m_timerMap.end()) {
        LOG_ERROR("Cannot update periodic timer next timeout, timer %" PRIu64 " not found",
                  timerId);
        return ErrorCode::E_NOT_FOUND;
    }
    TimerData& timerData = itr->second;
    if (timerData.m_timerType != TimerType::TT_PERIODIC) {
        LOG_ERROR("Cannot update periodic timer next timeout, timer %" PRIu64
                  " is not a periodic timer",
                  timerId);
        return ErrorCode::E_INVALID_ARGUMENT;
    }
    uv_timer_set_repeat(&timerData.m_timer, periodMillis);
    return ErrorCode::E_OK;
}

ErrorCode DataLoopListener::stopLoopTimer(uint64_t timerId) {
    TimerMap::iterator itr = m_timerMap.find(timerId);
    if (itr == m_timerMap.end()) {
        LOG_ERROR("Cannot stop timer, timer %" PRIu64 " not found", timerId);
        return ErrorCode::E_NOT_FOUND;
    }
    TimerData& timerData = itr->second;
    if (timerData.m_timerType != TimerType::TT_PERIODIC) {
        LOG_ERROR("Cannot stop periodic timer next timeout, timer %" PRIu64
                  " is not a periodic timer",
                  timerId);
        return ErrorCode::E_INVALID_ARGUMENT;
    }

    int res = uv_timer_stop(&timerData.m_timer);
    if (res < 0) {
        LOG_UV_ERROR(uv_timer_stop, res, "Failed to stop loop timer %" PRIu64, timerId);
        return ErrorCode::E_SYSTEM_FAILURE;
    }

    uv_close((uv_handle_t*)&timerData.m_timer, nullptr);
    m_timerMap.erase(itr);
    return ErrorCode::E_OK;
}

ErrorCode DataLoopListener::interruptLoop(uv_loop_t* loop, void* userData) {
    uv_async_t* asyncReq = new (std::nothrow) uv_async_t();
    if (asyncReq == nullptr) {
        LOG_ERROR("Failed to allocate asynchronous request for loop interrupt");
        return ErrorCode::E_NOMEM;
    }

    std::pair<DataLoopListener*, void*>* listenerPair =
        new (std::nothrow) std::pair<DataLoopListener*, void*>(this, userData);
    if (listenerPair == nullptr) {
        LOG_ERROR("Failed to allocate asynchronous request user data for loop interrupt");
        delete asyncReq;
        return ErrorCode::E_NOMEM;
    }
    asyncReq->data = listenerPair;

    int res = uv_async_init(loop, asyncReq, onInterruptStatic);
    if (res < 0) {
        LOG_UV_ERROR(uv_async_init, res,
                     "Failed to interrupt loop, failed to initialize asynchronous request");
        delete listenerPair;
        delete asyncReq;
        return ErrorCode::E_SYSTEM_FAILURE;
    }

    res = uv_async_send(asyncReq);
    if (res < 0) {
        LOG_UV_ERROR(uv_async_send, res,
                     "Failed to interrupt loop, failed to send asynchronous request");
        delete listenerPair;
        delete asyncReq;
        return ErrorCode::E_SYSTEM_FAILURE;
    }

    return ErrorCode::E_OK;
}

void DataLoopListener::onTimerStatic(uv_timer_t* handle) {
    TimerData* timerData = (TimerData*)handle->data;
    DataLoopListener* listener = timerData->m_listener;
    listener->onLoopTimer(handle->loop, timerData->m_timerId);
}

void DataLoopListener::onInterruptStatic(uv_async_t* asyncReq) {
    std::pair<DataLoopListener*, void*>* listenerPair =
        (std::pair<DataLoopListener*, void*>*)asyncReq->data;
    listenerPair->first->onLoopInterrupt(asyncReq->loop, listenerPair->second);
    delete listenerPair;
    uv_close((uv_handle_t*)asyncReq,
             [](uv_handle_t* handle) -> void { delete (uv_async_t*)handle; });
}

}  // namespace commutil
