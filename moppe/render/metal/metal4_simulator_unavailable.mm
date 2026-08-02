#include <moppe/render/metal/metal_renderer.hh>

#include <stdexcept>

namespace moppe::render {
  Renderer* create_metal_renderer (void*, const std::string&) {
    throw std::runtime_error (
      "Metal 4 is unavailable in Apple's Simulator runtime; use a Metal 4 "
      "device or the macOS build");
  }

  void set_metal_drawable (Renderer&, void*) {}

  void set_metal_edr_headroom (Renderer&, float) {}
}
