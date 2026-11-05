#pragma once

#include "ember/core/Log.h"

#include <cstdlib>

#if defined(_MSC_VER)
#define EMBER_DEBUG_BREAK() __debugbreak()
#else
#define EMBER_DEBUG_BREAK() __builtin_trap()
#endif

namespace ember::detail {

inline void reportAssert(const char* expression, const char* file, int line)
{
    log(LogLevel::Fatal, "Assertion failed: {} ({}:{})", expression, file, line);
}

} // namespace ember::detail

// EMBER_ASSERT: checks programmer mistakes. Compiled out of release builds.
#if defined(EMBER_DEBUG)
#define EMBER_ASSERT(cond)                                                \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ::ember::detail::reportAssert(#cond, __FILE__, __LINE__);     \
            EMBER_DEBUG_BREAK();                                          \
        }                                                                 \
    } while (0)
#else
#define EMBER_ASSERT(cond) ((void)0)
#endif

// EMBER_VERIFY: always checked. For conditions the engine cannot continue without.
#define EMBER_VERIFY(cond)                                                \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ::ember::detail::reportAssert(#cond, __FILE__, __LINE__);     \
            std::abort();                                                 \
        }                                                                 \
    } while (0)
