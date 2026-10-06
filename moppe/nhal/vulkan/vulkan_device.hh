// The Vulkan NHAL device, presenting to a window system's surface or, with
// none, rendering into offscreen images for captures and tests.
#ifndef MOPPE_NHAL_VULKAN_DEVICE_HH
#define MOPPE_NHAL_VULKAN_DEVICE_HH

#include <moppe/nhal/nhal.hh>

#include <vulkan/vulkan_core.h>

#include <functional>
#include <memory>
#include <vector>

namespace moppe::nhal {
  // What a window system contributes: the instance extensions its surface
  // needs (VK_KHR_surface and the platform's own), and a function making
  // the window's surface once the device has its instance. The device owns
  // the surface from then on.
  struct VulkanSurface {
    std::vector<const char*> instance_extensions;
    std::function<VkSurfaceKHR (VkInstance)> create;
  };

  // A device presenting to `surface`, or headless when it has no create
  // function. The surface starts at the given size; the host follows its
  // window with resize_surface. MOPPE_VULKAN_VALIDATION=1 turns on the
  // Khronos validation layer, and MOPPE_VULKAN_DEVICE=N picks the Nth
  // physical device instead of the first discrete one.
  std::unique_ptr<Device> create_vulkan_device (const VulkanSurface& surface,
                                                std::uint32_t width,
                                                std::uint32_t height,
                                                Format surface_format);
}

#endif
