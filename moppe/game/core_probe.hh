#ifndef MOPPE_GAME_CORE_PROBE_HH
#define MOPPE_GAME_CORE_PROBE_HH

// The core probe: the game's own world generation and simulation, run
// headlessly and measured, for bringing moppe up on a new platform before
// it has a renderer (the Xbox, first). It generates the default world from
// scratch, reads it back from the finished-world cache, walks and rides the
// session over it, and finally runs the whole application shell against a
// recording renderer. The host supplies the platform's view of memory and
// where the report goes.

#include <cstdint>
#include <functional>
#include <string>

namespace moppe::game {
  struct CoreProbeMemory {
    std::uint64_t current = 0; // bytes the process has committed now
    std::uint64_t peak = 0;    // the most it has committed, or zero
    std::uint64_t limit = 0;   // what the platform allows it, or zero
  };

  struct CoreProbeHost {
    // Runs platform::async completions when the probe owns the main
    // thread; empty when another thread pumps them.
    std::function<void ()> pump;
    std::function<CoreProbeMemory ()> memory;
    // Receives the whole report each time it grows.
    std::function<void (const std::string&)> write_report;
    // A short description of the current step and its progress, 0..1.
    std::function<void (const std::string&, float)> progress;
    std::string platform_name;
  };

  struct CoreProbeOptions {
    int seed = 123;
    int resolution = 2048;
    double walk_seconds = 10.0;
    double ride_seconds = 20.0;
    double game_seconds = 20.0;
    // Drop every cache first, so the first build is a real generation.
    bool clear_caches = true;
  };

  // Returns true when every phase completed. Errors go into the report.
  bool run_core_probe (const CoreProbeHost& host,
                       const CoreProbeOptions& options);
}

#endif
