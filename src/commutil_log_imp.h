#ifndef __COMMUTIL_LOG_IMP_H__
#define __COMMUTIL_LOG_IMP_H__

#include <uv.h>

#include <cinttypes>
#include <string>

#include "comm_util_def.h"
#include "comm_util_err.h"
#include "comm_util_log.h"

namespace commutil {

#define COMMUTIL_INVALID_LOGGER_ID ((size_t)-1)

struct Logger {
    std::string m_loggerName;
    size_t m_loggerId;
    LogSeverity m_severity;
};

extern void initLog(LogHandler* logHandler, LogSeverity severity);

// call this after TLS is initialized
extern ErrorCode finishInitLog();

// call this before TLS is terminated
extern ErrorCode beginTermLog();

extern ErrorCode termLog();

extern void registerLogger(Logger& logger, const char* loggerName);

extern void unregisterLogger(Logger& logger);

extern void notifyThreadStart(const char* threadName);

extern bool canLog(const Logger& logger, LogSeverity severity);

extern void logMsg(const Logger& logger, LogSeverity severity, const char* fmt, ...);

extern void startLog(const Logger& logger, LogSeverity severity, const char* fmt, ...);

extern void appendLog(const char* fmt, ...);

extern void appendLogNoFormat(const char* msg);

extern void finishLog();

extern const char* sysErrorToStr(int sysErrorCode);

/** @def Helper macro for implementing class logger (for internal use only). */
#define IMPLEMENT_CLASS_LOGGER(ClassName)                                          \
    static Logger sLogger;                                                         \
    void ClassName::registerClassLogger() { registerLogger(sLogger, #ClassName); } \
    void ClassName::unregisterClassLogger() { unregisterLogger(sLogger); }

#ifdef COMMUTIL_WINDOWS
extern char* win32SysErrorToStr(unsigned long sysErrorCode);
extern void win32FreeErrorStr(char* errStr);
#endif

}  // namespace commutil

// general logging macro
#define LOG(severity, fmt, ...)                        \
    if (canLog(sLogger, severity)) {                   \
        logMsg(sLogger, severity, fmt, ##__VA_ARGS__); \
    }

// per-severity logging macros
#define LOG_FATAL(fmt, ...) LOG(LS_FATAL, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG(LS_ERROR, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG(LS_WARN, fmt, ##__VA_ARGS__)
#define LOG_NOTICE(fmt, ...) LOG(LS_NOTICE, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG(LS_INFO, fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) LOG(LS_TRACE, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG(LS_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_DIAG(fmt, ...) LOG(LS_DIAG, fmt, ##__VA_ARGS__)

// multi-part logging macros
#define LOG_BEGIN(severity, fmt, ...)           \
    if (canLog(severity)) {                     \
        startLog(severity, fmt, ##__VA_ARGS__); \
    }

#define LOG_APPEND(fmt, ...) appendLog(fmt, ##__VA_ARGS__)

#define LOG_APPEND_NF(msg) appendLogNoFormat(msg)

#define LOG_END(logger) finishLog(logger)

// system error logging macros
#define LOG_SYS_ERROR_NUM(syscall, sysErr, fmt, ...)                                        \
    LOG_ERROR("System call " #syscall "() failed: %d (%s)", sysErr, sysErrorToStr(sysErr)); \
    LOG_ERROR(fmt, ##__VA_ARGS__);

#define LOG_SYS_ERROR(syscall, fmt, ...)                             \
    {                                                                \
        int localSysErr = errno;                                     \
        LOG_SYS_ERROR_NUM(syscall, localSysErr, fmt, ##__VA_ARGS__); \
    }

// Windows system error logging macros
#ifdef COMMUTIL_WINDOWS
#define LOG_WIN32_ERROR_NUM(syscall, sysErr, fmt, ...)                                    \
    {                                                                                     \
        char* errStr = win32SysErrorToStr(sysErr);                                        \
        LOG_ERROR("Windows system call " #syscall "() failed: %lu (%s)", sysErr, errStr); \
        win32FreeErrorStr(errStr);                                                        \
        LOG_ERROR(fmt, ##__VA_ARGS__);                                                    \
    }

#define LOG_WIN32_ERROR(syscall, fmt, ...)                             \
    {                                                                  \
        DWORD localSysErr = ::GetLastError();                          \
        LOG_WIN32_ERROR_NUM(syscall, localSysErr, fmt, ##__VA_ARGS__); \
    }

#endif  // COMMUTIL_WINDOWS

/** @brief Converts libuv error code to string. */
inline std::string uvErrorToString(int res) {
    char buf[64];
    return uv_strerror_r(res, buf, 64);
}

/** @def Helper macro for converting libuv error code to string */
#define UV_ERROR_STR(res) uvErrorToString(res).c_str()

/** @brief Reports libuv error to log. */
#define LOG_UV_ERROR(uvcall, res, fmt, ...)                                            \
    {                                                                                  \
        LOG_ERROR("libuv call " #uvcall "() failed: %d (%s)", res, UV_ERROR_STR(res)); \
        LOG_ERROR(fmt, ##__VA_ARGS__);                                                 \
    }

#endif  // __COMMUTIL_LOG_IMP_H__