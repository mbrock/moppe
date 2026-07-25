#include <moppe/map/terrain_evaluator.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/trail.hh>

#include <algorithm>
#include <stdexcept>
#include <variant>

namespace moppe::map {
  TerrainEvaluator::TerrainEvaluator (
    Surface& target,
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
    const terrain::ScalarRaster continent = [&] {
      MOPPE_PROFILE_ZONE ("terrain.materialize_height_source");
      return evaluator.evaluate (
        fields.continent.untyped (),
        m_target.discretization ().field_sampling_grid ());
    }();

    MOPPE_PROFILE_ZONE ("terrain.shape_initial_orogeny_relief");
    const float land_relief = meters_value (program.source.initial_land_relief);
    const float bathymetric_relief =
      meters_value (program.source.initial_bathymetric_relief);
    for (int y = 0; y < m_target.height (); ++y)
      for (int x = 0; x < m_target.width (); ++x) {
        const std::size_t offset =
          static_cast<std::size_t> (y) * m_target.width () + x;
        const float continent_value =
          continent.values ()[offset] - program.source.coastline;
        const float relief =
          continent_value < 0.0f ? bathymetric_relief : land_relief;
        m_target.set_elevation (
          x,
          y,
          SurfaceElevation (
            (program.source.sea_level + relief * continent_value) *
            terrain::surface_elevation[u::m]));
      }
    m_target.reset_material_history ();

    m_relative_uplift.clear ();
    MOPPE_PROFILE_ZONE ("terrain.materialize_uplift_field");
    const terrain::CpuEvaluator uplift_cpu_evaluator;
    const terrain::FieldEvaluator& uplift_evaluator =
      m_source_evaluator ? *m_source_evaluator : uplift_cpu_evaluator;
    const terrain::RelativeUpliftRaster uplift =
      terrain::materialize (uplift_evaluator,
                            fields.uplift,
                            m_target.discretization ().field_sampling_grid ());
    m_relative_uplift.resize (static_cast<std::size_t> (m_target.width ()) *
                              m_target.height ());
    for (int y = 0; y < m_target.height (); ++y)
      for (int x = 0; x < m_target.width (); ++x)
        m_relative_uplift[static_cast<std::size_t> (y) * m_target.width () +
                          x] =
          uplift
            .values ()[static_cast<std::size_t> (y) * m_target.width () + x];
  }

  terrain::TerrainTransformReport
  TerrainEvaluator::apply (const terrain::TerrainTransform& transform) {
    MOPPE_PROFILE_ZONE ("TerrainEvaluator::apply");
    if (!std::holds_alternative<terrain::TrailFormation> (transform))
      m_trail_network.reset ();
    terrain::TerrainTransformReport report;
    if (const auto* orogeny =
          std::get_if<terrain::OrogenyEvolution> (&transform)) {
      MOPPE_PROFILE_ZONE ("terrain.orogeny_evolution");
      const std::size_t sample_count =
        static_cast<std::size_t> (m_target.width ()) * m_target.height ();
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
          const std::size_t width = m_target.width ();
          const std::size_t height = m_target.height ();
          for (std::size_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < width; ++x)
              m_target.set_elevation (
                static_cast<int> (x),
                static_cast<int> (y),
                SurfaceElevation (heights[y * width + x] *
                                  terrain::surface_elevation[u::m]));
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
      const std::size_t width = m_target.width ();
      const std::size_t height = m_target.height ();
      for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
          const float updated = result.heights[y * width + x];
          m_target.set_elevation (
            static_cast<int> (x),
            static_cast<int> (y),
            SurfaceElevation (updated * terrain::surface_elevation[u::m]));
        }
      report = result.report;
      m_channel_tangents = std::move (result.channel_tangents);
    } else if (const auto* trails =
                 std::get_if<terrain::TrailFormation> (&transform)) {
      MOPPE_PROFILE_ZONE ("terrain.trail_formation");
      terrain::TrailFormationResult result =
        terrain::form_trails (m_target.terrain_view (), *trails);
      const std::size_t width = m_target.width ();
      const std::size_t height = m_target.height ();
      for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
          const float updated = result.heights[y * width + x];
          m_target.record_material_change (
            static_cast<int> (x),
            static_cast<int> (y),
            updated - terrain::surface_elevation_value (m_target.elevation_at (
                        static_cast<int> (x), static_cast<int> (y))));
          m_target.set_elevation (
            static_cast<int> (x),
            static_cast<int> (y),
            SurfaceElevation (updated * terrain::surface_elevation[u::m]));
        }
      report = result.report;
      m_trail_network = std::move (result.network);
    }
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
    return { .elevations = m_target.elevations (),
             .eroded = m_target.eroded_material (),
             .deposited = m_target.deposited_material (),
             .channel_tangents = m_channel_tangents };
  }

  void TerrainEvaluator::restore (const TerrainCheckpoint& checkpoint) {
    const std::size_t expected =
      static_cast<std::size_t> (m_target.width ()) * m_target.height ();
    if (checkpoint.elevations.size () != expected)
      throw std::invalid_argument (
        "terrain checkpoint dimensions do not match target");
    m_target.elevations () = checkpoint.elevations;
    m_target.reset_material_history ();
    if (checkpoint.eroded.size () == expected)
      m_target.eroded_material () = checkpoint.eroded;
    if (checkpoint.deposited.size () == expected)
      m_target.deposited_material () = checkpoint.deposited;
    const std::size_t unique =
      static_cast<std::size_t> (m_target.width ()) * m_target.height ();
    if (checkpoint.channel_tangents.size () == unique)
      m_channel_tangents = checkpoint.channel_tangents;
    else
      m_channel_tangents.clear ();
    m_trail_network.reset ();
  }
}
