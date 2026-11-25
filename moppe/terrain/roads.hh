#ifndef MOPPE_TERRAIN_ROADS_HH
#define MOPPE_TERRAIN_ROADS_HH

#include <moppe/terrain/domain.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    plan_position_t at {};
    SurfaceElevation height {};
    // A history rolled for each place from the world's seed, by its kind:
    // the sanctuaries oldest and often gone quiet, the landing and the
    // outlets old, the trailhead young and in use. Abandoned is zero for a
    // place still lived in or kept.
    age_t founded {};
    age_t abandoned {};

    friend bool operator== (const RoadPlace&, const RoadPlace&) = default;
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

  // Where a cart road crosses a stream or a pond on an embankment, the pipe
  // that lets the water through under it: at point `at` of the road, its
  // floor (invert) at `invert` and `diameter` across.
  struct RoadCulvert {
    std::uint32_t at = 0;
    SurfaceElevation invert {};
    meters_t diameter {};

    friend bool operator== (const RoadCulvert&, const RoadCulvert&) = default;
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
    std::vector<plan_position_t> points;
    // The height of the finished tread's centre at each point; empty until
    // form_roads solves it.
    std::vector<SurfaceElevation> heights;
    // Where it crosses a river on a bridge, in order along it; set by
    // form_roads.
    std::vector<RoadBridge> bridges;
    // Where it crosses smaller water on an embankment; set by form_roads.
    std::vector<RoadCulvert> culverts;
    // How many of the journeys between every pair of places go this way
    // along the network.
    std::uint32_t traffic = 0;
    // Since the oldest pair of places it joins have both stood: a way is as
    // old as the oldest journey along it.
    age_t age {};
    // Zero while some journey along it joins two places still in use;
    // otherwise since the last such journey ended.
    age_t abandoned {};
    // The road once round the world through the landing, which the bus
    // keeps to (game/bus.hh): closed on itself, its last point its first a
    // period on. It is paved, and its ends join each other, not a turning
    // place.
    bool loop = false;

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
  // keep out of mires (the sphagnum's cover), and shun wet ground;
  // footpaths mind them less.
  struct RoadGround {
    std::span<const quantity<map::forest_cover[one], float>> forest_cover;
    std::span<const quantity<map::sphagnum_abundance[one], float>> mire;
    std::span<const SoilWetness> wetness;
  };

  // How a road is built into the ground. The tread is a crowned
  // carriageway with shoulders; beyond it the ground is cut back or filled
  // out at the batters' slopes until they meet it. Slopes and the crown's
  // fall are rise over run across the road, the grade along it.
  struct RoadFormation {
    // Room for the bus in its lane, and for a car to pass it.
    meters_t half_width = 3.0f * u::m;
    meters_t shoulder = 0.75f * u::m;
    slope_t crown = 0.025f * terrain_slope[one];
    slope_t cut_slope = 1.0f * terrain_slope[one];
    slope_t fill_slope = 0.6f * terrain_slope[one];
    // The roadside ditch along cuts: a trench just outside the shoulder
    // that intercepts hillside runoff before it can reach the tread.
    meters_t ditch_depth = 0.55f * u::m;
    meters_t ditch_width = 1.5f * u::m;
    // The steepest a cart road climbs, and the length over which a change
    // of grade is eased (its vertical curves).
    grade_t maximum_grade = 0.10f * grade[one];
    meters_t vertical_curve = 30.0f * u::m;
    // The most the tread's centre should lie under or over the natural
    // ground; where a hump or a hollow asks for more, the road keeps its
    // grade and cuts or fills deeper.
    meters_t maximum_cut = 4.0f * u::m;
    meters_t maximum_fill = 2.5f * u::m;
    // A road crosses any watercourse draining more than this, and any
    // standing water, on an embankment over a culvert: its tread at least
    // `freeboard` over the water, and over the pipe's crown.
    square_meters_t culvert_area = 20000.0f * u::m * u::m;
    meters_t freeboard = 0.5f * u::m;
    // A road bridges any river draining more than this, where the rivers
    // are drawn (river.hh: five metres wide), its deck `clearance` above
    // the river's spate (rising to `navigable_clearance` over deeper
    // navigable rivers so boats and jet skis pass cleanly underneath) and
    // its abutments on the banks, at most `bridge_reach` either side.
    square_meters_t bridge_area = 175000.0f * u::m * u::m;
    square_meters_t navigable_area = 2000000.0f * u::m * u::m;
    meters_t clearance = 1.2f * u::m;
    meters_t navigable_clearance = 2.8f * u::m;
    meters_t bridge_reach = 55.0f * u::m;
    meters_t longest_bridge = 165.0f * u::m;
    // The deck's half width, inside the tread's: the kerbs stand at it.
    meters_t deck_half_width = 2.9f * u::m;
    // How deep the deck and its stringers stand under the tread: the
    // ground under a span is kept at least this far below it.
    meters_t deck_depth = 0.6f * u::m;
    // A footpath's worn width; footpaths are not built, only trodden.
    meters_t footpath_half_width = 0.8f * u::m;
  };

  struct RoadFormationResult {
    std::vector<SurfaceElevation> heights;
    // Ground laid down (positive) or cut away (negative).
    std::vector<ElevationChange> change;
    // How much of each cell the tread covers, 0 to 1.
    std::vector<proportion_t> influence;
    cubic_meters_f64_t cut = 0.0 * u::m * u::m * u::m;
    cubic_meters_f64_t fill = 0.0 * u::m * u::m * u::m;
    // Points where a cart road's grade exceeds maximum_grade, of all its
    // points.
    std::size_t steep_points = 0;
    std::size_t points = 0;
    std::size_t bridges = 0;
  };

  // Solves each road's profile into network.roads[i].heights and builds
  // the roads into the ground. Roads are taken in order, so each branch
  // meets the road it leaves at that road's own height. A cart road whose
  // end lies in water (a lake's outlet is planned on its stream) is cut
  // back to the bank; its tread keeps a freeboard over any standing water
  // beside it; and each end that joins no other road is built out into a
  // level turning place (RoadTurning).
  [[nodiscard]] RoadFormationResult
  form_roads (const TerrainDomain& domain,
              std::span<const SurfaceElevation> elevations,
              RoadNetwork& network,
              const FloodField& flood,
              const DrainageGraph& drainage,
              const RoadFormation& parameters = {});

  // Raises or bridges any unbridged cart-road stretch that still lies below
  // the final painted water sheet (e.g. river backwater behind a sill or
  // bank-side river stage), keeping every deck and approach within the
  // road's grade. Returns the number of bridges added.
  std::size_t bridge_wet_roads (const TerrainDomain& domain,
                                std::span<const SurfaceElevation> ground,
                                std::span<const SurfaceElevation> water,
                                RoadNetwork& network,
                                const RoadFormation& parameters = {});

  // Where a cart road ends without joining another it ends in a turning
  // place, so that it reads as a road's end and a cart can come about: a
  // round pad some ten metres across, the tread carried once round a
  // ring that passes through the road's end and lies back along its last
  // few metres, so that nothing is built past the end. `points` go round
  // from the end and back to it, unwrapped like the road's; `heights`
  // (empty until the road's are solved) lie in the plane the road's last
  // stretch climbs in, which form_roads makes level, so the pad meets
  // the road it ends without a step.
  struct RoadTurning {
    static constexpr meters_t radius = 2.0f * u::m;
    static constexpr std::size_t segments = 12;

    // Into RoadNetwork::roads, and which end: its last point or its first.
    std::size_t road = 0;
    bool back = false;
    std::vector<plan_position_t> points;
    std::vector<SurfaceElevation> heights;
  };

  // The turning places of a network's cart roads, in the roads' order: one
  // at each end that no other cart road comes within a tread's width of
  // (a footpath may go on from it). The period is the world's.
  [[nodiscard]] std::vector<RoadTurning>
  road_turnings (const RoadNetwork& network, plan_extent_t period);

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
    meters_t across {};
    meters_t along {};
    SurfaceElevation centre {};
    grade_t grade {};
    // The line's direction across the ground.
    plan_direction_t direction = Vec2 (1.0f, 0.0f) * moppe::direction[one];
  };

  // A guardrail along a paved lane where the ground falls away beside it:
  // a galvanized W-beam on steel posts, one run of posts in order along
  // the road. Unwrapped like the road it follows, so read modulo the
  // period.
  struct Guardrail {
    struct Post {
      plan_position_t at {};
      // The beam's middle, and the ground at the post's foot.
      SurfaceElevation rail {};
      SurfaceElevation foot {};
      // Away from the road, level.
      plan_direction_t out {};
    };
    std::vector<Post> posts;
  };

  // The built roads exactly, finer than the height field's samples: the
  // authority the renderer's near ground and every body's footing read, so
  // that what is drawn is what is driven on. Over a built road's tread the
  // ground is the crowned carriageway, under a paved lane's asphalt or a
  // gravel road's crown (asphalt, gravel, as the renderer's road ribbon
  // lays them); it blends back into the height field over `blend` past
  // the shoulder. Over a bridge's span it
  // is the deck, out to the kerbs, and beyond them the field below.
  // Footpaths are worn into the ground's colour, not built, and leave its
  // height alone. The turning places at the roads' ends are part of it.
  //
  // The tread's height is continuous round a bend: how far along a segment
  // a point lies is measured between the mitres it shares with its
  // neighbours, the lines bisecting the bends at its ends, so that both
  // segments give a point on their mitre the same height; outside the
  // bend the wedge between them is level at their shared point. (Measured
  // square to each segment instead, the inside of every bend on a grade
  // would be a stair of steps a few centimetres high.)
  class RoadSurface {
  public:
    // Segments are found by a grid of buckets, each no more than
    // `bucket` square and listing every segment that passes within
    // `reach` of it.
    static constexpr meters_t bucket = 16.0f * u::m;
    static constexpr meters_t reach = 10.0f * u::m;
    static constexpr meters_t blend = 1.5f * u::m;
    static constexpr meters_t asphalt = 0.045f * u::m;
    static constexpr meters_t gravel = 0.03f * u::m;

    RoadSurface () = default;
    RoadSurface (const RoadNetwork& network,
                 plan_extent_t period,
                 const RoadFormation& formation = {});

    bool empty () const noexcept {
      return m_segment_count == 0;
    }

    const RoadFormation& formation () const noexcept {
      return m_formation;
    }

    RoadSample nearest (plan_position_t at) const;

    // The ground's height at a point, given the height field's there.
    SurfaceElevation ground (plan_position_t at, SurfaceElevation field) const;

    // Guardrails: the beam's line stands `guardrail_offset` from the
    // road's centre and its middle `guardrail_height` over the tread's
    // edge, on posts every `guardrail_spacing`.
    static constexpr meters_t guardrail_offset = 4.1f * u::m;
    static constexpr meters_t guardrail_height = 0.6f * u::m;
    static constexpr meters_t guardrail_spacing = 2.0f * u::m;

    // Lays the guardrails along the paved lanes of `network` (the one
    // this surface was built from) wherever the ground, as `field` gives
    // the height field (unwrapped points, to be read modulo the period),
    // falls away from the road's edge. Bridges keep their own railings,
    // and junctions are left open.
    void place_guardrails (const RoadNetwork& network,
                           const ElevationField& field);

    std::span<const Guardrail> guardrails () const noexcept {
      return m_guardrails;
    }

    // As the renderer takes them, metres, the GPU's layout: three rows a
    // segment, (ax, az, bx, bz), (height at a, at b, distance along at a, 0
    // road, 1 footpath, 2 a bridge's deck, or 3 a paved road), and (the
    // tangents of half the bends at a and at b, positive turning left, 0, 0);
    // and per bucket, row by row, the offset into the entries (which follow the
    // buckets) and their count, then the entries.
    static constexpr std::size_t rows_per_segment = 3;

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

    // A bucket's sides: the periods divided evenly.
    meters_t bucket_x () const noexcept {
      return plan_value (m_period).x () / static_cast<float> (m_buckets_x) *
             u::m;
    }

    meters_t bucket_z () const noexcept {
      return plan_value (m_period).z () / static_cast<float> (m_buckets_z) *
             u::m;
    }

  private:
    RoadFormation m_formation;
    plan_extent_t m_period {};
    int m_buckets_x = 0;
    int m_buckets_z = 0;
    std::size_t m_segment_count = 0;
    std::vector<std::array<float, 4>> m_rows;
    std::vector<std::uint32_t> m_index;
    std::vector<Guardrail> m_guardrails;
  };

  // Which of the network's ways are paved, one flag a road: the loop, and
  // the cart roads still in use that carry at least half the busiest one's
  // traffic, the lanes the country keeps up. The rest stay gravel.
  [[nodiscard]] std::vector<bool> paved_roads (const RoadNetwork& network);

  // Plans the network over one period of the world: from the trailhead to
  // the landing, a loop round the world through the landing (the cheaper
  // way round of the two) when one can be built, joined by a road from the
  // trailhead; and from that network the rest. `home_base` is the
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
              SurfaceElevation sea_level,
              std::uint32_t seed);
}

#endif
