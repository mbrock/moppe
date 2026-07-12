#pragma once

#include <memory>

namespace atelier {
  class Renderer {
  public:
    Renderer ();
    ~Renderer ();

    Renderer (const Renderer&) = delete;
    Renderer& operator= (const Renderer&) = delete;

    void* native_layer () const;
    void resize (double width, double height);
    void draw ();

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
  };
}
