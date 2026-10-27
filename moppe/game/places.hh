#ifndef MOPPE_GAME_PLACES_HH
#define MOPPE_GAME_PLACES_HH

// Places worth arriving at: what stands at the ends of the roads
// (terrain/roads.hh). The landing has a jetty with the skiff moored at
// it; each peak sanctuary a stone fire platform inside a low enclosure
// wall, after Peatfield (1983), whose fire, once lit, shows from the
// sanctuaries on its sightlines; each lake outlet a little watermill and
// a farm. A place's history decides what is kept and what has fallen.
// Near each, where the trees allow, an old fire ring marks a good camp,
// and a landing stage on the lake by the mill a place to fish from.

#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
#include <moppe/mov/trunk_field.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/roads.hh>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace moppe::game {
  // What the planner reads of the world.
  struct PlaceGround {
    // The ground's height at a point, roads and all.
    std::function<float (float x, float z)> ground;
    // The still water's surface over a point, the sea's where none
    // stands there.
    std::function<float (float x, float z)> water;
    // Whether the water at a point is the sea's.
    std::function<bool (float x, float z)> sea;
    // How fast the water over a point runs, m/s, and which way.
    std::function<Vec3 (float x, float z)> current;
    const terrain::RoadSurface* roads = nullptr;
    float sea_level = 0.0f;
    float period_x = 0.0f;
    float period_z = 0.0f;
  };

  // A jetty of planks on posts, from the shore out over the water.
  struct Jetty {
    // Where its deck meets the land, the way it runs out, its length,
    // and the deck's height.
    Vec3 root {};
    Vec3 out {};
    float length = 0.0f;
    float deck = 0.0f;
    float half_width = 0.9f;
    // How far gone it is, 0 sound to 1 its planks mostly fallen in.
    float decay = 0.0f;
  };

  struct Landing {
    std::size_t place = 0;
    Jetty jetty;
    // Where the skiff lies alongside, and which way its bow points.
    Vec3 berth {};
    Vec3 berth_forward {};
    // The post its painter is tied to.
    Vec3 bollard {};
  };

  // A peak sanctuary's stones.
  struct Sanctuary {
    std::size_t place = 0;
    // The middle of the platform's top, where its fire is laid.
    Vec3 hearth {};
    float platform_radius = 1.4f;
    // The enclosure: its centre on the ground, radius, and the way out
    // through its gap toward the footpath.
    Vec3 centre {};
    float wall_radius = 7.0f;
    Vec3 gate {};
    // How far the stones have tumbled, 0 kept to 1 a low ring of rubble.
    float ruin = 0.0f;
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
    // Its origin on the ground and the way its front faces.
    Vec3 at {};
    Vec3 forward {};
  };

  struct Farm {
    std::size_t place = 0;
    std::vector<Building> buildings;
    // Lived in and the wheel turning, or long left and roofless.
    bool kept = true;
    bool ruined = false;
  };

  struct PlacesPlan {
    std::optional<Landing> landing;
    std::vector<Sanctuary> sanctuaries;
    std::vector<Farm> farms;
    // Short stages over the water to fish from.
    std::vector<Jetty> stages;
    // Old fire rings by trees fit for a hammock: good camps.
    std::vector<Vec3> hearths;
    // Ground kept clear of trees and loose rock round what is built.
    struct Clearing {
      Vec3 centre {};
      float radius = 0.0f;
    };
    std::vector<Clearing> clearings;
    float period_x = 0.0f;
    float period_z = 0.0f;
  };

  // Where everything stands; deterministic for a world. Camps are added
  // once the trees are known (place_camps).
  [[nodiscard]] PlacesPlan plan_places (const terrain::RoadNetwork& network,
                                        const PlaceGround& ground,
                                        std::uint32_t seed);

  // Whether a point lies in one of the plan's clearings.
  [[nodiscard]] bool cleared (const PlacesPlan& plan, const Vec3& at);

  // An old fire ring by a fit pair of hammock trees near each place,
  // found by `hammock_near` (a pair's middle near a point, if any).
  void place_camps (
    PlacesPlan& plan,
    const terrain::RoadNetwork& network,
    const PlaceGround& ground,
    const std::function<std::optional<Vec3> (const Vec3&)>& hammock_near);

  // The sanctuary whose platform someone standing at `feet` is beside.
  [[nodiscard]] std::optional<std::size_t>
  sanctuary_at_hand (const PlacesPlan& plan, const Vec3& feet);
  // The old fire ring within a step or two of `feet`.
  [[nodiscard]] std::optional<Vec3> hearth_at_hand (const PlacesPlan& plan,
                                                    const Vec3& feet);

  // How fiercely a beacon lit at `lit` (or put out, if `lit` is
  // negative, at `out`) burns at `now`, 0..1.
  [[nodiscard]] float beacon_burn (double lit, double out, double now);

  // The built things as baked meshes and their colliders.
  class PlacesLandscape {
  public:
    void rebuild (render::Renderer& renderer,
                  const PlacesPlan& plan,
                  const PlaceGround& ground);

    // The stones, timber, and buildings near the camera; the mill wheel
    // turns with `time`.
    void draw (render::Renderer& renderer,
               const Vec3& camera,
               float time,
               std::uint64_t motion_base) const;
    // The same meshes, lent to the sun's shadow.
    void cast_shadows (render::Renderer& renderer, const Vec3& camera) const;

    // Unlit beacons' laid wood and lit ones' fires: the stack burning on
    // each platform near the camera, and from afar a glow and a column
    // of smoke over each that burns. `burn` is each sanctuary's.
    void draw_beacons (render::DrawList& dl,
                       const Vec3& camera,
                       const std::vector<float>& burn) const;
    void draw_beacon_fires (render::Renderer& renderer,
                            const Vec3& camera,
                            const std::vector<float>& burn,
                            float daylight,
                            float time) const;

    const std::vector<mov::Trunk>& colliders () const noexcept {
      return m_colliders;
    }

  private:
    struct Piece {
      render::MeshPtr mesh;
      Vec3 origin {};
      float reach = 0.0f;
      // The mill's wheel, drawn in the mill's frame about its axle,
      // turning while the mill is kept.
      std::optional<Mat4> frame;
      bool wheel = false;
      bool turning = false;
      Vec3 axis {};
    };
    Vec3 near_copy (const Vec3& at, const Vec3& camera) const;

    std::vector<Piece> m_pieces;
    std::vector<mov::Trunk> m_colliders;
    std::vector<Vec3> m_beacons;
    float m_period_x = 0.0f;
    float m_period_z = 0.0f;
  };
}

#endif
