// The SDL host's device in the browser: NHAL on WebGPU, drawing into the
// canvas SDL's window is.
#include <moppe/nhal/webgpu/webgpu_device.hh>
#include <moppe/platform/sdl/sdl.hh>

#include <SDL3/SDL.h>

namespace moppe::platform::sdl {
  // The canvas fills the page and follows the browser window's size.
  std::uint64_t window_flags () {
    return SDL_WINDOW_FILL_DOCUMENT;
  }

  std::unique_ptr<nhal::Device> create_device (SDL_Window*,
                                               std::uint32_t width,
                                               std::uint32_t height) {
    // SDL's Emscripten window is the page's canvas of this id.
    return nhal::create_webgpu_device ("#canvas", width, height);
  }

  const nhal::WorldShaders& world_shaders () {
    return nhal::world_shaders_webgpu ();
  }
}
