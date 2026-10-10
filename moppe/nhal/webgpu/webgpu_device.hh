// The WebGPU NHAL device, for the browser: it draws into a canvas with the
// GPUDevice the page acquired before the program started.
#ifndef MOPPE_NHAL_WEBGPU_DEVICE_HH
#define MOPPE_NHAL_WEBGPU_DEVICE_HH

#include <moppe/nhal/nhal.hh>

#include <memory>

namespace moppe::nhal {
  // A device presenting to the canvas `selector` names (a CSS selector such
  // as "#canvas"), whose drawing buffer starts at the given size; the host
  // follows the canvas with resize_surface. The page hands over its device
  // as Module.preinitializedWebGPUDevice (moppe/platform/web/pre.js), with
  // float32-filterable among its features.
  std::unique_ptr<Device> create_webgpu_device (const char* selector,
                                                std::uint32_t width,
                                                std::uint32_t height);
}

#endif
