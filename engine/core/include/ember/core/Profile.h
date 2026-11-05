#pragma once

// Profiling macros. Backed by Tracy when EMBER_PROFILING is defined, empty otherwise.
// Connect the Tracy profiler GUI (same version as the vcpkg port) to see the timeline.

#if defined(EMBER_PROFILING)
#include <tracy/Tracy.hpp>

#define EMBER_PROFILE_SCOPE(name) ZoneScopedN(name) // time the rest of this scope under `name`
#define EMBER_PROFILE_FUNCTION() ZoneScoped         // time the rest of this function
#define EMBER_PROFILE_FRAME() FrameMark             // marks the end of a frame
#else
#define EMBER_PROFILE_SCOPE(name) ((void)0)
#define EMBER_PROFILE_FUNCTION() ((void)0)
#define EMBER_PROFILE_FRAME() ((void)0)
#endif
