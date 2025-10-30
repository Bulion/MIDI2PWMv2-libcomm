#pragma once

#include "libcomm/logging.h"

void ExampleLoggerCallback(libcomm::LogLevel level, const char* tag, const char* format, va_list args);

void InstallExampleLogger();
