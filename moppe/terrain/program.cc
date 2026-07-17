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
