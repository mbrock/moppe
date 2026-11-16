#ifndef MOPPE_GAME_GRAPHICS_SETTINGS_HH
#define MOPPE_GAME_GRAPHICS_SETTINGS_HH

#include <moppe/render/types.hh>

namespace moppe::game {
  struct GraphicsSettings {
    float scene_scale = 1.0f;
    // Zero uses scene_scale relative to the point-resolution baseline.
    // A positive value is an absolute fraction of drawable resolution.
    float render_scale_override = 0.0f;
    // The most scene pixels the 3D passes may cover at scene_scale 1, in
    // megapixels.  Point resolution alone does not bound the scene on a
    // desktop display attached at 1x, so this is what keeps an oversized
    // drawable costing resolution rather than frame rate.  Zero disables
    // the budget and restores the point-relative rule on its own.
    float scene_megapixel_budget = 4.2f;
    // The frame rate play holds by shedding scene resolution, down to half
    // of what the rules above give, while the GPU cannot keep up. Zero
    // never sheds any; so does an explicit render scale, which is a
    // promise about the scene's size.
    float target_frame_rate = 60.0f;
    // Late afternoon: a lower, warmer sun with longer shadows.
    float sun_height = 0.62f;

    bool terrain_shadows = true;
    bool ocean = true;
    bool vehicle_effects = true;
    bool star_effects = true;
    bool motion_blur = true;
    bool bloom = true;
    bool auto_exposure = true;
    bool lens_flare = true;
    bool undergrowth = true;
  };

  // Ordinary play favors stable high-refresh presentation. Explicit quality
  // presets remain available as named alternatives.
  GraphicsSettings default_graphics_settings ();
  GraphicsSettings high_graphics_settings ();
  GraphicsSettings balanced_graphics_settings ();
  GraphicsSettings low_graphics_settings ();
  // Apple TV renders the scene below UIKit point resolution and leaves dense
  // procedural ground cover and full-frame cinematic passes to the high
  // preset. Water, particles, vehicle effects, and temporal reconstruction
  // remain enabled by default.
  GraphicsSettings apple_tv_graphics_settings ();
}

#endif
