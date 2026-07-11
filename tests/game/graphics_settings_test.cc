#include <moppe/game/graphics_settings.hh>

#include <tests/test.hh>

#include <sstream>
#include <string>

using namespace moppe;

MOPPE_TEST (graphics_feature_registry_finds_canonical_entities) {
  const game::GraphicsFeature* bloom = game::find_graphics_feature ("bloom");
  MOPPE_CHECK (bloom == &game::bloom_feature);
  MOPPE_CHECK (game::find_graphics_feature ("not-a-feature") == nullptr);
}

MOPPE_TEST (graphics_features_describe_hot_switchability) {
  MOPPE_CHECK (game::grass_feature.hot);
  MOPPE_CHECK (game::ocean_feature.hot);
  MOPPE_CHECK (game::particles_feature.hot);
  MOPPE_CHECK (game::vehicle_effects_feature.hot);
  MOPPE_CHECK (game::star_effects_feature.hot);
  MOPPE_CHECK (game::bloom_feature.hot);
  MOPPE_CHECK (game::auto_exposure_feature.hot);
  MOPPE_CHECK (game::lens_flare_feature.hot);

  MOPPE_CHECK (!game::terrain_shadows_feature.hot);
  MOPPE_CHECK (!game::vegetation_feature.hot);
  MOPPE_CHECK (!game::river_ribbons_feature.hot);
  MOPPE_CHECK (!game::motion_blur_feature.hot);
  MOPPE_CHECK (!game::terrain_topology_feature.hot);
}

MOPPE_TEST (graphics_feature_lists_apply_to_settings) {
  game::GraphicsSettings settings = game::low_graphics_settings ();
  std::string error;
  MOPPE_CHECK (game::set_graphics_features (
    settings, "bloom,ocean,motion-blur", true, error));
  MOPPE_CHECK (settings.bloom);
  MOPPE_CHECK (settings.ocean);
  MOPPE_CHECK (settings.motion_blur);
  MOPPE_CHECK (!settings.vegetation);

  MOPPE_CHECK (
    game::set_graphics_features (settings, "bloom,ocean", false, error));
  MOPPE_CHECK (!settings.bloom);
  MOPPE_CHECK (!settings.ocean);
}

MOPPE_TEST (graphics_feature_lists_reject_unknown_and_empty_names) {
  game::GraphicsSettings settings;
  std::string error;
  MOPPE_CHECK (
    !game::set_graphics_features (settings, "bloom,unknown", true, error));
  MOPPE_CHECK (error == "unknown graphics feature: unknown");
  MOPPE_CHECK (!game::set_graphics_features (settings, "bloom,", true, error));
  MOPPE_CHECK (error == "empty graphics feature name");
}

MOPPE_TEST (graphics_settings_print_every_resolved_value) {
  game::GraphicsSettings settings = game::low_graphics_settings ();
  settings.ocean = true;
  std::ostringstream output;
  game::print_graphics_settings (output, settings);
  const std::string text = output.str ();
  MOPPE_CHECK (text.find ("scene-scale=0.5") != std::string::npos);
  MOPPE_CHECK (text.find ("grass-density=1") != std::string::npos);
  MOPPE_CHECK (text.find ("ocean=on(hot)") != std::string::npos);
  MOPPE_CHECK (text.find ("bloom=off(hot)") != std::string::npos);
  MOPPE_CHECK (text.find ("terrain-shadows=off(not-hot)") != std::string::npos);
}
