#ifndef MOPPE_TERRAIN_ROADS_HH
#define MOPPE_TERRAIN_ROADS_HH

#include <moppe/terrain/domain.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <array>
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

  // A bridge's span along a road: the points (indices into Road::points)
  // its two abutments stand at. Between them the tread is the deck,
  // straight from one abutment's height to the other's, and the ground
  // beneath is left to the river.
  struct RoadBridge {
    std::uint32_t first = 0;
    std::uint32_t last = 0;

    friend bool operator== (const RoadBridge&, const RoadBridge&) = default;
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
    // Where it crosses a river on a bridge, in order along it; set by
    // form_roads.
    std::vector<RoadBridge> bridges;
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
    // The most the tread's centre should lie under or over the natural
    // ground; where a hump or a hollow asks for more, the road keeps its
    // grade and cuts or fills deeper.
    float maximum_cut = 4.0f;
    float maximum_fill = 2.5f;
    // A road crosses any watercourse draining more than this, and any
    // standing water, at the ground (a ford or a drift), so that no
    // embankment dams it.
    float culvert_area_m2 = 20000.0f;
    // A road bridges any river draining more than this, where the rivers
    // are drawn (river.hh: five metres wide), its deck `clearance` metres
    // above the river's spate and its abutments on the banks, at most
    // `bridge_reach` metres either side of the river's line.
    float bridge_area_m2 = 175000.0f;
    float clearance = 1.2f;
    float bridge_reach = 30.0f;
    float longest_bridge = 90.0f;
    // The deck's half width, inside the tread's: the kerbs stand at it.
    float deck_half_width = 2.4f;
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
    std::size_t bridges = 0;
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

  // What the nearest road makes of a point: how far it lies from the line
  // (signed, positive to the left going along), how far along the road,
  // the height of the line there, and the line's rise per metre along.
  struct RoadSample {
    bool found = false;
    Road::Kind kind = Road::Kind::Road;
    // On a bridge's span: the deck, not the ground.
    bool bridge = false;
    // A paved lane (paved_roads), whose asphalt lies on the tread.
    bool paved = false;
    float across = 0.0f;
    float along = 0.0f;
    float centre_m = 0.0f;
    float grade = 0.0f;
    // The line's direction across the ground, a unit vector.
    float direction_x = 1.0f;
    float direction_z = 0.0f;
  };

  // The built roads exactly, finer than the height field's samples: the
  // authority the renderer's near ground and every body's footing read, so
  // that what is drawn is what is driven on. Over a built road's tread the
  // ground is the crowned carriageway, under a paved lane's asphalt or a
  // gravel road's crown (asphalt_m, gravel_m, as the renderer's road
  // ribbon lays them); it blends back into the height field over `blend`
  // metres past the shoulder. Over a bridge's span it
  // is the deck, out to the kerbs, and beyond them the field below.
  // Footpaths are worn into the ground's colour, not built, and leave its
  // height alone.
  class RoadSurface {
  public:
    // Segments are found by a grid of square buckets, each listing every
    // segment that passes within `reach` metres of it.
    static constexpr float bucket_m = 16.0f;
    static constexpr float reach_m = 10.0f;
    static constexpr float blend_m = 1.5f;
    static constexpr float asphalt_m = 0.045f;
    static constexpr float gravel_m = 0.03f;

    RoadSurface () = default;
    RoadSurface (const RoadNetwork& network,
                 float period_x,
                 float period_z,
                 const RoadFormation& formation = {});

    bool empty () const noexcept {
      return m_segment_count == 0;
    }

    const RoadFormation& formation () const noexcept {
      return m_formation;
    }

    RoadSample nearest (float x_m, float z_m) const;

    // The ground's height at a point, given the height field's there.
    float ground (float x_m, float z_m, float field_m) const;

    // As the renderer takes them: two rows a segment, (ax, az, bx, bz)
    // and (height at a, at b, distance along at a, 0 road, 1 footpath,
    // 2 a bridge's deck, or 3 a paved road); and per bucket, row by row, the
    // offset into the entries (which follow the buckets) and their count, then
    // the entries.
    std::span<const std::array<float, 4>> segment_rows () const noexcept {
      return m_rows;
    }

    std::span<const std::uint32_t> index () const noexcept {
      return m_index;
    }

    int buckets_x () const noexcept {
      return m_buckets_x;
    }

    int buckets_z () const noexcept {
      return m_buckets_z;
    }

  private:
    RoadFormation m_formation;
    float m_period_x = 0.0f;
    float m_period_z = 0.0f;
    int m_buckets_x = 0;
    int m_buckets_z = 0;
    std::size_t m_segment_count = 0;
    std::vector<std::array<float, 4>> m_rows;
    std::vector<std::uint32_t> m_index;
  };

  // Which of the network's ways are paved, one flag a road: the cart roads
  // still in use that carry at least half the busiest one's traffic, the
  // lanes the country keeps up. The rest stay gravel.
  [[nodiscard]] std::vector<bool> paved_roads (const RoadNetwork& network);

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
