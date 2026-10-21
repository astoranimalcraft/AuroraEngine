// NexiumEngine — log.cpp
#include "nexium/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace n3d {
namespace {

LogSink gSink = nullptr;
LogLevel gMinLevel = LogLevel::Info;

const char* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "?????";
}

void DefaultSink(LogLevel level, const char* message) {
    char line[2048];
    std::snprintf(line, sizeof(line), "[nexium] %s %s\n", LevelName(level), message);
    OutputDebugStringA(line);
    if (GetConsoleWindow() != nullptr) {
        std::fputs(line, stderr);
        std::fflush(stderr);
    }
}

} // namespace

void SetLogSink(LogSink sink) { gSink = sink; }
void SetMinLogLevel(LogLevel level) { gMinLevel = level; }

void LogMessage(LogLevel level, const char* fmt, ...) {
    if (level < gMinLevel && level != LogLevel::Fatal) return;

    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    (gSink ? gSink : DefaultSink)(level, buffer);
}

#define N3D_DEFINE_LOG_FN(name, level)              \
    void name(const char* fmt, ...) {               \
        if (level < gMinLevel) return;              \
        char buffer[4096];                          \
        va_list args;                               \
        va_start(args, fmt);                        \
        std::vsnprintf(buffer, sizeof(buffer), fmt, args); \
        va_end(args);                               \
        LogMessage(level, "%s", buffer);            \
    }

N3D_DEFINE_LOG_FN(LogTrace, LogLevel::Trace)
N3D_DEFINE_LOG_FN(LogDebug, LogLevel::Debug)
N3D_DEFINE_LOG_FN(LogInfo,  LogLevel::Info)
N3D_DEFINE_LOG_FN(LogWarn,  LogLevel::Warn)
N3D_DEFINE_LOG_FN(LogError, LogLevel::Error)

#undef N3D_DEFINE_LOG_FN

} // namespace n3d
