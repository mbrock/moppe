#pragma once

#include "atelier/atelier.hh"

#include <memory>

namespace atelier {
  class Renderer {
  public:
    Renderer ();
    ~Renderer ();

    Renderer (const Renderer&) = delete;
    Renderer& operator= (const Renderer&) = delete;

    void* native_layer () const;
    void resize (Viewport viewport);
    void draw ();

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
  };
}
