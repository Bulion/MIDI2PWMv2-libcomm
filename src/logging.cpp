#include "libcomm/logging.h"

namespace libcomm {

static LogSink globalLogger;

void SetGlobalLogger(LogSink sink) {
    globalLogger = sink;
}

LogSink GetGlobalLogger() {
    return globalLogger;
}

}
