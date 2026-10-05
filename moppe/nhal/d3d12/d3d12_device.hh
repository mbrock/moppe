// The Direct3D 12 NHAL device, presenting to a UWP CoreWindow.
#ifndef MOPPE_NHAL_D3D12_DEVICE_HH
#define MOPPE_NHAL_D3D12_DEVICE_HH

#include <moppe/nhal/nhal.hh>

#include <memory>

struct IUnknown;

namespace moppe::nhal {
  // `window` is the CoreWindow as IUnknown. The swapchain is the given
  // size, which need not match the window: on Xbox a 3840x2160 swapchain
  // reaches the TV natively from a 1920x1080 window.
  std::unique_ptr<Device> create_d3d12_device (IUnknown* window,
                                               std::uint32_t width,
                                               std::uint32_t height,
                                               Format surface_format);

  // The step the device is in, for a watchdog to name when frames stall.
  const char* d3d12_device_step ();
}

#endif
