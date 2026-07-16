#include <moppe/terrain/program.hh>

#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace moppe::terrain {
  TerrainProgram make_geological_program (std::uint32_t root_seed,
                                          GeologicalLayer layer) {
    return { .source = { .recipe = make_geological_recipe (root_seed),
                         .layer = layer },
             .seed = Seed { root_seed },
             .transforms = { NormalizeHeights {} } };
  }

  TerrainProgram make_default_world_program (std::uint32_t root_seed) {
    return make_world_program (root_seed, TerrainGenerationProfile::Research);
  }

  TerrainProgram make_orogeny_program (std::uint32_t root_seed,
                                       TerrainGenerationProfile profile) {
    TerrainProgram program = make_geological_program (root_seed);
    program.source.mode = GeologicalSource::Mode::Orogeny;
    program.transforms.clear ();
    OrogenyEvolution orogeny;
    const float duration =
      profile == TerrainGenerationProfile::Fast       ? 200000.0f
      : profile == TerrainGenerationProfile::Play     ? 500000.0f
      : profile == TerrainGenerationProfile::Research ? 1000000.0f
                                                      : 200000.0f;
    orogeny.evolution.duration = duration * mp_units::astronomy::Julian_year;
    program.transforms.emplace_back (orogeny);
    return program;
  }

  TerrainProgram make_world_program (std::uint32_t root_seed,
                                     TerrainGenerationProfile profile) {
    TerrainProgram program = make_orogeny_program (root_seed, profile);
    program.transforms.emplace_back (TrailFormation {});
    return program;
  }

  std::string_view profile_id (TerrainGenerationProfile profile) noexcept {
    switch (profile) {
    case TerrainGenerationProfile::Fast:
      return "fast";
    case TerrainGenerationProfile::Play:
      return "play";
    case TerrainGenerationProfile::Research:
      return "research";
    }
    return "play";
  }

  void validate_program (const TerrainProgram& program) {
    validate_geological_recipe (program.source.recipe);
    if (!std::isfinite (program.source.sea_level) ||
        !std::isfinite (program.source.coastline) ||
        !std::isfinite (meters_value (program.source.initial_land_relief)) ||
        program.source.initial_land_relief < 0.0f * mp_units::si::metre ||
        !std::isfinite (
          meters_value (program.source.initial_bathymetric_relief)) ||
        program.source.initial_bathymetric_relief < 0.0f * mp_units::si::metre)
      throw std::invalid_argument ("orogeny source parameters are invalid");
    for (const TerrainTransform& transform : program.transforms) {
      std::visit (
        [] (const auto& operation) {
          using T = std::decay_t<decltype (operation)>;
          if constexpr (std::is_same_v<T, PowerHeights>) {
            if (!std::isfinite (operation.exponent) ||
                operation.exponent <= 0.0f)
              throw std::invalid_argument (
                "height exponent must be positive and finite");
          } else if constexpr (std::is_same_v<T, AnalyticalErosion>) {
            if (!std::isfinite (julian_years_value (operation.duration)) ||
                operation.duration < 0.0f * mp_units::astronomy::Julian_year ||
                !std::isfinite (
                  meters_per_julian_year_value (operation.uplift_rate)) ||
                operation.uplift_rate < 0.0f * mp_units::si::metre /
                                          mp_units::astronomy::Julian_year ||
                !std::isfinite (operation.erodibility) ||
                operation.erodibility <= 0.0f ||
                !std::isfinite (operation.area_exponent) ||
                operation.area_exponent < 0.0f ||
                !std::isfinite (operation.sea_level) ||
                operation.fixed_point_iterations <= 0 ||
                !std::isfinite (operation.relaxation) ||
                operation.relaxation <= 0.0f || operation.relaxation > 1.0f)
              throw std::invalid_argument (
                "analytical erosion parameters are invalid");
          } else if constexpr (std::is_same_v<T, OrogenyEvolution>) {
            const StreamPowerEvolution& evolution = operation.evolution;
            if (!std::isfinite (meters_per_julian_year_value (
                  operation.maximum_uplift_rate)) ||
                operation.maximum_uplift_rate <
                  0.0f * mp_units::si::metre /
                    mp_units::astronomy::Julian_year ||
                !std::isfinite (julian_years_value (evolution.duration)) ||
                evolution.duration < 0.0f * mp_units::astronomy::Julian_year ||
                !std::isfinite (julian_years_value (evolution.time_step)) ||
                evolution.time_step <=
                  0.0f * mp_units::astronomy::Julian_year ||
                !std::isfinite (meters_per_julian_year_value (
                  evolution.reference_incision_rate)) ||
                evolution.reference_incision_rate <
                  0.0f * mp_units::si::metre /
                    mp_units::astronomy::Julian_year ||
                !std::isfinite (
                  square_meters_value (evolution.reference_area)) ||
                evolution.reference_area <=
                  0.0f * mp_units::si::metre * mp_units::si::metre ||
                !std::isfinite (evolution.area_exponent) ||
                evolution.area_exponent < 0.0f ||
                !std::isfinite (square_meters_per_julian_year_value (
                  evolution.diffusivity)) ||
                evolution.diffusivity < 0.0f * mp_units::si::metre *
                                          mp_units::si::metre /
                                          mp_units::astronomy::Julian_year ||
                !std::isfinite (evolution.sea_level) ||
                !std::isfinite (
                  evolution.channel_persistence.numerical_value_in (
                    mp_units::one)) ||
                evolution.channel_persistence <
                  0.0f * channel_persistence[mp_units::one] ||
                evolution.channel_persistence >=
                  1.0f * channel_persistence[mp_units::one])
              throw std::invalid_argument (
                "orogeny evolution parameters are invalid");
          } else if constexpr (std::is_same_v<T, ThermalErosion>) {
            if (operation.iterations < 0 || !std::isfinite (operation.talus) ||
                operation.talus < 0.0f)
              throw std::invalid_argument (
                "thermal erosion parameters are invalid");
          } else if constexpr (std::is_same_v<T, HillslopeDiffusion>) {
            if (!std::isfinite (julian_years_value (operation.duration)) ||
                operation.duration < 0.0f * mp_units::astronomy::Julian_year ||
                !std::isfinite (square_meters_per_julian_year_value (
                  operation.diffusivity)) ||
                square_meters_per_julian_year_value (operation.diffusivity) <
                  0.0f)
              throw std::invalid_argument (
                "hillslope diffusion parameters are invalid");
          } else if constexpr (std::is_same_v<T, TrailFormation>) {
            if (!std::isfinite (operation.sea_level) ||
                !std::isfinite (
                  square_meters_value (operation.minimum_catchment_area)) ||
                !std::isfinite (
                  square_meters_value (operation.maximum_catchment_area)) ||
                operation.minimum_catchment_area <=
                  0.0f * mp_units::si::metre * mp_units::si::metre ||
                operation.maximum_catchment_area <
                  operation.minimum_catchment_area ||
                !std::isfinite (
                  meters_value (operation.minimum_height_above_sea)) ||
                operation.minimum_height_above_sea <
                  0.0f * mp_units::si::metre ||
                !std::isfinite (meters_value (operation.width)) ||
                operation.width <= 0.0f * mp_units::si::metre ||
                !std::isfinite (meters_value (operation.shoulder_blend)) ||
                operation.shoulder_blend < 0.0f * mp_units::si::metre ||
                !std::isfinite (meters_value (operation.maximum_cut)) ||
                operation.maximum_cut < 0.0f * mp_units::si::metre ||
                !std::isfinite (meters_value (operation.maximum_fill)) ||
                operation.maximum_fill < 0.0f * mp_units::si::metre ||
                !std::isfinite (
                  operation.maximum_grade.numerical_value_in (mp_units::one)) ||
                operation.maximum_grade < 0.0f * terrain_slope[mp_units::one] ||
                !std::isfinite (operation.designed_grade.numerical_value_in (
                  mp_units::one)) ||
                operation.designed_grade <
                  0.0f * terrain_slope[mp_units::one] ||
                operation.designed_grade > operation.maximum_grade ||
                !std::isfinite (
                  operation.crossfall.numerical_value_in (mp_units::one)) ||
                operation.crossfall < 0.0f * terrain_slope[mp_units::one] ||
                operation.crossfall > 0.1f * terrain_slope[mp_units::one] ||
                operation.grading_iterations < 0 ||
                !std::isfinite (
                  meters_value (operation.home_base_water_distance)) ||
                operation.home_base_water_distance <=
                  0.0f * mp_units::si::metre ||
                !std::isfinite (
                  meters_value (operation.home_base_pad_radius)) ||
                operation.home_base_pad_radius <= 0.0f * mp_units::si::metre ||
                !std::isfinite (
                  meters_value (operation.desired_circuit_radius)) ||
                operation.desired_circuit_radius <=
                  0.0f * mp_units::si::metre ||
                !std::isfinite (meters_value (
                  operation.highland_preference_height_above_sea)) ||
                operation.highland_preference_height_above_sea <=
                  0.0f * mp_units::si::metre ||
                !std::isfinite (
                  meters_value (operation.alpine_avoidance_height_above_sea)) ||
                operation.alpine_avoidance_height_above_sea <
                  operation.highland_preference_height_above_sea)
              throw std::invalid_argument (
                "trail formation parameters are invalid");
          }
        },
        transform);
    }
  }

  std::string_view terrain_transform_id (const TerrainTransform& transform) {
    return std::visit (
      [] (const auto& operation) -> std::string_view {
        using T = std::decay_t<decltype (operation)>;
        if constexpr (std::is_same_v<T, NormalizeHeights>)
          return "normalize";
        else if constexpr (std::is_same_v<T, PowerHeights>)
          return "power";
        else if constexpr (std::is_same_v<T, AnalyticalErosion>)
          return "analytical";
        else if constexpr (std::is_same_v<T, OrogenyEvolution>)
          return "orogeny";
        else if constexpr (std::is_same_v<T, TrailFormation>)
          return "trails";
        else if constexpr (std::is_same_v<T, HillslopeDiffusion>)
          return "diffuse";
        else
          return "thermal";
      },
      transform);
  }

  TransformSemantics
  terrain_transform_semantics (const TerrainTransform& transform) {
    return std::visit (
      [] (const auto& operation) -> TransformSemantics {
        using T = std::decay_t<decltype (operation)>;
        if constexpr (std::is_same_v<T, PowerHeights>)
          return { SpatialScope::Pointwise, EvaluationOrder::Direct };
        else if constexpr (std::is_same_v<T, NormalizeHeights>)
          return { SpatialScope::Global, EvaluationOrder::Reduction };
        else if constexpr (std::is_same_v<T, ThermalErosion>)
          return { SpatialScope::Neighborhood, EvaluationOrder::Iterative };
        else if constexpr (std::is_same_v<T, HillslopeDiffusion>)
          return { SpatialScope::Neighborhood, EvaluationOrder::Iterative };
        else if constexpr (std::is_same_v<T, TrailFormation>)
          return { SpatialScope::Global, EvaluationOrder::Iterative };
        else
          return { SpatialScope::Global, EvaluationOrder::Iterative };
      },
      transform);
  }
}
