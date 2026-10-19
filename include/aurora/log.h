// Aurora3D — log.h
// Tiny printf-style logger. The library never throws across its public boundary,
// so errors surface here and through false/nullptr return values.
#pragma once

#include <cstdio>

namespace a3d {

enum class LogLevel { Trace, Debug, Info, Warn, Error, Fatal };

// Routes output to the debugger (OutputDebugString) and, when a console is
// attached, to stderr. Overridable so a host application can capture logs.
using LogSink = void (*)(LogLevel level, const char* message);
void SetLogSink(LogSink sink);
void SetMinLogLevel(LogLevel level);

void LogMessage(LogLevel level, const char* fmt, ...);

void LogTrace(const char* fmt, ...);
void LogDebug(const char* fmt, ...);
void LogInfo(const char* fmt, ...);
void LogWarn(const char* fmt, ...);
void LogError(const char* fmt, ...);

} // namespace a3d

// Assertion that logs and traps instead of unwinding. Aurora3D is exception-free.
#define A3D_ASSERT(cond, ...)                                                     \
    do {                                                                          \
        if (!(cond)) {                                                            \
            ::a3d::LogMessage(::a3d::LogLevel::Fatal,                             \
                              "assertion failed: %s  (%s:%d)", #cond, __FILE__, __LINE__); \
            __VA_ARGS__;                                                          \
        }                                                                         \
    } while (0)
