#ifndef MOPPE_GAME_MUSHROOMS_HH
#define MOPPE_GAME_MUSHROOMS_HH

#include <moppe/game/basket.hh>
#include <moppe/game/forest_plan.hh>
#include <moppe/map/surface.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/watercourse.hh>

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace moppe::game {
  // What a walker calls it, singular and plural.
  const char* mushroom_name (MushroomKind kind);
  const char* mushroom_plural (MushroomKind kind);
  // Fly agaric is admired, not picked.
  bool mushroom_edible (MushroomKind kind);

  // One mushroom standing on the forest floor. `base` is where its stem
  // enters the ground; `scale` multiplies its kind's ordinary height.
  struct MushroomSite {
    Vec3 base {};
    float scale = 1.0f;
    float yaw = 0.0f;
    // How far it leans from upright, and which way, in radians.
    float lean = 0.0f;
    float lean_toward = 0.0f;
    std::uint32_t seed = 0;
    MushroomKind kind = MushroomKind::chanterelle;
  };

  struct MushroomPlan {
    std::vector<MushroomSite> sites;
  };

  // A few of the trees host a group of mushrooms in an arc around the
  // trunk, more of them near the trailhead so the first walk finds some.
  // None grow in the trail's tread, the home base, or water.
  [[nodiscard]] MushroomPlan
  plan_mushrooms (const ForestPlan& forest,
                  const map::SurfaceGeometry& surface,
                  const map::SurfaceReadings& readings,
                  const terrain::WaterSheets& water,
                  std::uint32_t seed,
                  meters_t sea_level,
                  const Vec3& trailhead);

  // Records one mushroom, as planned, into `list` in world space. The
  // basket and the picking animation reuse it with their own transform.
  void draw_mushroom (render::DrawList& list, const MushroomSite& site);

  // The planned mushrooms on the ground: meshes built tile by tile as the
  // player comes near, rebuilt when one of theirs is picked.
  class MushroomPatch {
  public:
    void rebuild (MushroomPlan plan);

    std::size_t mushroom_count () const noexcept {
      return m_plan.sites.size ();
    }
    const MushroomSite& site (std::uint32_t index) const {
      return m_plan.sites[index];
    }

    // The mushroom a walker standing at `feet` and facing `heading` would
    // reach for, if any lies within arm's reach and unpicked.
    std::optional<std::uint32_t> within_reach (const Vec3& feet,
                                               const Vec3& heading) const;

    // The unpicked mushroom nearest `at`, horizontally, within `within`
    // metres, passing over those in `skip`.
    std::optional<std::uint32_t>
    nearest (const Vec3& at,
             float within,
             std::span<const std::uint32_t> skip = {}) const;

    // Brings the ground in line with the basket: anything it holds is no
    // longer standing. Cheap when nothing changed.
    void follow (const Basket& basket);

    void draw (render::Renderer& renderer, const Vec3& camera);

  private:
    struct Tile {
      std::vector<std::uint32_t> sites;
      render::MeshPtr mesh;
      bool dirty = true;
    };

    static std::uint64_t tile_key (int x, int z);
    Tile* tile_at (const Vec3& at);

    MushroomPlan m_plan;
    std::vector<bool> m_picked;
    std::size_t m_followed = 0;
    std::unordered_map<std::uint64_t, Tile> m_tiles;
  };
}

#endif
