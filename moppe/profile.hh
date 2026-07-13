#pragma once

#include <cstdint>

// Tracy compiles these macros away completely in ordinary builds.  Keeping
// the dependency behind this small vocabulary lets Moppe's profiling map stay
// meaningful without spreading profiler-specific names through the game.
#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>

#include <chrono>
#include <thread>

namespace moppe::profile {
  inline void wait_for_profiler () {
    while (!TracyIsConnected)
      std::this_thread::sleep_for (std::chrono::milliseconds (1));
  }
}

#define MOPPE_PROFILE_FRAME() FrameMark
#define MOPPE_PROFILE_PLOT(name, value)                                        \
  TracyPlot (name, static_cast<int64_t> (value))
#define MOPPE_PROFILE_THREAD(name) tracy::SetThreadName (name)
#define MOPPE_PROFILE_ZONE(name) ZoneScopedN (name)
#define MOPPE_PROFILE_NAMED_ZONE(variable, name)                               \
  ZoneNamedN (variable, name, true)
#define MOPPE_PROFILE_WAIT() ::moppe::profile::wait_for_profiler ()
#else
#define MOPPE_PROFILE_FRAME() ((void)0)
#define MOPPE_PROFILE_PLOT(name, value) ((void)0)
#define MOPPE_PROFILE_THREAD(name) ((void)0)
#define MOPPE_PROFILE_ZONE(name) ((void)0)
#define MOPPE_PROFILE_NAMED_ZONE(variable, name) ((void)0)
#define MOPPE_PROFILE_WAIT() ((void)0)
#endif
