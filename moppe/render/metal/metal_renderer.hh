#ifndef MOPPE_METAL_RENDERER_HH
#define MOPPE_METAL_RENDERER_HH

#include <moppe/render/renderer.hh>

#include <string>

namespace moppe {
  namespace render {
    // Creates the Metal 4 backend for a CAMetalLayer (passed as void* so this
    // header stays Objective-C-free).  The backend owns command submission;
    // the platform owns display pacing and drawable acquisition.
    Renderer* create_metal_renderer (void* metal_layer,
                                     const std::string& shader_path);

    // Supplies a drawable acquired by CAMetalDisplayLink or the platform view
    // for the next frame.  When none is supplied, the layer acquires one.
    void set_metal_drawable (Renderer& renderer, void* drawable);

    // Supplies the display's current EDR headroom without coupling the backend
    // to NSWindow/NSScreen or a particular host view class.
    void set_metal_edr_headroom (Renderer& renderer, float headroom);
  }
}

#endif
