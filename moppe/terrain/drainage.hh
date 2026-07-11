#ifndef MOPPE_TERRAIN_DRAINAGE_HH
#define MOPPE_TERRAIN_DRAINAGE_HH

#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/terrain_view.hh>

#include <cstdint>
#include <vector>

namespace moppe::terrain {
  enum class DrainageRouting {
    D8
  };

  struct DrainageParameters {
    DrainageRouting routing = DrainageRouting::D8;
  };

  // A compact functional graph: every cell points to one strictly lower
  // receiver, or to itself when it is a sink. Rasters contain unique torus
  // samples, without the duplicated rendering seam.
  struct DrainageGraph {
    TerrainGrid source_grid;
    std::vector<std::uint32_t> receiver;
    ScalarRaster slope;
    ScalarRaster contributing_area;
    std::vector<std::uint32_t> basin;
    std::vector<std::uint32_t> sinks;

    std::size_t width () const noexcept {
      return source_grid.unique_width ();
    }

    std::size_t height () const noexcept {
      return source_grid.unique_height ();
    }
  };

  DrainageGraph analyze_drainage
    (const TerrainView& terrain,
     const DrainageParameters& parameters = { });
}

#endif
