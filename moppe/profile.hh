#pragma once

// Tracy compiles these macros away completely in ordinary builds.  Keeping
// the dependency behind this small vocabulary lets Moppe's profiling map stay
// meaningful without spreading profiler-specific names through the game.
#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>

#define MOPPE_PROFILE_FRAME() FrameMark
#define MOPPE_PROFILE_THREAD(name) tracy::SetThreadName (name)
#define MOPPE_PROFILE_ZONE(name) ZoneScopedN (name)
#else
#define MOPPE_PROFILE_FRAME() ((void)0)
#define MOPPE_PROFILE_THREAD(name) ((void)0)
#define MOPPE_PROFILE_ZONE(name) ((void)0)
#endif
