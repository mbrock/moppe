#ifndef MOPPE_PLATFORM_LINUX_LINUX_HH
#define MOPPE_PLATFORM_LINUX_LINUX_HH

// What the Linux host adds to platform.hh for its own run loop.

namespace moppe::platform::linux_host {
  // Runs the completions platform::async has queued for the main thread.
  // The host calls it once per turn of its loop.
  void run_main_thread_tasks ();
}

#endif
