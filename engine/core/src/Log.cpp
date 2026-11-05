#include "ember/core/Log.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace ember {

namespace {

std::mutex g_logMutex;
const auto g_startTime = std::chrono::steady_clock::now();

const char* levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "trace";
    case LogLevel::Info: return "info ";
    case LogLevel::Warn: return "warn ";
    case LogLevel::Error: return "error";
    case LogLevel::Fatal: return "FATAL";
    }
    return "?????";
}

} // namespace

void logMessage(LogLevel level, std::string_view message)
{
    const f64 seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - g_startTime).count();
    const std::string line = std::format("[{:9.3f}] [{}] {}\n", seconds, levelName(level), message);

    std::lock_guard lock(g_logMutex);
    std::FILE* out = level >= LogLevel::Warn ? stderr : stdout;
    std::fputs(line.c_str(), out);
    std::fflush(out);
#if defined(_WIN32)
    OutputDebugStringA(line.c_str());
#endif
}

} // namespace ember
