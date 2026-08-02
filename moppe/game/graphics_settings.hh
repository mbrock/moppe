#ifndef MOPPE_GAME_GRAPHICS_SETTINGS_HH
#define MOPPE_GAME_GRAPHICS_SETTINGS_HH

#include <array>
#include <iosfwd>
#include <string>
#include <string_view>

namespace moppe::game {
  enum class GraphicsFeatureId {
    terrain_shadows,
    ocean,
    waterfall_curtains,
    particles,
    vehicle_effects,
    star_effects,
    motion_blur,
    bloom,
    auto_exposure,
    lens_flare,
    terrain_topology,
    terrain_fragment_normals,
    snow_support_filter,
    channel_flux_detail,
    undergrowth,
  };

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
    // Late afternoon: a lower, warmer sun with longer shadows.
    float sun_height = 0.56f;

    bool terrain_shadows = true;
    bool ocean = true;
    bool waterfall_curtains = true;
    bool particles = true;
    bool vehicle_effects = true;
    bool star_effects = true;
    bool motion_blur = true;
    bool bloom = true;
    bool auto_exposure = true;
    bool lens_flare = true;
    bool terrain_topology = false;
    bool terrain_fragment_normals = true;
    bool snow_support_filter = true;
    bool channel_flux_detail = true;
    bool undergrowth = true;
  };

  // A Boolean graphics feature has one canonical name and knows where its
  // value lives.  The same entities drive command-line parsing and resolved
  // configuration output.
  struct GraphicsFeature {
    GraphicsFeatureId id;
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
    GraphicsFeatureId::terrain_shadows,
    "terrain-shadows",
    &GraphicsSettings::terrain_shadows,
    false
  };
  inline constexpr GraphicsFeature ocean_feature {
    GraphicsFeatureId::ocean, "ocean", &GraphicsSettings::ocean, true
  };
  inline constexpr GraphicsFeature waterfall_curtains_feature {
    GraphicsFeatureId::waterfall_curtains,
    "waterfall-curtains",
    &GraphicsSettings::waterfall_curtains,
    true
  };
  inline constexpr GraphicsFeature particles_feature {
    GraphicsFeatureId::particles,
    "particles",
    &GraphicsSettings::particles,
    true
  };
  inline constexpr GraphicsFeature vehicle_effects_feature {
    GraphicsFeatureId::vehicle_effects,
    "vehicle-effects",
    &GraphicsSettings::vehicle_effects,
    true
  };
  inline constexpr GraphicsFeature star_effects_feature {
    GraphicsFeatureId::star_effects,
    "star-effects",
    &GraphicsSettings::star_effects,
    true
  };
  inline constexpr GraphicsFeature motion_blur_feature {
    GraphicsFeatureId::motion_blur,
    "motion-blur",
    &GraphicsSettings::motion_blur,
    false
  };
  inline constexpr GraphicsFeature bloom_feature {
    GraphicsFeatureId::bloom, "bloom", &GraphicsSettings::bloom, true
  };
  inline constexpr GraphicsFeature auto_exposure_feature {
    GraphicsFeatureId::auto_exposure,
    "auto-exposure",
    &GraphicsSettings::auto_exposure,
    true
  };
  inline constexpr GraphicsFeature lens_flare_feature {
    GraphicsFeatureId::lens_flare,
    "lens-flare",
    &GraphicsSettings::lens_flare,
    true
  };
  inline constexpr GraphicsFeature terrain_topology_feature {
    GraphicsFeatureId::terrain_topology,
    "terrain-topology",
    &GraphicsSettings::terrain_topology,
    true
  };

  inline constexpr GraphicsFeature terrain_fragment_normals_feature {
    GraphicsFeatureId::terrain_fragment_normals,
    "terrain-fragment-normals",
    &GraphicsSettings::terrain_fragment_normals,
    true
  };

  inline constexpr GraphicsFeature snow_support_filter_feature {
    GraphicsFeatureId::snow_support_filter,
    "snow-support-filter",
    &GraphicsSettings::snow_support_filter,
    true
  };

  inline constexpr GraphicsFeature channel_flux_detail_feature {
    GraphicsFeatureId::channel_flux_detail,
    "channel-flux-detail",
    &GraphicsSettings::channel_flux_detail,
    true
  };

  inline constexpr GraphicsFeature undergrowth_feature {
    GraphicsFeatureId::undergrowth,
    "undergrowth",
    &GraphicsSettings::undergrowth,
    true
  };

  inline constexpr std::array<const GraphicsFeature*, 15> graphics_features {
    &terrain_shadows_feature,     &ocean_feature,
    &waterfall_curtains_feature,  &particles_feature,
    &vehicle_effects_feature,     &star_effects_feature,
    &motion_blur_feature,         &bloom_feature,
    &auto_exposure_feature,       &lens_flare_feature,
    &terrain_topology_feature,    &terrain_fragment_normals_feature,
    &snow_support_filter_feature, &channel_flux_detail_feature,
    &undergrowth_feature,
  };

  GraphicsSettings high_graphics_settings ();
  GraphicsSettings balanced_graphics_settings ();
  GraphicsSettings low_graphics_settings ();
  // Apple TV renders the scene below UIKit point resolution. This keeps a 4K
  // drawable from silently turning the 3D pass into a 4K workload while
  // preserving the complete presentation feature set.
  GraphicsSettings apple_tv_graphics_settings ();
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
