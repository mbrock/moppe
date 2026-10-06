// The SDL host's device on Linux: NHAL on Vulkan, presenting to the
// window's Vulkan surface.
#include <moppe/nhal/vulkan/vulkan_device.hh>
#include <moppe/platform/sdl/sdl.hh>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <stdexcept>
#include <string>

namespace moppe::platform::sdl {
  std::uint64_t window_flags () {
    return SDL_WINDOW_VULKAN;
  }

  std::unique_ptr<nhal::Device> create_device (SDL_Window* window,
                                               std::uint32_t width,
                                               std::uint32_t height) {
    nhal::VulkanSurface surface;
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions (&count);
    surface.instance_extensions.assign (names, names + count);
    surface.create = [window] (VkInstance instance) {
      VkSurfaceKHR made = VK_NULL_HANDLE;
      if (!SDL_Vulkan_CreateSurface (window, instance, nullptr, &made))
        throw std::runtime_error (std::string ("SDL_Vulkan_CreateSurface: ")
                                  + SDL_GetError ());
      return made;
    };
    return nhal::create_vulkan_device (surface, width, height,
                                       nhal::Format::bgra8_unorm);
  }

  const nhal::WorldShaders& world_shaders () {
    return nhal::world_shaders_vulkan ();
  }
}
