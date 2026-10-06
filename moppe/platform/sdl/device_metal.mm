// The SDL host's device on macOS: NHAL on Metal 4, drawing into the
// CAMetalLayer of an SDL Metal view, in extended linear sRGB for EDR.
#import <QuartzCore/CAMetalLayer.h>

#include <moppe/nhal/metal/metal_device.hh>
#include <moppe/platform/sdl/sdl.hh>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <stdexcept>
#include <string>

namespace moppe::platform::sdl {
  std::uint64_t window_flags () {
    return SDL_WINDOW_METAL;
  }

  std::unique_ptr<nhal::Device> create_device (SDL_Window* window,
                                               std::uint32_t width,
                                               std::uint32_t height) {
    SDL_MetalView view = SDL_Metal_CreateView (window);
    if (!view)
      throw std::runtime_error (std::string ("SDL_Metal_CreateView: ")
                                + SDL_GetError ());
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer (view);
    layer.drawableSize = CGSizeMake (width, height);
    return nhal::create_metal_device (layer, nhal::Format::rgba16_float);
  }

  const nhal::WorldShaders& world_shaders () {
    return nhal::world_shaders_metal ();
  }
}
