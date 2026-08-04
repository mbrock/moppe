#include <moppe/render/metal/metal_renderer.hh>

#include <stdexcept>

namespace moppe::render {
  Renderer* create_metal_renderer (void*, const std::string&, int, bool) {
    throw std::runtime_error (
      "Metal 4 is unavailable in Apple's Simulator runtime; use a Metal 4 "
      "device or the macOS build");
  }

  void set_metal_drawable (Renderer&, void*) {}

  void set_metal_edr_headroom (Renderer&, float) {}
  bool metal_frame_interpolation_supported (Renderer&) {
    return false;
  }
  bool metal_frame_interpolation_active (Renderer&) {
    return false;
  }
  void set_metal_frame_interpolation_enabled (Renderer&, bool) {}
  void set_metal_frame_delta_time (Renderer&, float) {}
  bool present_metal_rendered_frame (Renderer&, void*) {
    return false;
  }
}
