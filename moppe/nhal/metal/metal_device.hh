// The Metal 4 NHAL device, drawing into a CAMetalLayer.
#ifndef MOPPE_NHAL_METAL_DEVICE_HH
#define MOPPE_NHAL_METAL_DEVICE_HH

#include <moppe/nhal/nhal.hh>

#include <memory>

@class CAMetalLayer;

namespace moppe::nhal {
  // The layer's drawable size is the surface size; the host keeps it in
  // step with the view (or calls resize_surface). The surface format is
  // bgra8_unorm or rgba16_float (extended linear sRGB, for EDR).
  std::unique_ptr<Device> create_metal_device (CAMetalLayer* layer,
                                               Format surface_format);
}

#endif
