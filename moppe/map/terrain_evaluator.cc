#include <moppe/map/terrain_evaluator.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/analytical_erosion.hh>
#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/trail.hh>

#include <algorithm>
#include <stdexcept>
#include <variant>

namespace moppe::map {
  TerrainEvaluator::TerrainEvaluator (
    RandomHeightMap& target,
    const terrain::FieldEvaluator* source_evaluator,
    const terrain::StreamPowerEvolutionBackend* evolution_backend)
      : m_target (target), m_source_evaluator (source_evaluator),
        m_evolution_backend (evolution_backend) {}

  void TerrainEvaluator::begin (const terrain::TerrainProgram& program,
                                const SourceProgress& source_progress) {
    MOPPE_PROFILE_ZONE ("TerrainEvaluator::begin");
    terrain::validate_program (program);
    m_trail_network.reset ();
    m_channel_tangents.clear ();
    const terrain::GeologicalFields fields = [&] {
      MOPPE_PROFILE_ZONE ("terrain.expand_geological_recipe");
      return terrain::make_geological_fields (program.source.recipe);
    }();
    const terrain::CpuEvaluator cpu_evaluator (source_progress);
    const terrain::FieldEvaluator& evaluator =
      m_source_evaluator ? *m_source_evaluator : cpu_evaluator;
    const bool has_orogeny = std::ranges::any_of (
      program.transforms, [] (const terrain::TerrainTransform& transform) {
        return std::holds_alternative<terrain::OrogenyEvolution> (transform);
      });
    const bool orogeny_source =
      program.source.mode == terrain::GeologicalSource::Mode::Orogeny;
    const terrain::ScalarField field =
      orogeny_source ? fields.continent.untyped ()
                     : terrain::geological_layer (fields, program.source.layer);
    {
      MOPPE_PROFILE_ZONE ("terrain.materialize_height_source");
      m_target.materialize (field, evaluator);
    }

    if (orogeny_source) {
      MOPPE_PROFILE_ZONE ("terrain.shape_initial_orogeny_relief");
      const float height_scale_m = m_target.scale ()[1];
      const float land_relief =
        meters_value (program.source.initial_land_relief) / height_scale_m;
      const float bathymetric_relief =
        meters_value (program.source.initial_bathymetric_relief) /
        height_scale_m;
      for (int y = 0; y < m_target.unique_height (); ++y)
        for (int x = 0; x < m_target.unique_width (); ++x) {
          const float continent =
            m_target.get (x, y) - program.source.coastline;
          const float relief =
            continent < 0.0f ? bathymetric_relief : land_relief;
          m_target.set (x, y, program.source.sea_level + relief * continent);
        }
      m_target.synchronize_periodic_edges ();
    }

    m_relative_uplift.clear ();
    if (has_orogeny) {
      MOPPE_PROFILE_ZONE ("terrain.materialize_uplift_field");
      const terrain::CpuEvaluator uplift_cpu_evaluator;
      const terrain::FieldEvaluator& uplift_evaluator =
        m_source_evaluator ? *m_source_evaluator : uplift_cpu_evaluator;
      const terrain::RelativeUpliftRaster uplift = terrain::materialize (
        uplift_evaluator,
        fields.uplift,
        m_target.discretization ().field_sampling_grid ());
      m_relative_uplift.resize (
        static_cast<std::size_t> (m_target.unique_width ()) *
        m_target.unique_height ());
      for (int y = 0; y < m_target.unique_height (); ++y)
        for (int x = 0; x < m_target.unique_width (); ++x)
          m_relative_uplift[static_cast<std::size_t> (y) *
                              m_target.unique_width () +
                            x] =
            uplift
              .values ()[static_cast<std::size_t> (y) * m_target.width () + x];
    }
  }

  terrain::TerrainTransformReport
  TerrainEvaluator::apply (const terrain::TerrainTransform& transform) {
    MOPPE_PROFILE_ZONE ("TerrainEvaluator::apply");
    if (!std::holds_alternative<terrain::TrailFormation> (transform))
      m_trail_network.reset ();
    terrain::TerrainTransformReport report;
    if (std::holds_alternative<terrain::NormalizeHeights> (transform)) {
      MOPPE_PROFILE_ZONE ("terrain.normalize_heights");
      m_target.normalize ();
    } else if (const auto* power =
                 std::get_if<terrain::PowerHeights> (&transform)) {
      MOPPE_PROFILE_ZONE ("terrain.power_heights");
      m_target.exponentiate (power->exponent);
    } else if (const auto* analytical =
                 std::get_if<terrain::AnalyticalErosion> (&transform)) {
      MOPPE_PROFILE_ZONE ("terrain.analytical_erosion");
      terrain::AnalyticalErosionResult result =
        terrain::erode_analytically (m_target.terrain_view (), *analytical);
      const std::size_t width = m_target.unique_width ();
      const std::size_t height = m_target.unique_height ();
      for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
          const float updated = result.heights[y * width + x];
          m_target.record_material_change (
            static_cast<int> (x),
            static_cast<int> (y),
            updated -
              m_target.get (static_cast<int> (x), static_cast<int> (y)));
          m_target.set (static_cast<int> (x), static_cast<int> (y), updated);
        }
      report = result.report;
    } else if (const auto* orogeny =
                 std::get_if<terrain::OrogenyEvolution> (&transform)) {
      MOPPE_PROFILE_ZONE ("terrain.orogeny_evolution");
      const std::size_t sample_count =
        static_cast<std::size_t> (m_target.unique_width ()) *
        m_target.unique_height ();
      if (m_relative_uplift.size () != sample_count)
        throw std::logic_error (
          "orogeny evolution requires an uplift field materialized by begin");
      const float maximum_uplift =
        meters_per_julian_year_value (orogeny->maximum_uplift_rate);
      std::vector<meters_per_julian_year_t> uplift;
      uplift.reserve (m_relative_uplift.size ());
      for (const float relative : m_relative_uplift)
        uplift.push_back (relative * maximum_uplift * mp_units::si::metre /
                          mp_units::astronomy::Julian_year);
      const terrain::StreamPowerProgress iteration_progress =
        [this, &transform] (
          int completed, int total, std::span<const float> heights) {
          const std::size_t width = m_target.unique_width ();
          const std::size_t height = m_target.unique_height ();
          for (std::size_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < width; ++x)
              m_target.set (static_cast<int> (x),
                            static_cast<int> (y),
                            heights[y * width + x]);
          m_target.synchronize_periodic_edges ();
          if (m_iteration_progress)
            m_iteration_progress (
              m_transform_index, transform, completed, total);
        };
      terrain::StreamPowerEvolutionResult result =
        m_evolution_backend
          ? terrain::evolve_stream_power (m_target.terrain_view (),
                                          uplift,
                                          orogeny->evolution,
                                          *m_evolution_backend,
                                          iteration_progress,
                                          m_channel_tangents)
          : terrain::evolve_stream_power (m_target.terrain_view (),
                                          uplift,
                                          orogeny->evolution,
                                          iteration_progress,
                                          m_channel_tangents);
      const std::size_t width = m_target.unique_width ();
      const std::size_t height = m_target.unique_height ();
      for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
          const float updated = result.heights[y * width + x];
          m_target.set (static_cast<int> (x), static_cast<int> (y), updated);
        }
      report = result.report;
      m_channel_tangents = std::move (result.channel_tangents);
    } else if (const auto* thermal =
                 std::get_if<terrain::ThermalErosion> (&transform)) {
      MOPPE_PROFILE_ZONE ("terrain.thermal_erosion");
      m_target.erode_thermally (terrain::count_value (thermal->iterations),
                                thermal->talus);
    } else if (const auto* diffusion =
                 std::get_if<terrain::HillslopeDiffusion> (&transform)) {
      MOPPE_PROFILE_ZONE ("terrain.hillslope_diffusion");
      report = m_target.diffuse_hillslopes (diffusion->duration,
                                            diffusion->diffusivity);
    } else if (const auto* trails =
                 std::get_if<terrain::TrailFormation> (&transform)) {
      MOPPE_PROFILE_ZONE ("terrain.trail_formation");
      terrain::TrailFormationResult result =
        terrain::form_trails (m_target.terrain_view (), *trails);
      const std::size_t width = m_target.unique_width ();
      const std::size_t height = m_target.unique_height ();
      for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
          const float updated = result.heights[y * width + x];
          m_target.record_material_change (
            static_cast<int> (x),
            static_cast<int> (y),
            updated -
              m_target.get (static_cast<int> (x), static_cast<int> (y)));
          m_target.set (static_cast<int> (x), static_cast<int> (y), updated);
        }
      report = result.report;
      m_trail_network = std::move (result.network);
    }
    m_target.synchronize_periodic_edges ();
    return report;
  }

  void TerrainEvaluator::evaluate (const terrain::TerrainProgram& program,
                                   const Progress& progress,
                                   const IterationProgress& iteration_progress,
                                   const SourceProgress& source_progress) {
    MOPPE_PROFILE_ZONE ("TerrainEvaluator::evaluate");
    begin (program, source_progress);
    m_iteration_progress = iteration_progress;
    for (std::size_t i = 0; i < program.transforms.size (); ++i) {
      m_transform_index = i;
      if (progress)
        progress (i, program.transforms[i]);
      apply (program.transforms[i]);
    }
    m_iteration_progress = {};
  }

  TerrainCheckpoint TerrainEvaluator::checkpoint () const {
    const std::size_t count =
      static_cast<std::size_t> (m_target.width ()) * m_target.height ();
    return { .heights = std::vector<float> (m_target.raw_heights (),
                                            m_target.raw_heights () + count),
             .eroded = std::vector<float> (m_target.raw_eroded (),
                                           m_target.raw_eroded () + count),
             .deposited = std::vector<float> (
               m_target.raw_deposited (), m_target.raw_deposited () + count),
             .channel_tangents = m_channel_tangents };
  }

  void TerrainEvaluator::restore (const TerrainCheckpoint& checkpoint) {
    const std::size_t expected =
      static_cast<std::size_t> (m_target.width ()) * m_target.height ();
    if (checkpoint.heights.size () != expected)
      throw std::invalid_argument (
        "terrain checkpoint dimensions do not match target");
    std::copy (checkpoint.heights.begin (),
               checkpoint.heights.end (),
               m_target.raw_heights ());
    // Checkpoints predating the sediment ledger restore it as empty.
    m_target.reset_sediment_ledger ();
    if (checkpoint.eroded.size () == expected)
      std::copy (checkpoint.eroded.begin (),
                 checkpoint.eroded.end (),
                 m_target.raw_eroded ());
    if (checkpoint.deposited.size () == expected)
      std::copy (checkpoint.deposited.begin (),
                 checkpoint.deposited.end (),
                 m_target.raw_deposited ());
    const std::size_t unique =
      static_cast<std::size_t> (m_target.unique_width ()) *
      m_target.unique_height ();
    if (checkpoint.channel_tangents.size () == unique)
      m_channel_tangents = checkpoint.channel_tangents;
    else
      m_channel_tangents.clear ();
    m_trail_network.reset ();
  }
}
