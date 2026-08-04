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
                                     const std::string& shader_path,
                                     int msaa_samples = 0,
                                     bool request_frame_interpolation = false);

    // Supplies a drawable acquired by CAMetalDisplayLink or the platform view
    // for the next frame.  When none is supplied, the layer acquires one.
    void set_metal_drawable (Renderer& renderer, void* drawable);

    // Supplies the display's current EDR headroom without coupling the backend
    // to NSWindow/NSScreen or a particular host view class.
    void set_metal_edr_headroom (Renderer& renderer, float headroom);

    // Frame interpolation is deliberately a macOS host/backend handshake:
    // the backend validates MetalFX support while the host decides whether
    // the display cadence is high enough to alternate generated and rendered
    // frames. The delta is the interval between rendered simulation frames.
    bool metal_frame_interpolation_supported (Renderer& renderer);
    bool metal_frame_interpolation_active (Renderer& renderer);
    void set_metal_frame_interpolation_enabled (Renderer& renderer,
                                                bool enabled);
    void set_metal_frame_delta_time (Renderer& renderer, float delta_time);

    // Presents the most recently rendered, UI-composited frame into a
    // display-link drawable. This is the second half of the generated/current
    // pair and does not advance the game or encode another world frame.
    bool present_metal_rendered_frame (Renderer& renderer, void* drawable);
  }
}

#endif
