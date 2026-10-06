#ifndef MOPPE_PLATFORM_UWP_UWP_HH
#define MOPPE_PLATFORM_UWP_UWP_HH

// What the UWP host adds to platform.hh for its own run loop.

#include <string>

namespace moppe::platform::uwp {
  // Runs the completions platform::async has queued for the main thread.
  // The host calls it once per turn of its loop, on the thread that called
  // it first.
  void run_main_thread_tasks ();

  // True once request_quit has been called.
  bool quit_requested ();
  void request_quit ();

  // Frames the SDL host has rendered, for a watchdog.
  long frames_rendered ();

  // The app's LocalState folder, which Device Portal's file explorer shows,
  // with a trailing separator.
  std::string local_state_path ();

  // Reads NAME=VALUE lines from a file into moppe::environment, the UWP
  // stand-in for a process environment. A missing file is not an error.
  // Returns the number of variables set.
  int load_environment_file (const std::string& path);
}

#endif
