// The SDL host's device on Xbox: NHAL on Direct3D 12, presenting to the
// window's CoreWindow (the SDL3 fork's UWP backend) with a 3840x2160
// swapchain, which the console shows natively behind its 1080p window.
#include <moppe/nhal/d3d12/d3d12_device.hh>
#include <moppe/platform/sdl/sdl.hh>

#include <SDL3/SDL.h>

#include <stdexcept>

namespace moppe::platform::sdl {
  std::uint64_t window_flags () {
    return 0;
  }

  std::unique_ptr<nhal::Device> create_device (SDL_Window* window,
                                               std::uint32_t,
                                               std::uint32_t) {
    auto* core_window = static_cast<IUnknown*> (SDL_GetPointerProperty (
      SDL_GetWindowProperties (window), SDL_PROP_WINDOW_WINRT_WINDOW_POINTER,
      nullptr));
    if (!core_window)
      throw std::runtime_error ("NHAL: the SDL window has no CoreWindow");
    return nhal::create_d3d12_device (core_window, 3840, 2160,
                                      nhal::Format::bgra8_unorm);
  }

  const nhal::WorldShaders& world_shaders () {
    return nhal::world_shaders_d3d12 ();
  }
}
