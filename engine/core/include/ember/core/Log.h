#pragma once

#include "ember/core/Types.h"

#include <format>
#include <string_view>
#include <utility>

namespace ember {

enum class LogLevel : u8 { Trace, Info, Warn, Error, Fatal };

// Thread-safe. Writes to stdout/stderr and, on Windows, the debugger output window.
void logMessage(LogLevel level, std::string_view message);

template <typename... Args>
void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
{
    logMessage(level, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace ember

#define EMBER_LOG_TRACE(...) ::ember::log(::ember::LogLevel::Trace, __VA_ARGS__)
#define EMBER_LOG_INFO(...) ::ember::log(::ember::LogLevel::Info, __VA_ARGS__)
#define EMBER_LOG_WARN(...) ::ember::log(::ember::LogLevel::Warn, __VA_ARGS__)
#define EMBER_LOG_ERROR(...) ::ember::log(::ember::LogLevel::Error, __VA_ARGS__)
#define EMBER_LOG_FATAL(...) ::ember::log(::ember::LogLevel::Fatal, __VA_ARGS__)
