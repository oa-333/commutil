#include "io/io_loggers.h"

#include "io/segmented_input_stream.h"

namespace commutil {

void registerIoLoggers() { SegmentedInputStream::registerClassLogger(); }

void unregisterIoLoggers() { SegmentedInputStream::unregisterClassLogger(); }

}  // namespace commutil