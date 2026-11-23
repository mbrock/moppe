#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// psapi.h uses the Windows types above.
#include <psapi.h>
#undef near
#undef far
#else
#include <sys/resource.h>
#endif

namespace moppe::benchmark {
  // Megabytes at the most there has been.
  inline double peak_rss_mib () {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info {};
    if (!GetProcessMemoryInfo (GetCurrentProcess (), &info, sizeof (info)))
      return 0.0;
    return static_cast<double> (info.PeakWorkingSetSize) / 1048576.0;
#else
    rusage usage {};
    getrusage (RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return static_cast<double> (usage.ru_maxrss) / 1048576.0;
#else
    return static_cast<double> (usage.ru_maxrss) / 1024.0;
#endif
#endif
  }

}
