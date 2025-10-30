#include "example_logger.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

static std::mutex logMutex;

void ExampleLoggerCallback(libcomm::LogLevel level, const char* tag, const char* format, va_list args)
{
    std::lock_guard<std::mutex> lock(logMutex);

    const char* levelStr = "";
    switch (level) {
        case libcomm::LogLevel::Error:
            levelStr = "ERROR";
            break;
        case libcomm::LogLevel::Warning:
            levelStr = "WARN ";
            break;
        case libcomm::LogLevel::Info:
            levelStr = "INFO ";
            break;
        case libcomm::LogLevel::Debug:
            levelStr = "DEBUG";
            break;
    }

    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    char timeBuffer[32];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", localTime);

    std::fprintf(stderr, "[%s] [%s] [%s] ", timeBuffer, levelStr, tag);
    std::vfprintf(stderr, format, args);
    std::fprintf(stderr, "\n");
}

void InstallExampleLogger()
{
    libcomm::SetGlobalLogger(ExampleLoggerCallback);
}
