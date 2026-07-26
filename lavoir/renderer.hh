#pragma once

#include "lavoir/units.hh"

#include <cstdint>
#include <memory>

/// The renderer's window-agnostic face: a native layer for the view
/// to adopt, a resize, a per-frame draw, and a headless capture. The
/// Metal machinery stays behind the pimpl so the Cocoa shim compiles
/// without metal-cpp.

namespace lavoir {
  struct viewport;

  /// One captured frame as CPU-side BGRA pixels.
  struct image {
    pixels_t width = pixels_t::zero ();
    pixels_t height = pixels_t::zero ();
    std::unique_ptr<std::uint8_t[]> bgra;
  };

  class renderer {
  public:
    renderer ();
    ~renderer ();

    void* native_layer () const;
    void resize (pixels_t width, pixels_t height);
    void draw ();
    image capture (seconds_t elapsed);

  private:
    class impl;
    std::unique_ptr<impl> m_impl;
  };
}
