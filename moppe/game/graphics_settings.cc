#include <moppe/game/graphics_settings.hh>

namespace moppe::game {
  GraphicsSettings default_graphics_settings () {
    GraphicsSettings settings = high_graphics_settings ();
    settings.render_scale_override = 0.5f;
    return settings;
  }

  GraphicsSettings high_graphics_settings () {
    return {};
  }

  GraphicsSettings balanced_graphics_settings () {
    GraphicsSettings settings;
    settings.scene_scale = 2.0f / 3.0f;
    return settings;
  }

  GraphicsSettings low_graphics_settings () {
    GraphicsSettings settings;
    settings.scene_scale = 0.5f;
    settings.terrain_shadows = false;
    settings.ocean = false;
    settings.vehicle_effects = false;
    settings.star_effects = false;
    settings.motion_blur = false;
    settings.bloom = false;
    settings.auto_exposure = false;
    settings.lens_flare = false;
    settings.undergrowth = false;
    return settings;
  }

  GraphicsSettings apple_tv_graphics_settings () {
    GraphicsSettings settings = high_graphics_settings ();
    // A15 has to feed a 4K television as well as reconstruct the scene. Keep
    // the water and moment-to-moment effects, but avoid the passes whose cost
    // scales across the whole image or explodes into dense procedural
    // geometry. The temporal resolve restores the output footprint from this
    // 540p scene.
    settings.scene_scale = 0.5f;
    settings.terrain_shadows = false;
    settings.motion_blur = false;
    settings.bloom = false;
    settings.auto_exposure = false;
    settings.undergrowth = false;
    return settings;
  }
}
