#pragma once

#include <etl/delegate.h>
#include <cstdarg>

namespace libcomm {

enum class LogLevel {
    Error = 0,
    Warning = 1,
    Info = 2,
    Debug = 3
};

using LogSink = etl::delegate<void(LogLevel level, const char* tag, const char* format, va_list args)>;

void SetGlobalLogger(LogSink sink);
LogSink GetGlobalLogger();

}

#ifndef LIBCOMM_LOG_LEVEL
#define LIBCOMM_LOG_LEVEL 2
#endif

#define LOG_LEVEL_OFF 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4

#if LIBCOMM_LOG_LEVEL >= LOG_LEVEL_ERROR
#define LIBCOMM_LOG_ERROR(tag, format, ...) \
    do { \
        libcomm::LogSink logger = libcomm::GetGlobalLogger(); \
        if (logger) { \
            libcomm::detail::LogHelper(logger, libcomm::LogLevel::Error, tag, format, ##__VA_ARGS__); \
        } \
    } while(0)
#else
#define LIBCOMM_LOG_ERROR(tag, format, ...) ((void)0)
#endif

#if LIBCOMM_LOG_LEVEL >= LOG_LEVEL_WARN
#define LIBCOMM_LOG_WARN(tag, format, ...) \
    do { \
        libcomm::LogSink logger = libcomm::GetGlobalLogger(); \
        if (logger) { \
            libcomm::detail::LogHelper(logger, libcomm::LogLevel::Warning, tag, format, ##__VA_ARGS__); \
        } \
    } while(0)
#else
#define LIBCOMM_LOG_WARN(tag, format, ...) ((void)0)
#endif

#if LIBCOMM_LOG_LEVEL >= LOG_LEVEL_INFO
#define LIBCOMM_LOG_INFO(tag, format, ...) \
    do { \
        libcomm::LogSink logger = libcomm::GetGlobalLogger(); \
        if (logger) { \
            libcomm::detail::LogHelper(logger, libcomm::LogLevel::Info, tag, format, ##__VA_ARGS__); \
        } \
    } while(0)
#else
#define LIBCOMM_LOG_INFO(tag, format, ...) ((void)0)
#endif

#if LIBCOMM_LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LIBCOMM_LOG_DEBUG(tag, format, ...) \
    do { \
        libcomm::LogSink logger = libcomm::GetGlobalLogger(); \
        if (logger) { \
            libcomm::detail::LogHelper(logger, libcomm::LogLevel::Debug, tag, format, ##__VA_ARGS__); \
        } \
    } while(0)
#else
#define LIBCOMM_LOG_DEBUG(tag, format, ...) ((void)0)
#endif

namespace libcomm {
namespace detail {

inline void LogHelper(LogSink sink, LogLevel level, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    sink(level, tag, format, args);
    va_end(args);
}

}
}
