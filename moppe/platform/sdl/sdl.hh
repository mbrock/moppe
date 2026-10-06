#ifndef MOPPE_PLATFORM_SDL_SDL_HH
#define MOPPE_PLATFORM_SDL_SDL_HH

// The SDL host's seams: what services.cc gives the run loop, and what each
// platform's device file gives the host. The host (host.cc) is the same on
// every platform SDL3 carries; only the NHAL device differs.

#include <moppe/nhal/nhal.hh>
#include <moppe/nhal/renderer/nhal_renderer.hh>

#include <cstdint>
#include <memory>

struct SDL_Window;

namespace moppe::platform::sdl {
  // Runs the completions platform::async has queued for the main thread.
  // The host calls it once per turn of its loop.
  void run_main_thread_tasks ();

  // The SDL window flag the device needs (SDL_WINDOW_VULKAN or
  // SDL_WINDOW_METAL).
  std::uint64_t window_flags ();

  // A device drawing into `window`, whose drawable is the given pixels.
  std::unique_ptr<nhal::Device> create_device (SDL_Window* window,
                                               std::uint32_t width,
                                               std::uint32_t height);

  // The world programs in the device's form.
  const nhal::WorldShaders& world_shaders ();
}

#endif
