#ifndef __TRANSPORT_H__
#define __TRANSPORT_H__

#include <uv.h>

#include <thread>

#include "comm_util_err.h"

// common pipe definitions
#ifdef COMMUTIL_WINDOWS
#define COMMUTIL_PIPE_NAME_PREFIX "\\\\.\\pipe\\comm."
#else
#define COMMUTIL_PIPE_NAME_PREFIX "/tmp/comm.pipe."
#endif

namespace commutil {

extern void registerTransportLoggers();
extern void unregisterTransportLoggers();

typedef void (*TerminateCallback)(void* arg);

extern ErrorCode stopTransportLoop(uv_loop_t* loop, std::thread& ioThread, bool closeLoop = false,
                                   TerminateCallback cb = nullptr, void* data = nullptr);

}  // namespace commutil

#endif  // __TRANSPORT_H__