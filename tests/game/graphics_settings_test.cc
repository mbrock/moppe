#include <moppe/game/graphics_benchmark.hh>
#include <moppe/game/graphics_settings.hh>

#include <tests/test.hh>

#include <bit>
#include <set>
#include <sstream>
#include <string>

using namespace moppe;

MOPPE_TEST (graphics_feature_registry_finds_canonical_entities) {
  const game::GraphicsFeature* bloom = game::find_graphics_feature ("bloom");
  MOPPE_CHECK (bloom == &game::bloom_feature);
  MOPPE_CHECK (game::find_graphics_feature ("snow-support-filter") ==
               &game::snow_support_filter_feature);
  MOPPE_CHECK (game::find_graphics_feature ("channel-flux-detail") ==
               &game::channel_flux_detail_feature);
  MOPPE_CHECK (game::find_graphics_feature ("not-a-feature") == nullptr);
}

MOPPE_TEST (graphics_features_describe_hot_switchability) {
  MOPPE_CHECK (game::ocean_feature.hot);
  MOPPE_CHECK (game::river_ribbons_feature.hot);
  MOPPE_CHECK (game::particles_feature.hot);
  MOPPE_CHECK (game::vehicle_effects_feature.hot);
  MOPPE_CHECK (game::star_effects_feature.hot);
  MOPPE_CHECK (game::bloom_feature.hot);
  MOPPE_CHECK (game::auto_exposure_feature.hot);
  MOPPE_CHECK (game::lens_flare_feature.hot);
  MOPPE_CHECK (game::terrain_topology_feature.hot);
  MOPPE_CHECK (game::snow_support_filter_feature.hot);
  MOPPE_CHECK (game::channel_flux_detail_feature.hot);

  MOPPE_CHECK (!game::terrain_shadows_feature.hot);
  MOPPE_CHECK (!game::motion_blur_feature.hot);
}

MOPPE_TEST (graphics_feature_lists_apply_to_settings) {
  game::GraphicsSettings settings = game::low_graphics_settings ();
  std::string error;
  MOPPE_CHECK (game::set_graphics_features (
    settings, "bloom,ocean,motion-blur,snow-support-filter", true, error));
  MOPPE_CHECK (settings.bloom);
  MOPPE_CHECK (settings.ocean);
  MOPPE_CHECK (settings.motion_blur);
  MOPPE_CHECK (settings.snow_support_filter);

  MOPPE_CHECK (
    game::set_graphics_features (settings, "bloom,ocean", false, error));
  MOPPE_CHECK (!settings.bloom);
  MOPPE_CHECK (!settings.ocean);
}

MOPPE_TEST (balanced_graphics_reduces_resolution_without_removing_features) {
  const game::GraphicsSettings settings = game::balanced_graphics_settings ();
  MOPPE_CHECK_NEAR (settings.scene_scale, 2.0f / 3.0f, 0.0f);
  for (const game::GraphicsFeature* feature : game::graphics_features)
    if (feature != &game::terrain_topology_feature)
      MOPPE_CHECK (feature->enabled (settings));
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
  MOPPE_CHECK (text.find ("ocean=on(hot)") != std::string::npos);
  MOPPE_CHECK (text.find ("bloom=off(hot)") != std::string::npos);
  MOPPE_CHECK (text.find ("terrain-shadows=off(not-hot)") != std::string::npos);
}

MOPPE_TEST (graphics_benchmark_visits_every_partition_mask_in_gray_order) {
  const int bits = game::graphics_benchmark_dimension_count ();
  MOPPE_CHECK (bits == 5);
  std::set<uint32_t> masks;
  uint32_t previous = game::gray_code (0);
  for (uint32_t epoch = 0; epoch < (1u << bits); ++epoch) {
    const uint32_t mask = game::gray_code (epoch);
    masks.insert (mask);
    if (epoch > 0)
      MOPPE_CHECK (std::popcount (mask ^ previous) == 1);
    previous = mask;
  }
  MOPPE_CHECK (masks.size () == (1u << bits));
}

MOPPE_TEST (graphics_benchmark_partition_groups_small_effects) {
  using Partition = game::RidingGraphicsPartition;
  constexpr Partition partition;
  MOPPE_CHECK (equivalent (partition,
                           game::GraphicsFeatureId::particles,
                           game::GraphicsFeatureId::lens_flare));
  MOPPE_CHECK (!equivalent (
    partition, game::GraphicsFeatureId::ocean, game::GraphicsFeatureId::bloom));
  MOPPE_CHECK (!equivalent (partition,
                            game::GraphicsFeatureId::river_ribbons,
                            game::GraphicsFeatureId::ocean));

  game::GraphicsSettings settings = game::low_graphics_settings ();
  settings.terrain_topology = true;
  const uint32_t resolved = game::apply_graphics_benchmark_mask (
    settings, 1u << static_cast<unsigned> (Partition::Block::small_effects));
  MOPPE_CHECK (resolved == 0b11110011100u);
  MOPPE_CHECK (settings.particles);
  MOPPE_CHECK (settings.vehicle_effects);
  MOPPE_CHECK (settings.star_effects);
  MOPPE_CHECK (settings.lens_flare);
  MOPPE_CHECK (settings.terrain_fragment_normals);
  MOPPE_CHECK (settings.snow_support_filter);
  MOPPE_CHECK (settings.channel_flux_detail);
  MOPPE_CHECK (settings.terrain_topology);
  MOPPE_CHECK (!settings.ocean);
  MOPPE_CHECK (!settings.river_ribbons);
  MOPPE_CHECK (!settings.bloom);
  MOPPE_CHECK (!settings.auto_exposure);
}

MOPPE_TEST (graphics_benchmark_input_tape_is_repeatable) {
  const game::InputFrame a = game::benchmark_input (731);
  const game::InputFrame b = game::benchmark_input (731);
  MOPPE_CHECK_NEAR (
    game::input_value (a.turn), game::input_value (b.turn), 0.0f);
  MOPPE_CHECK_NEAR (
    game::input_value (a.drive), game::input_value (b.drive), 0.0f);
  MOPPE_CHECK_NEAR (
    game::input_value (a.boost), game::input_value (b.boost), 0.0f);
}
