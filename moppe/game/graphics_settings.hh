#ifndef MOPPE_GAME_GRAPHICS_SETTINGS_HH
#define MOPPE_GAME_GRAPHICS_SETTINGS_HH

#include <array>
#include <iosfwd>
#include <string>
#include <string_view>

namespace moppe::game {
  struct GraphicsSettings {
    float scene_scale = 1.0f;
    // Zero uses scene_scale relative to the point-resolution baseline.
    // A positive value is an absolute fraction of drawable resolution.
    float render_scale_override = 0.0f;
    float grass_density = 1.0f;
    float sun_height = 0.62f;

    bool terrain_shadows = true;
    bool vegetation = true;
    bool grass = true;
    bool ocean = true;
    bool river_ribbons = false;
    bool particles = true;
    bool vehicle_effects = true;
    bool star_effects = true;
    bool motion_blur = true;
    bool bloom = true;
    bool auto_exposure = true;
    bool lens_flare = true;
    bool terrain_topology = false;
  };

  // A Boolean graphics feature has one canonical name and knows where its
  // value lives.  The same entities drive command-line parsing and resolved
  // configuration output.
  struct GraphicsFeature {
    std::string_view name;
    bool GraphicsSettings::* member;
    // Hot features need no work beyond changing their stored value.  The next
    // frame observes the change without rebuilding resources or resetting
    // renderer state.
    bool hot;

    bool enabled (const GraphicsSettings& settings) const {
      return settings.*member;
    }
    void set (GraphicsSettings& settings, bool value) const {
      settings.*member = value;
    }
  };

  inline constexpr GraphicsFeature terrain_shadows_feature {
    "terrain-shadows", &GraphicsSettings::terrain_shadows, false
  };
  inline constexpr GraphicsFeature vegetation_feature {
    "vegetation", &GraphicsSettings::vegetation, false
  };
  inline constexpr GraphicsFeature grass_feature { "grass",
                                                   &GraphicsSettings::grass,
                                                   true };
  inline constexpr GraphicsFeature ocean_feature { "ocean",
                                                   &GraphicsSettings::ocean,
                                                   true };
  inline constexpr GraphicsFeature river_ribbons_feature {
    "river-ribbons", &GraphicsSettings::river_ribbons, false
  };
  inline constexpr GraphicsFeature particles_feature {
    "particles", &GraphicsSettings::particles, true
  };
  inline constexpr GraphicsFeature vehicle_effects_feature {
    "vehicle-effects", &GraphicsSettings::vehicle_effects, true
  };
  inline constexpr GraphicsFeature star_effects_feature {
    "star-effects", &GraphicsSettings::star_effects, true
  };
  inline constexpr GraphicsFeature motion_blur_feature {
    "motion-blur", &GraphicsSettings::motion_blur, false
  };
  inline constexpr GraphicsFeature bloom_feature { "bloom",
                                                   &GraphicsSettings::bloom,
                                                   true };
  inline constexpr GraphicsFeature auto_exposure_feature {
    "auto-exposure", &GraphicsSettings::auto_exposure, true
  };
  inline constexpr GraphicsFeature lens_flare_feature {
    "lens-flare", &GraphicsSettings::lens_flare, true
  };
  inline constexpr GraphicsFeature terrain_topology_feature {
    "terrain-topology", &GraphicsSettings::terrain_topology, false
  };

  inline constexpr std::array<const GraphicsFeature*, 13> graphics_features {
    &terrain_shadows_feature,
    &vegetation_feature,
    &grass_feature,
    &ocean_feature,
    &river_ribbons_feature,
    &particles_feature,
    &vehicle_effects_feature,
    &star_effects_feature,
    &motion_blur_feature,
    &bloom_feature,
    &auto_exposure_feature,
    &lens_flare_feature,
    &terrain_topology_feature,
  };

  GraphicsSettings high_graphics_settings ();
  GraphicsSettings low_graphics_settings ();
  const GraphicsFeature* find_graphics_feature (std::string_view name);

  // Parses a comma-separated list of canonical feature names.
  bool set_graphics_features (GraphicsSettings& settings,
                              std::string_view names,
                              bool enabled,
                              std::string& error);

  // Applies the legacy graphics environment variables in one place.
  bool apply_graphics_environment (GraphicsSettings& settings,
                                   std::string& error);

  void print_graphics_settings (std::ostream& output,
                                const GraphicsSettings& settings);
}

#endif
