#include "comm_util.h"
#include "commutil_log_imp.h"
#include "commutil_tls.h"
#include "io/io_loggers.h"
#include "msg/msg_loggers.h"
#include "transport/transport.h"

namespace commutil {

static bool sIsInitialized = false;

// helper macro
#define EXEC_CHECK_OP(op)            \
    {                                \
        ErrorCode rc = op();         \
        if (rc != ErrorCode::E_OK) { \
            return rc;               \
        }                            \
    }

ErrorCode initCommUtil(LogHandler* logHandler /* = nullptr */,
                       LogSeverity severity /* = LS_FATAL */) {
    if (sIsInitialized) {
        return ErrorCode::E_INVALID_STATE;
    }
    // TLS and logger initialization is tricky, and must be done in parts
    initLog(logHandler, severity);
    initTls();
    EXEC_CHECK_OP(finishInitLog);

    registerIoLoggers();
    registerTransportLoggers();
    registerMsgLoggers();

    sIsInitialized = true;
    return ErrorCode::E_OK;
}

ErrorCode termCommUtil() {
    if (!sIsInitialized) {
        return ErrorCode::E_INVALID_STATE;
    }

    unregisterMsgLoggers();
    unregisterTransportLoggers();
    unregisterIoLoggers();

    EXEC_CHECK_OP(beginTermLog);
    termTls();
    EXEC_CHECK_OP(termLog);
    sIsInitialized = false;
    return ErrorCode::E_OK;
}

bool isCommUtilInitialized() { return sIsInitialized; }

}  // namespace commutil