#include "comm_util_def.h"

#ifdef COMMUTIL_MINGW
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cassert>
#include <cstdarg>
#include <cstring>
#include <vector>

// TODO: we need to refactor osutil as well for TLS, log, path parser, etc...

#include "commutil_common.h"
#include "commutil_log_imp.h"
#include "commutil_tls.h"
#include "log_buffer.h"

#define MAX_LOGGERS ((size_t)1024)

namespace commutil {

// log severity string constants
static const char* sLogSeverityStr[] = {"FATAL", "ERROR", "WARN",  "NOTICE",
                                        "INFO",  "TRACE", "DEBUG", "DIAG"};
static const size_t sLogSeverityCount = sizeof(sLogSeverityStr) / sizeof(sLogSeverityStr[0]);

struct LogData {
    const Logger* m_logger;
    LogSeverity m_severity;
    LogBuffer m_buffer;
    LogData* m_next;

    LogData(LogData* next = nullptr) : m_logger(nullptr), m_severity(LS_INFO), m_next(next) {
        m_buffer.reset();
    }

    inline void reset(const Logger* logger = nullptr, LogSeverity severity = LS_INFO) {
        m_logger = logger;
        m_severity = severity;
        m_buffer.reset();
    }
};

class DefaultLogHandler final : public LogHandler {
public:
    DefaultLogHandler() {}
    DefaultLogHandler(const DefaultLogHandler&) = delete;
    DefaultLogHandler(DefaultLogHandler&&) = delete;
    DefaultLogHandler& operator=(DefaultLogHandler&) = delete;
    ~DefaultLogHandler() final {}

    LogSeverity onRegisterLogger(LogSeverity severity, const char* /* loggerName */,
                                 size_t /* loggerId */) override {
        return severity;
    }

    void onUnregisterLogger(size_t /* loggerId */) override {}

    void onMsg(LogSeverity severity, size_t /* loggerId */, const char* loggerName,
               const char* msg) override {
        fprintf(stderr, "[%s] <%s> %s\n", logSeverityToString(severity), loggerName, msg);
    }

    void onThreadStart(const char* threadName) override { (void)threadName; }
};

// use TLS instead of thread_local due to MinGW bug (static thread_local variable destruction
// sometimes takes place twice, not clear under which conditions)
static TlsKey sLogDataKey = COMMUTIL_INVALID_TLS_KEY;

inline LogData* allocLogData() { return new (std::nothrow) LogData(); }

inline void freeLogData(void* data) {
    LogData* logData = (LogData*)data;
    if (logData != nullptr) {
        delete logData;
    }
}

static LogData* getOrCreateTlsLogData() {
    LogData* logData = (LogData*)getTls(sLogDataKey);
    if (logData == nullptr) {
        logData = allocLogData();
        if (logData == nullptr) {
            fprintf(stderr, "Failed to allocate thread-local log buffer\n");
            return nullptr;
        }
        if (!setTls(sLogDataKey, logData)) {
            fprintf(stderr, "Failed to set thread-local log buffer\n");
            freeLogData(logData);
            return nullptr;
        }
    }
    return logData;
}

// due to MinGW issues with static/thread-local destruction (it crashes sometimes), we use instead
// thread-local pointers
static thread_local LogData* sLogDataHead = nullptr;
static thread_local LogData* sLogData = nullptr;

static DefaultLogHandler sDefaultLogHandler;
static LogHandler* sLogHandler = nullptr;
static LogSeverity sLogSeverity = LS_INFO;
static std::vector<Logger*> sLoggers;

inline void ensureLogDataExists() {
    if (sLogData == nullptr) {
        // create on-demand on a per-thread basis
        sLogData = getOrCreateTlsLogData();
        assert(sLogDataHead == nullptr);
        sLogDataHead = sLogData;
    }
}

inline ErrorCode createLogDataKey() {
    if (sLogDataKey != COMMUTIL_INVALID_TLS_KEY) {
        fprintf(stderr, "Cannot create record builder TLS key, already created\n");
        return ErrorCode::E_INVALID_STATE;
    }
    if (!createTls(sLogDataKey, freeLogData)) {
        return ErrorCode::E_SYSTEM_FAILURE;
    }
    return ErrorCode::E_OK;
}

inline ErrorCode destroyLogDataKey() {
    if (sLogDataKey == COMMUTIL_INVALID_TLS_KEY) {
        // silently ignore the request
        return ErrorCode::E_OK;
    }
    if (!destroyTls(sLogDataKey)) {
        return ErrorCode::E_SYSTEM_FAILURE;
    }
    sLogDataKey = COMMUTIL_INVALID_TLS_KEY;
    return ErrorCode::E_OK;
}

static LogData* getLogData() {
    ensureLogDataExists();
    // we cannot afford a failure here, this is fatal
    assert(sLogData != nullptr);
    return sLogData;
}

static LogData* pushLogData() {
    LogData* logData = new (std::nothrow) LogData(sLogData);
    if (logData != nullptr) {
        sLogData = logData;
    }
    return sLogData;
}

static void popLogData() {
    if (sLogData != sLogDataHead) {
        LogData* next = sLogData->m_next;
        delete sLogData;
        sLogData = next;
    }
}

void initLog(LogHandler* logHandler, LogSeverity severity) {
    if (logHandler == COMMUTIL_DEFAULT_LOG_HANDLER) {
        sLogHandler = &sDefaultLogHandler;
    } else {
        sLogHandler = logHandler;
    }
    sLogSeverity = severity;
}

ErrorCode finishInitLog() { return createLogDataKey(); }

ErrorCode beginTermLog() { return destroyLogDataKey(); }

ErrorCode termLog() {
    // NOTE: it is expected that at this point there are no log data lists at any thread
    // the recommended behavior is to arrive here after all application threads have terminated,
    // such that in each thread the TLS destructor was called
    return destroyLogDataKey();
}

void setLogSeverity(LogSeverity severity) { sLogSeverity = severity; }

void setLoggerSeverity(size_t loggerId, LogSeverity severity) {
    if (loggerId < sLoggers.size() && sLoggers[loggerId] != nullptr) {
        sLoggers[loggerId]->m_severity = severity;
    }
}

COMMUTIL_API const char* logSeverityToString(LogSeverity severity) {
    if (severity < sLogSeverityCount) {
        return sLogSeverityStr[severity];
    }
    return "N/A";
}

/** @brief Queries whether a multi-part log message is being constructed. */
static bool isLogging(LogData* logData) { return logData->m_buffer.getOffset() > 0; }

static void appendMsgV(LogData* logData, const char* fmt, va_list args) {
    va_list apCopy;
    va_copy(apCopy, args);
    logData->m_buffer.appendV(fmt, args);
    va_end(apCopy);
}

static void appendMsg(LogData* logData, const char* msg) { logData->m_buffer.append(msg); }

static void finishLogData(LogData* logData) {
    if (isLogging(logData)) {
        // NOTE: new line character at the end of the line is added by log handler if at all
        logData->m_buffer.finalize();
        const char* formattedLogMsg = logData->m_buffer.getRef();
        if (sLogHandler != nullptr) {
            sLogHandler->onMsg(logData->m_severity, logData->m_logger->m_loggerId,
                               logData->m_logger->m_loggerName.c_str(), formattedLogMsg);
        }
        logData->reset();
        popLogData();
    } else {
        fprintf(stderr, "attempt to end log message without start-log being issued first\n");
    }
}

void registerLogger(Logger& logger, const char* loggerName) {
    if (sLoggers.size() > MAX_LOGGERS) {
        fprintf(stderr, "Cannot register logger %s, reached limit %zu\n", loggerName, MAX_LOGGERS);
        logger.m_loggerId = COMMUTIL_INVALID_LOGGER_ID;
        return;
    }
    logger.m_loggerId = sLoggers.size();
    sLoggers.push_back(&logger);
    logger.m_loggerName = loggerName;
    if (sLogHandler != nullptr) {
        logger.m_severity =
            sLogHandler->onRegisterLogger(sLogSeverity, loggerName, logger.m_loggerId);
    }
}

void unregisterLogger(Logger& logger) {
    if (logger.m_loggerId == COMMUTIL_INVALID_LOGGER_ID) {
        // silently ignore
        return;
    }
    if (sLogHandler != nullptr) {
        sLogHandler->onUnregisterLogger(logger.m_loggerId);
    }
    if (logger.m_loggerId < sLoggers.size()) {
        sLoggers[logger.m_loggerId] = nullptr;

        // find largest suffix of removed loggers
        size_t maxLogTargetId = sLoggers.size() - 1;
        bool done = false;
        while (!done) {
            if (sLoggers[maxLogTargetId] != nullptr) {
                sLoggers.resize(maxLogTargetId + 1);
                done = true;
            } else if (maxLogTargetId == 0) {
                // last one is also null
                sLoggers.clear();
                done = true;
            } else {
                --maxLogTargetId;
            }
        }
    }
}

void notifyThreadStart(const char* threadName) {
    if (sLogHandler != nullptr) {
        sLogHandler->onThreadStart(threadName);
    }
}

bool canLog(const Logger& logger, LogSeverity severity) {
    return severity <= sLogSeverity || severity <= logger.m_severity;
}

void logMsg(const Logger& logger, LogSeverity severity, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    LogData* logData = getLogData();
    if (isLogging(logData)) {
        logData = pushLogData();
    }
    logData->reset(&logger, severity);
    appendMsgV(logData, fmt, args);
    finishLogData(logData);

    va_end(args);
}

void startLog(const Logger& logger, LogSeverity severity, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogData* logData = getLogData();
    if (isLogging(logData)) {
        logData = pushLogData();
    }
    logData->reset(&logger, severity);
    appendMsgV(logData, fmt, args);
    va_end(args);
}

void appendLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogData* logData = getLogData();
    if (isLogging(logData)) {
        logData = pushLogData();
    }
    if (isLogging(logData)) {
        appendMsgV(logData, fmt, args);
    } else {
        fprintf(stderr, "Attempt to append log message without start-log being issued first: ");
        vfprintf(stderr, fmt, args);
        fputs("\n", stderr);
        fflush(stderr);
    }
    va_end(args);
}

void appendLogNoFormat(const char* msg) {
    LogData* logData = getLogData();
    if (isLogging(logData)) {
        logData = pushLogData();
    }
    if (isLogging(logData)) {
        appendMsg(logData, msg);
    } else {
        fprintf(stderr,
                "Attempt to append unformatted log message without start-log being issued first: ");
        fputs(msg, stderr);
        fputs("\n", stderr);
        fflush(stderr);
    }
}

void finishLog() { finishLogData(getLogData()); }

const char* sysErrorToStr(int sysErrorCode) {
    const int BUF_LEN = 256;
    static thread_local char buf[BUF_LEN];
#ifdef COMMUTIL_WINDOWS
    (void)strerror_s(buf, BUF_LEN, sysErrorCode);
    return buf;
#else
#if (_POSIX_C_SOURCE >= 200112L) && !_GNU_SOURCE
    (void)strerror_r(sysErrorCode, buf, BUF_LEN);
    return buf;
#else
    return strerror_r(sysErrorCode, buf, BUF_LEN);
#endif
#endif
}

#ifdef COMMUTIL_WINDOWS
char* win32SysErrorToStr(unsigned long sysErrorCode) {
    LPSTR messageBuffer = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, sysErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0,
        nullptr);
    return messageBuffer;
}

void win32FreeErrorStr(char* errStr) { LocalFree(errStr); }

#endif

}  // namespace commutil
