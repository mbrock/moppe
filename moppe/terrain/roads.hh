#ifndef MOPPE_TERRAIN_ROADS_HH
#define MOPPE_TERRAIN_ROADS_HH

#include <moppe/terrain/domain.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace moppe::terrain {
  // A first reading of how the world might be made passable: the places it
  // already points at, and the roads and footpaths that would join them.
  // Nothing here edits the ground; it is drawn on the map to be judged
  // (ideas/second-author.md's centers and route network, as an overlay).
  struct RoadPlace {
    enum class Kind {
      // Where the trail and the machines start.
      Trailhead,
      // Where the road reaches the sea most easily.
      Landing,
      // Where a lake's water leaves it: a ford, a mill, a hamlet's site.
      LakeOutlet,
      // A peak sanctuary, after Peatfield (1983): on or just off a summit,
      // where it best sees and is seen by the low country round it.
      Sanctuary,
    };

    Kind kind = Kind::Trailhead;
    float x_m = 0.0f;
    float z_m = 0.0f;
    float height_m = 0.0f;
  };

  struct RoadPoint {
    float x_m = 0.0f;
    float z_m = 0.0f;
  };

  struct Road {
    enum class Kind {
      // A cart road: gentle grades, bridges where it must.
      Road,
      // A walker's path, steep where the hill is.
      Footpath,
    };

    Kind kind = Kind::Road;
    // Unwrapped: each point follows the last across the torus's seam, so
    // the line may run beyond the period and is to be read modulo it.
    std::vector<RoadPoint> points;
  };

  struct RoadNetwork {
    std::vector<RoadPlace> places;
    std::vector<Road> roads;
    // Pairs of sanctuaries (indices into places) that see each other: a
    // fire on one shows on the other.
    std::vector<std::pair<std::size_t, std::size_t>> sightlines;
  };

  // Plans the network over one period of the world. `home_base` is the
  // trail's (the trailhead); `river_area` the least catchment drawn as a
  // river, which a road must bridge.
  [[nodiscard]] RoadNetwork
  plan_roads (const TerrainDomain& domain,
              std::span<const SurfaceElevation> elevations,
              const FloodField& flood,
              const DrainageGraph& drainage,
              square_meters_t river_area,
              CellIndex home_base,
              float sea_level);
}

#endif
