#pragma once

#include <memory>

namespace CA {
  class MetalDrawable;
}

namespace MTL {
  class Device;
  class RenderPassDescriptor;
}

namespace atelier {
  class Renderer {
  public:
    Renderer (MTL::Device* device);
    ~Renderer ();

    Renderer (const Renderer&) = delete;
    Renderer& operator= (const Renderer&) = delete;

    void draw (MTL::RenderPassDescriptor* pass,
               CA::MetalDrawable* drawable,
               float elapsed_seconds,
               float aspect);

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
  };
}
