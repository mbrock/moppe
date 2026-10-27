#ifndef MOPPE_TERRAIN_ROADS_HH
#define MOPPE_TERRAIN_ROADS_HH

#include <moppe/terrain/domain.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <cstddef>
#include <cstdint>
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
    // A history rolled for each place from the world's seed, by its kind:
    // the sanctuaries oldest and often gone quiet, the landing and the
    // outlets old, the trailhead young and in use. Abandoned is zero for a
    // place still lived in or kept.
    float founded_years_ago = 0.0f;
    float abandoned_years_ago = 0.0f;

    friend bool operator== (const RoadPlace&, const RoadPlace&) = default;
  };

  struct RoadPoint {
    float x_m = 0.0f;
    float z_m = 0.0f;

    friend bool operator== (const RoadPoint&, const RoadPoint&) = default;
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
    // The height of the finished tread's centre at each point, metres;
    // empty until form_roads solves it.
    std::vector<float> heights;
    // How many of the journeys between every pair of places go this way
    // along the network.
    std::uint32_t traffic = 0;
    // Since the oldest pair of places it joins have both stood: a way is as
    // old as the oldest journey along it.
    float age_years = 0.0f;
    // Zero while some journey along it joins two places still in use;
    // otherwise since the last such journey ended.
    float abandoned_years_ago = 0.0f;

    friend bool operator== (const Road&, const Road&) = default;
  };

  struct RoadNetwork {
    std::vector<RoadPlace> places;
    std::vector<Road> roads;
    // Pairs of sanctuaries (indices into places) that see each other: a
    // fire on one shows on the other.
    std::vector<std::pair<std::size_t, std::size_t>> sightlines;

    friend bool operator== (const RoadNetwork&, const RoadNetwork&) = default;
  };

  // What grows on and soaks the ground, per terrain cell, each 0 to 1; an
  // empty span counts as none. Roads are dearer to clear through forest,
  // keep out of mires, and shun wet ground; footpaths mind them less.
  struct RoadGround {
    std::span<const float> forest_cover;
    std::span<const float> mire;
    std::span<const float> wetness;
  };

  // How a road is built into the ground. The tread is a crowned
  // carriageway with shoulders; beyond it the ground is cut back or filled
  // out at the batters' slopes until they meet it. Lengths are metres and
  // slopes rise over run.
  struct RoadFormation {
    float half_width = 2.5f;
    float shoulder = 0.75f;
    float crown = 0.025f;
    float cut_slope = 1.0f;
    float fill_slope = 0.6f;
    // The steepest a cart road climbs, and the length over which a change
    // of grade is eased (its vertical curves).
    float maximum_grade = 0.10f;
    float vertical_curve = 30.0f;
    // The most the tread's centre may lie under or over the natural ground;
    // where the land asks for more, the road takes a steeper grade instead.
    float maximum_cut = 4.0f;
    float maximum_fill = 2.5f;
    // A road crosses any watercourse draining more than this, and any
    // standing water, at the ground (a ford or a drift), so that no
    // embankment dams it.
    float culvert_area_m2 = 20000.0f;
    // A footpath's worn width; footpaths are not built, only trodden.
    float footpath_half_width = 0.8f;
  };

  struct RoadFormationResult {
    std::vector<SurfaceElevation> heights;
    // Metres of ground laid down (positive) or cut away (negative).
    std::vector<float> change_m;
    // How much of each cell the tread covers, 0 to 1.
    std::vector<float> influence;
    double cut_m3 = 0.0;
    double fill_m3 = 0.0;
    // Points where a cart road's grade exceeds maximum_grade, of all its
    // points.
    std::size_t steep_points = 0;
    std::size_t points = 0;
  };

  // Solves each road's profile into network.roads[i].heights and builds
  // the roads into the ground. Roads are taken in order, so each branch
  // meets the road it leaves at that road's own height.
  [[nodiscard]] RoadFormationResult
  form_roads (const TerrainDomain& domain,
              std::span<const SurfaceElevation> elevations,
              RoadNetwork& network,
              const FloodField& flood,
              const DrainageGraph& drainage,
              const RoadFormation& parameters = {});

  // Plans the network over one period of the world. `home_base` is the
  // trail's (the trailhead); `river_area` the least catchment drawn as a
  // river, which a road must bridge; `seed` rolls the places' histories.
  [[nodiscard]] RoadNetwork
  plan_roads (const TerrainDomain& domain,
              std::span<const SurfaceElevation> elevations,
              const FloodField& flood,
              const DrainageGraph& drainage,
              const RoadGround& ground,
              square_meters_t river_area,
              CellIndex home_base,
              float sea_level,
              std::uint32_t seed);
}

#endif
