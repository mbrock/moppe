#pragma once

#include "atelier/atelier.hh"

#include <cstdint>
#include <memory>

namespace atelier {
  // A still image read back from the renderer: tightly packed BGRA
  // rows in the sRGB colour space.
  struct Image {
    std::size_t width = 0;
    std::size_t height = 0;
    std::unique_ptr<std::uint8_t[]> bgra;
  };

  class Renderer {
  public:
    Renderer ();
    ~Renderer ();

    Renderer (const Renderer&) = delete;
    Renderer& operator= (const Renderer&) = delete;

    void* native_layer () const;
    void resize (Viewport viewport);
    void draw ();

    // Render the scene as it stands at `elapsed` and photograph it,
    // without involving the window system.  The surface must have
    // been resized to a non-empty viewport first.
    [[nodiscard]] Image capture (Duration elapsed);

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
  };
}
