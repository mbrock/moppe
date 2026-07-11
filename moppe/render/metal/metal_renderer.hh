#ifndef MOPPE_METAL_RENDERER_HH
#define MOPPE_METAL_RENDERER_HH

#include <moppe/render/renderer.hh>

#include <string>

namespace moppe {
  namespace render {
    // Creates the Metal backend attached to an MTKView (passed as
    // void* so this header stays Objective-C-free).  The view's pixel
    // format and colorspace are configured here.  shader_path names a
    // compiled library or runtime-compilable source inside the assets.
    Renderer* create_metal_renderer (void* mtk_view,
                                     const std::string& shader_path);

    // Supplies a drawable acquired by CAMetalDisplayLink for the next frame.
    // When none is supplied, the backend acquires MTKView.currentDrawable.
    void set_metal_drawable (Renderer& renderer, void* drawable);
  }
}

#endif
