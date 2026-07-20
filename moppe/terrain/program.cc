#include <moppe/terrain/program.hh>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace moppe::terrain {
  namespace {
    std::string format_program_transform_float (float value, int precision) {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision (precision) << value;
      return stream.str ();
    }

    std::string format_program_transform_count (int value) {
      std::string text = std::to_string (value);
      for (int i = static_cast<int> (text.size ()) - 3; i > 0; i -= 3)
        text.insert (static_cast<std::size_t> (i), ",");
      return text;
    }

    std::string format_program_transform_ledger (double value) {
      std::ostringstream stream;
      stream << std::scientific << std::setprecision (2) << value;
      return stream.str ();
    }

    [[noreturn]] void invalid_program_transform_property_index () {
      throw std::out_of_range ("terrain transform property index is invalid");
    }

  }

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
      profile == TerrainGenerationProfile::Fast       ? 750000.0f
      : profile == TerrainGenerationProfile::Play     ? 1500000.0f
      : profile == TerrainGenerationProfile::Research ? 2000000.0f
                                                      : 750000.0f;
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

  std::size_t GeologicalSource::property_count () const noexcept {
    return 9;
  }

  TransformProperty GeologicalSource::property (std::size_t index) const {
    switch (index) {
    case 0:
      return { "WARP STRENGTH",
               format_program_transform_float (recipe.warp.amplitude, 3),
               ParameterDomain::Continuous };
    case 1:
      return { "CONTINENT WAVES",
               std::to_string (recipe.continent.noise.cycles),
               ParameterDomain::Natural };
    case 2:
      return { "PLAINS WAVES",
               std::to_string (recipe.plains.noise.cycles),
               ParameterDomain::Natural };
    case 3:
      return { "RIDGE WAVES",
               std::to_string (recipe.mountains.cycles),
               ParameterDomain::Natural };
    case 4:
      return { "MASK START",
               format_program_transform_float (recipe.blend.mask_low, 3),
               ParameterDomain::Continuous };
    case 5:
      return { "MASK END",
               format_program_transform_float (recipe.blend.mask_high, 3),
               ParameterDomain::Continuous };
    case 6:
      return { "CONTINENT MIX",
               format_program_transform_float (recipe.blend.continent_weight,
                                               2),
               ParameterDomain::Continuous };
    case 7:
      return { "PLAINS MIX",
               format_program_transform_float (recipe.blend.plains_weight, 2),
               ParameterDomain::Continuous };
    case 8:
      return { "MOUNTAIN MIX",
               format_program_transform_float (recipe.blend.mountain_weight, 2),
               ParameterDomain::Continuous };
    default:
      invalid_program_transform_property_index ();
    }
  }

  float GeologicalSource::normalized_property (std::size_t index) const {
    switch (index) {
    case 0:
      return normalized_edit_value (recipe.warp.amplitude, 0.0f, 0.6f);
    case 1:
      return normalized_edit_value (
        static_cast<float> (recipe.continent.noise.cycles), 1.0f, 16.0f);
    case 2:
      return normalized_edit_value (
        static_cast<float> (recipe.plains.noise.cycles), 1.0f, 32.0f);
    case 3:
      return normalized_edit_value (
        static_cast<float> (recipe.mountains.cycles), 1.0f, 8.0f);
    case 4:
      return normalized_edit_value (
        recipe.blend.mask_low, 0.0f, recipe.blend.mask_high - 0.001f);
    case 5:
      return normalized_edit_value (
        recipe.blend.mask_high, recipe.blend.mask_low + 0.001f, 1.0f);
    case 6:
      return normalized_edit_value (recipe.blend.continent_weight, 0.0f, 1.5f);
    case 7:
      return normalized_edit_value (recipe.blend.plains_weight, 0.0f, 1.0f);
    case 8:
      return normalized_edit_value (recipe.blend.mountain_weight, 0.0f, 1.5f);
    default:
      invalid_program_transform_property_index ();
    }
  }

  bool GeologicalSource::set_normalized_property (std::size_t index,
                                                  float value) {
    switch (index) {
    case 0:
      return replace_edit_value (recipe.warp.amplitude,
                                 edited_value (value, 0.0f, 0.6f));
    case 1:
    case 2:
    case 3:
      return false;
    case 4:
      return replace_edit_value (
        recipe.blend.mask_low,
        edited_value (value, 0.0f, recipe.blend.mask_high - 0.001f));
    case 5:
      return replace_edit_value (
        recipe.blend.mask_high,
        edited_value (value, recipe.blend.mask_low + 0.001f, 1.0f));
    case 6:
      return replace_edit_value (recipe.blend.continent_weight,
                                 edited_value (value, 0.0f, 1.5f));
    case 7:
      return replace_edit_value (recipe.blend.plains_weight,
                                 edited_value (value, 0.0f, 1.0f));
    case 8:
      return replace_edit_value (recipe.blend.mountain_weight,
                                 edited_value (value, 0.0f, 1.5f));
    default:
      invalid_program_transform_property_index ();
    }
  }

  bool GeologicalSource::adjust_natural_property (std::size_t index,
                                                  int direction) {
    if (direction == 0)
      return false;
    switch (index) {
    case 1: {
      const int value =
        std::clamp (recipe.continent.noise.cycles + direction, 1, 16);
      return replace_edit_value (recipe.continent.noise.cycles, value);
    }
    case 2: {
      const int value =
        std::clamp (recipe.plains.noise.cycles + direction, 1, 32);
      return replace_edit_value (recipe.plains.noise.cycles, value);
    }
    case 3: {
      const int value = std::clamp (recipe.mountains.cycles + direction, 1, 8);
      return replace_edit_value (recipe.mountains.cycles, value);
    }
    case 0:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
      return false;
    default:
      invalid_program_transform_property_index ();
    }
  }

  void NormalizeHeights::validate () const {}

  TransformDescription NormalizeHeights::description () const noexcept {
    return { "normalize",
             "NORMALIZE",
             { SpatialScope::Global, EvaluationOrder::Reduction } };
  }

  std::string NormalizeHeights::detail () const {
    return "map sampled range to 0..1";
  }

  std::size_t NormalizeHeights::property_count () const noexcept {
    return 0;
  }

  TransformProperty NormalizeHeights::property (std::size_t) const {
    invalid_program_transform_property_index ();
  }

  float NormalizeHeights::normalized_property (std::size_t index) const {
    property (index);
    return 0.0f;
  }

  bool NormalizeHeights::set_normalized_property (std::size_t index, float) {
    property (index);
    return false;
  }

  bool NormalizeHeights::adjust_natural_property (std::size_t index, int) {
    property (index);
    return false;
  }

  void PowerHeights::validate () const {
    if (!std::isfinite (exponent) || exponent <= 0.0f)
      throw std::invalid_argument (
        "height exponent must be positive and finite");
  }

  TransformDescription PowerHeights::description () const noexcept {
    return { "power",
             "POWER CURVE",
             { SpatialScope::Pointwise, EvaluationOrder::Direct } };
  }

  std::string PowerHeights::detail () const {
    return "height ^ " + format_program_transform_float (exponent, 2);
  }

  std::size_t PowerHeights::property_count () const noexcept {
    return 1;
  }

  TransformProperty PowerHeights::property (std::size_t index) const {
    if (index != 0)
      invalid_program_transform_property_index ();
    return { "EXPONENT",
             format_program_transform_float (exponent, 2),
             ParameterDomain::Continuous };
  }

  float PowerHeights::normalized_property (std::size_t index) const {
    if (index != 0)
      invalid_program_transform_property_index ();
    return normalized_edit_value (exponent, 0.1f, 4.0f);
  }

  bool PowerHeights::set_normalized_property (std::size_t index, float value) {
    if (index != 0)
      invalid_program_transform_property_index ();
    return replace_edit_value (exponent, edited_value (value, 0.1f, 4.0f));
  }

  bool PowerHeights::adjust_natural_property (std::size_t index, int) {
    if (index != 0)
      invalid_program_transform_property_index ();
    return false;
  }

  void ThermalErosion::validate () const {
    if (iterations < 0 || !std::isfinite (talus) || talus < 0.0f)
      throw std::invalid_argument ("thermal erosion parameters are invalid");
  }

  TransformDescription ThermalErosion::description () const noexcept {
    return { "thermal",
             "TALUS RELAX",
             { SpatialScope::Neighborhood, EvaluationOrder::Iterative } };
  }

  std::string ThermalErosion::detail () const {
    return std::to_string (count_value (iterations)) + " passes @ " +
           format_program_transform_float (talus, 4);
  }

  std::size_t ThermalErosion::property_count () const noexcept {
    return 2;
  }

  TransformProperty ThermalErosion::property (std::size_t index) const {
    if (index == 0)
      return { "PASSES",
               std::to_string (count_value (iterations)),
               ParameterDomain::Natural };
    if (index == 1)
      return { "TALUS",
               format_program_transform_float (talus, 4),
               ParameterDomain::Continuous };
    invalid_program_transform_property_index ();
  }

  float ThermalErosion::normalized_property (std::size_t index) const {
    if (index == 0)
      return normalized_edit_value (
        static_cast<float> (count_value (iterations)), 0.0f, 20.0f);
    if (index == 1)
      return normalized_edit_value (talus, 0.0f, 0.05f);
    invalid_program_transform_property_index ();
  }

  bool ThermalErosion::set_normalized_property (std::size_t index,
                                                float value) {
    if (index == 0)
      return false;
    if (index == 1)
      return replace_edit_value (talus, edited_value (value, 0.0f, 0.05f));
    invalid_program_transform_property_index ();
  }

  bool ThermalErosion::adjust_natural_property (std::size_t index,
                                                int direction) {
    if (index != 0) {
      if (index >= property_count ())
        invalid_program_transform_property_index ();
      return false;
    }
    if (direction == 0)
      return false;
    const IterationCount value = iteration_count (
      std::clamp (count_value (iterations) + direction, 0, 20));
    return replace_edit_value (iterations, value);
  }

  void OrogenyEvolution::validate () const {
    const StreamPowerEvolution& parameters = evolution;
    if (!std::isfinite (meters_per_julian_year_value (maximum_uplift_rate)) ||
        maximum_uplift_rate <
          0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year ||
        !std::isfinite (julian_years_value (parameters.duration)) ||
        parameters.duration < 0.0f * mp_units::astronomy::Julian_year ||
        !std::isfinite (julian_years_value (parameters.time_step)) ||
        parameters.time_step <= 0.0f * mp_units::astronomy::Julian_year ||
        !std::isfinite (
          meters_per_julian_year_value (parameters.reference_incision_rate)) ||
        parameters.reference_incision_rate <
          0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year ||
        !std::isfinite (square_meters_value (parameters.reference_area)) ||
        parameters.reference_area <=
          0.0f * mp_units::si::metre * mp_units::si::metre ||
        !std::isfinite (parameters.area_exponent) ||
        parameters.area_exponent < 0.0f ||
        !std::isfinite (
          square_meters_per_julian_year_value (parameters.diffusivity)) ||
        parameters.diffusivity < 0.0f * mp_units::si::metre *
                                   mp_units::si::metre /
                                   mp_units::astronomy::Julian_year ||
        !std::isfinite (parameters.sea_level) ||
        !std::isfinite (
          parameters.channel_persistence.numerical_value_in (mp_units::one)) ||
        parameters.channel_persistence <
          0.0f * channel_persistence[mp_units::one] ||
        parameters.channel_persistence >=
          1.0f * channel_persistence[mp_units::one])
      throw std::invalid_argument ("orogeny evolution parameters are invalid");
  }

  TransformDescription OrogenyEvolution::description () const noexcept {
    return { "orogeny",
             "OROGENY EVOLUTION",
             { SpatialScope::Global, EvaluationOrder::Iterative } };
  }

  std::string OrogenyEvolution::detail () const {
    const float duration = julian_years_value (evolution.duration);
    const float time_step = julian_years_value (evolution.time_step);
    return format_program_transform_float (duration / 1000.0f, 0) + " ky / " +
           format_program_transform_count (
             static_cast<int> (std::ceil (duration / time_step))) +
           " geological steps";
  }

  std::size_t OrogenyEvolution::property_count () const noexcept {
    return 8;
  }

  TransformProperty OrogenyEvolution::property (std::size_t index) const {
    switch (index) {
    case 0:
      return { "DURATION (KY)",
               format_program_transform_float (
                 julian_years_value (evolution.duration) / 1000.0f, 0),
               ParameterDomain::Continuous };
    case 1:
      return { "STEP (KY)",
               format_program_transform_float (
                 julian_years_value (evolution.time_step) / 1000.0f, 0),
               ParameterDomain::Continuous };
    case 2:
      return { "UPLIFT (MM/Y)",
               format_program_transform_float (
                 meters_per_julian_year_value (maximum_uplift_rate) * 1000.0f,
                 2),
               ParameterDomain::Continuous };
    case 3:
      return { "INCISION AT 1 M2 (M/Y)",
               format_program_transform_ledger (meters_per_julian_year_value (
                 evolution.reference_incision_rate)),
               ParameterDomain::Continuous };
    case 4:
      return { "AREA EXPONENT",
               format_program_transform_float (evolution.area_exponent, 2),
               ParameterDomain::Continuous };
    case 5:
      return { "DIFFUSIVITY",
               format_program_transform_float (
                 square_meters_per_julian_year_value (evolution.diffusivity),
                 5),
               ParameterDomain::Continuous };
    case 6:
      return {
        "CHANNEL MEMORY",
        format_program_transform_float (
          evolution.channel_persistence.numerical_value_in (mp_units::one), 2),
        ParameterDomain::Continuous
      };
    case 7:
      return { "SEA LEVEL",
               format_program_transform_float (evolution.sea_level, 3),
               ParameterDomain::Continuous };
    default:
      invalid_program_transform_property_index ();
    }
  }

  float OrogenyEvolution::normalized_property (std::size_t index) const {
    switch (index) {
    case 0:
      return normalized_edit_value (
        julian_years_value (evolution.duration), 0.0f, 5000000.0f);
    case 1:
      return normalized_edit_value (
        julian_years_value (evolution.time_step), 1000.0f, 250000.0f);
    case 2:
      return normalized_edit_value (
        meters_per_julian_year_value (maximum_uplift_rate), 0.0f, 0.003f);
    case 3:
      return normalized_edit_value (std::log10 (meters_per_julian_year_value (
                                      evolution.reference_incision_rate)),
                                    -7.0f,
                                    -3.0f);
    case 4:
      return normalized_edit_value (evolution.area_exponent, 0.0f, 1.0f);
    case 5:
      return normalized_edit_value (
        square_meters_per_julian_year_value (evolution.diffusivity),
        0.0f,
        0.005f);
    case 6:
      return normalized_edit_value (
        evolution.channel_persistence.numerical_value_in (mp_units::one),
        0.0f,
        0.95f);
    case 7:
      return normalized_edit_value (evolution.sea_level, 0.0f, 0.3f);
    default:
      invalid_program_transform_property_index ();
    }
  }

  bool OrogenyEvolution::set_normalized_property (std::size_t index,
                                                  float value) {
    switch (index) {
    case 0:
      return replace_edit_value (evolution.duration,
                                 edited_value (value, 0.0f, 5000000.0f) *
                                   mp_units::astronomy::Julian_year);
    case 1:
      return replace_edit_value (evolution.time_step,
                                 edited_value (value, 1000.0f, 250000.0f) *
                                   mp_units::astronomy::Julian_year);
    case 2:
      return replace_edit_value (maximum_uplift_rate,
                                 edited_value (value, 0.0f, 0.003f) *
                                   mp_units::si::metre /
                                   mp_units::astronomy::Julian_year);
    case 3:
      return replace_edit_value (
        evolution.reference_incision_rate,
        std::pow (10.0f, edited_value (value, -7.0f, -3.0f)) *
          mp_units::si::metre / mp_units::astronomy::Julian_year);
    case 4:
      return replace_edit_value (evolution.area_exponent,
                                 edited_value (value, 0.0f, 1.0f));
    case 5:
      return replace_edit_value (evolution.diffusivity,
                                 edited_value (value, 0.0f, 0.005f) *
                                   mp_units::si::metre * mp_units::si::metre /
                                   mp_units::astronomy::Julian_year);
    case 6:
      return replace_edit_value (evolution.channel_persistence,
                                 edited_value (value, 0.0f, 0.95f) *
                                   channel_persistence[mp_units::one]);
    case 7:
      return replace_edit_value (evolution.sea_level,
                                 edited_value (value, 0.0f, 0.3f));
    default:
      invalid_program_transform_property_index ();
    }
  }

  bool OrogenyEvolution::adjust_natural_property (std::size_t index, int) {
    if (index >= property_count ())
      invalid_program_transform_property_index ();
    return false;
  }

  void validate_transform (const TerrainTransform& transform) {
    std::visit ([] (const auto& operation) { operation.validate (); },
                transform);
  }

  TransformDescription
  terrain_transform_description (const TerrainTransform& transform) {
    return std::visit (
      [] (const auto& operation) { return operation.description (); },
      transform);
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
    for (const TerrainTransform& transform : program.transforms)
      validate_transform (transform);
  }

  std::string_view terrain_transform_id (const TerrainTransform& transform) {
    return terrain_transform_description (transform).id;
  }

  TransformSemantics
  terrain_transform_semantics (const TerrainTransform& transform) {
    return terrain_transform_description (transform).semantics;
  }

  std::string terrain_transform_detail (const TerrainTransform& transform) {
    return std::visit (
      [] (const auto& operation) { return operation.detail (); }, transform);
  }

  std::size_t
  terrain_transform_property_count (const TerrainTransform& transform) {
    return std::visit (
      [] (const auto& operation) { return operation.property_count (); },
      transform);
  }

  TransformProperty
  terrain_transform_property (const TerrainTransform& transform,
                              std::size_t index) {
    return std::visit (
      [index] (const auto& operation) { return operation.property (index); },
      transform);
  }
}
