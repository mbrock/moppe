#pragma once

#include <cstddef>
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
    std::size_t width = 0;
    std::size_t height = 0;
    std::unique_ptr<std::uint8_t[]> bgra;
  };

  class renderer {
  public:
    renderer ();
    ~renderer ();

    void* native_layer () const;
    void resize (std::size_t width, std::size_t height);
    void draw ();
    image capture (double seconds_elapsed);

  private:
    class impl;
    std::unique_ptr<impl> m_impl;
  };
}
