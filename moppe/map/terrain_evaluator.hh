#ifndef MOPPE_MAP_TERRAIN_EVALUATOR_HH
#define MOPPE_MAP_TERRAIN_EVALUATOR_HH

#include <moppe/map/generate.hh>
#include <moppe/terrain/program.hh>

#include <cstddef>
#include <functional>
#include <random>
#include <vector>

namespace moppe::map {
  // A resumable value produced at a materialization barrier.  Capturing the
  // random stream together with the raster makes a resumed program exactly
  // equivalent to evaluating the same prefix again.
  struct TerrainCheckpoint {
    std::vector<float> heights;
    std::mt19937 randomness;
  };

  // Interprets terrain-language values against concrete heightmap storage.
  // RandomHeightMap owns samples and legacy shaping kernels; this class owns
  // program order, random-stream position, progress, and resumable history.
  class TerrainEvaluator {
  public:
    using Progress = std::function
      <void (std::size_t, const terrain::TerrainTransform&)>;

    explicit TerrainEvaluator (RandomHeightMap& target);

    void begin (const terrain::TerrainProgram& program);
    void apply (const terrain::TerrainTransform& transform);
    void evaluate (const terrain::TerrainProgram& program,
		   const Progress& progress = { });

    TerrainCheckpoint checkpoint () const;
    void restore (const TerrainCheckpoint& checkpoint);

  private:
    RandomHeightMap& m_target;
    std::mt19937 m_randomness;
  };
}

#endif
