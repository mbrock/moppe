#ifndef MOPPE_GAME_PLACES_HH
#define MOPPE_GAME_PLACES_HH

// Places worth arriving at: what stands at the ends of the roads
// (terrain/roads.hh). The landing has a jetty with the skiff moored at
// it; each peak sanctuary a stone fire platform inside a low enclosure
// wall, after Peatfield (1983), whose fire, once lit, shows from the
// sanctuaries on its sightlines; each lake outlet a little watermill and
// a farm. A place's history decides what is kept and what has fallen.
// Along the roads still in use, wherever the ground is level and dry,
// stand farmsteads still lived in: a house facing the road and a barn
// facing the house across the yard, its doors open on the byre where
// the horses, cows, pigs, and hens are kept (game/hens.hh).
// Near each, where the trees allow, an old fire ring marks a good camp,
// and a landing stage on the lake by the mill a place to fish from.

#include <moppe/game/game_clock.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/roads.hh>
#include <moppe/terrain/torus.hh>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace moppe::game {
  // What the planner reads of the world.
  struct PlaceGround {
    // The ground's elevation at a point, roads and all.
    terrain::ElevationField ground;
    // The still water's surface over a point, the sea's where none
    // stands there.
    terrain::ElevationField water;
    // Whether the water at a point is the sea's.
    terrain::PlanField<bool> sea;
    // How fast the water over a point runs, and which way.
    terrain::PlanField<terrain::WaterVelocity> current;
    // How much of a point the trees cover; may be empty.
    terrain::PlanField<map::ForestCover> forest;
    const terrain::RoadSurface* roads = nullptr;
    terrain::SurfaceElevation sea_level {};
    // The world's period (moppe/terrain/torus.hh); zero for none.
    plan_extent_t period = plan_extent (0.0f * u::m, 0.0f * u::m);
  };

  // A jetty of planks on posts, from the shore out over the water.
  struct Jetty {
    // Where its deck meets the land (at the deck's height), the level
    // way it runs out, its length, and the deck's height.
    position_t root {};
    direction_t out {};
    meters_t length = 0.0f * u::m;
    terrain::SurfaceElevation deck {};
    meters_t half_width = 0.9f * u::m;
    // How far gone it is, 0 sound to 1 its planks mostly fallen in.
    proportion_t decay = 0.0f * proportion[one];
  };

  struct Landing {
    std::size_t place = 0;
    Jetty jetty;
    // Where the skiff lies alongside on the periodic torus, and which way
    // its bow points.
    terrain::torus_position_t berth {};
    direction_t berth_forward {};
    // The post its painter is tied to, on the periodic torus.
    terrain::torus_position_t bollard {};
  };

  // A peak sanctuary's stones.
  struct Sanctuary {
    std::size_t place = 0;
    // The middle of the platform's top, where its fire is laid.
    position_t hearth {};
    meters_t platform_radius = 1.4f * u::m;
    // The enclosure: its centre on the ground, radius, and the level way
    // out through its gap toward the footpath.
    position_t centre {};
    meters_t wall_radius = 7.0f * u::m;
    direction_t gate {};
    // How far the stones have tumbled, 0 kept to 1 a low ring of rubble.
    proportion_t ruin = 0.0f * proportion[one];
    bool kept = false;
    // The other sanctuaries (indices into PlacesPlan::sanctuaries) that
    // see this one.
    std::vector<std::size_t> seen_from;
  };

  // A building of the farm by an outlet, each one of the exported
  // models.
  struct Building {
    enum class Kind { mill, house, barn };
    Kind kind = Kind::mill;
    // Its origin on the ground and the level way its front faces.
    position_t at {};
    direction_t forward {};
  };

  struct Farm {
    std::size_t place = 0;
    std::vector<Building> buildings;
    // Lived in and the wheel turning, or long left and roofless.
    bool kept = true;
    bool ruined = false;
    // A farmstead by a road rather than at one of the network's places
    // (`place` then means nothing).
    bool homestead = false;
  };

  // A standing building's frame: x to the left of its front, y up, z out
  // of its front, the origin on the ground at its middle.
  [[nodiscard]] Mat4 building_frame (const Building& b);

  // The barn's inside, in its frame (tools/figure/farm.py): the floor's
  // half extents within the walls, and its ramp out of the doorway.
  inline constexpr meters_t barn_inside_half_x = 5.86f * u::m;
  inline constexpr meters_t barn_inside_half_z = 3.86f * u::m;
  inline constexpr meters_t barn_front = 4.0f * u::m;
  inline constexpr meters_t barn_door_half = 1.7f * u::m;
  inline constexpr meters_t barn_ramp_length = 1.9f * u::m;

  struct PlacesPlan {
    std::optional<Landing> landing;
    std::vector<Sanctuary> sanctuaries;
    std::vector<Farm> farms;
    // Short stages over the water to fish from.
    std::vector<Jetty> stages;
    // Old fire rings by trees fit for a hammock: good camps.
    std::vector<position_t> hearths;
    // Ground kept clear of trees and loose rock round what is built.
    struct Clearing {
      position_t centre {};
      meters_t radius = 0.0f * u::m;
    };
    std::vector<Clearing> clearings;
    plan_extent_t period = plan_extent (0.0f * u::m, 0.0f * u::m);
  };

  // Where everything stands; deterministic for a world. Camps are added
  // once the trees are known (place_camps).
  [[nodiscard]] PlacesPlan plan_places (const terrain::RoadNetwork& network,
                                        const PlaceGround& ground,
                                        std::uint32_t seed);

  // The floor underfoot at a point inside a standing house or barn, or on
  // a barn's ramp: its height, for what walks there (the animals, the
  // hens) to stand on rather than the ground beneath it. The ramp's foot
  // is set into the ground, so take the higher of this and the ground.
  [[nodiscard]] std::optional<terrain::SurfaceElevation>
  floor_at (const PlacesPlan& plan, const plan_position_t& at);

  // Whether a point lies in one of the plan's clearings.
  [[nodiscard]] bool cleared (const PlacesPlan& plan, const position_t& at);

  // An old fire ring by a fit pair of hammock trees near each place,
  // found by `hammock_near` (a pair's middle near a point, if any).
  void place_camps (
    PlacesPlan& plan,
    const terrain::RoadNetwork& network,
    const PlaceGround& ground,
    const std::function<std::optional<position_t> (const position_t&)>&
      hammock_near);

  // The sanctuary whose platform someone standing at `feet` is beside.
  [[nodiscard]] std::optional<std::size_t>
  sanctuary_at_hand (const PlacesPlan& plan, const position_t& feet);
  // The old fire ring within a step or two of `feet`, at the copy of the
  // world nearest them.
  [[nodiscard]] std::optional<position_t>
  hearth_at_hand (const PlacesPlan& plan, const position_t& feet);

  // How fiercely a beacon lit at `lit` (or put out, if `lit` is
  // negative, at `out`) burns at `now`, 0..1.
  [[nodiscard]] proportion_t
  beacon_burn (play_time_t lit, play_time_t out, play_time_t now);

  // The built things as baked meshes and their colliders.
  class PlacesLandscape {
  public:
    void rebuild (render::Renderer& renderer,
                  const PlacesPlan& plan,
                  const PlaceGround& ground);

    // The stones, timber, and buildings near the camera; the mill wheel
    // turns with `time`.
    void draw (render::Renderer& renderer,
               const position_t& camera,
               seconds_t time,
               std::uint64_t motion_base) const;
    // The same meshes, lent to the sun's shadow.
    void cast_shadows (render::Renderer& renderer,
                       const position_t& camera) const;

    // Unlit beacons' laid wood and lit ones' fires: the stack burning on
    // each platform near the camera, and from afar a glow and a column
    // of smoke over each that burns. `burn` is each sanctuary's.
    void draw_beacons (render::DrawList& dl,
                       const position_t& camera,
                       std::span<const proportion_t> burn) const;
    void draw_beacon_fires (render::Renderer& renderer,
                            const position_t& camera,
                            std::span<const proportion_t> burn,
                            proportion_t daylight,
                            seconds_t time) const;

    const std::vector<mov::Trunk>& colliders () const noexcept {
      return m_colliders;
    }

  private:
    struct Piece {
      // The mesh near to, and (for a building or the wheel) two coarser
      // tiers, which take over once the finer one's finest parts --
      // `detail` metres and twice that -- would span under a pixel.
      render::MeshPtr mesh;
      std::array<render::MeshPtr, 2> coarser;
      meters_t detail = 0.0f * u::m;
      position_t origin {};
      meters_t reach = 0.0f * u::m;
      // The mill's wheel, drawn in the mill's frame about its axle,
      // turning while the mill is kept.
      std::optional<Mat4> frame;
      bool wheel = false;
      bool turning = false;
      direction_t axis {};
    };
    position_t near_copy (const position_t& at, const position_t& camera) const;
    const render::Mesh&
    tier (const Piece& piece, meters_t distance, float focal) const;

    std::vector<Piece> m_pieces;
    std::vector<mov::Trunk> m_colliders;
    std::vector<position_t> m_beacons;
    plan_extent_t m_period = plan_extent (0.0f * u::m, 0.0f * u::m);
  };
}

#endif
