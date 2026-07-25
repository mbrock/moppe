#ifndef MOPPE_MAP_TERRAIN_EVALUATOR_HH
#define MOPPE_MAP_TERRAIN_EVALUATOR_HH

#include <moppe/map/surface.hh>
#include <moppe/terrain/program.hh>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace moppe::map {
  // A resumable value produced at a materialization barrier. Capturing the
  // terrain, change ledgers, and lagged channel tangent makes a resumed
  // program equivalent to evaluating the same prefix again.
  struct TerrainCheckpoint {
    std::vector<SurfaceElevation> elevations;
    std::vector<ErodedSurfaceMaterial> eroded;
    std::vector<DepositedSurfaceMaterial> deposited;
    std::vector<terrain::ChannelTangent> channel_tangents;
  };

  // Interprets terrain-language values against the authoritative surface
  // geometry bundle. This class owns program order, progress, and resumable
  // history.
  class TerrainEvaluator {
  public:
    using Progress =
      std::function<void (std::size_t, const terrain::TerrainTransform&)>;
    using IterationProgress = std::function<void (
      std::size_t, const terrain::TerrainTransform&, int, int)>;
    using SourceProgress = std::function<void (std::size_t, std::size_t)>;

    explicit TerrainEvaluator (
      Surface& target,
      const terrain::StreamPowerEvolutionBackend* evolution_backend = nullptr);

    void begin (const terrain::TerrainProgram& program,
                const SourceProgress& source_progress = {});
    terrain::TerrainTransformReport
    apply (const terrain::TerrainTransform& transform);
    void evaluate (const terrain::TerrainProgram& program,
                   const Progress& progress = {},
                   const IterationProgress& iteration_progress = {},
                   const SourceProgress& source_progress = {});

    TerrainCheckpoint checkpoint () const;
    void restore (const TerrainCheckpoint& checkpoint);

    const std::optional<terrain::TrailNetwork>& trail_network () const {
      return m_trail_network;
    }
    std::optional<terrain::TrailNetwork> release_trail_network () {
      return std::move (m_trail_network);
    }
    std::span<const terrain::ChannelTangent> channel_tangents () const {
      return m_channel_tangents;
    }
    std::vector<terrain::ChannelTangent> release_channel_tangents () {
      return std::move (m_channel_tangents);
    }

  private:
    Surface& m_target;
    const terrain::StreamPowerEvolutionBackend* m_evolution_backend;
    std::vector<terrain::UpliftWeight> m_uplift;
    std::vector<terrain::ChannelTangent> m_channel_tangents;
    std::optional<terrain::TrailNetwork> m_trail_network;
    IterationProgress m_iteration_progress;
    std::size_t m_transform_index = 0;
  };
}

#endif
