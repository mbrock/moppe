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
  // How done a mushroom is over the fire (quantities.hh's doneness).
  using doneness_t = quantity<doneness[one], float>;

  // What a walker calls it, singular and plural.
  const char* mushroom_name (MushroomKind kind);
  const char* mushroom_plural (MushroomKind kind);
  // Fly agaric is admired, not picked.
  bool mushroom_edible (MushroomKind kind);

  // One mushroom standing on the forest floor. `base` is where its stem
  // enters the ground; `scale` multiplies its kind's ordinary height.
  struct MushroomSite {
    position_t base {};
    scale_factor_t scale = 1.0f * scale_factor[one];
    heading_t yaw = 0.0f * heading[si::radian];
    // How far it leans from upright, and which way.
    inclination_t lean = 0.0f * inclination[si::radian];
    heading_t lean_toward = 0.0f * heading[si::radian];
    std::uint32_t seed = 0;
    MushroomKind kind = MushroomKind::chanterelle;
    // How long over a fire: none raw, one fried golden brown, two burnt
    // black.
    doneness_t cooked = 0.0f * doneness[one];
  };

  struct MushroomPlan {
    std::vector<MushroomSite> sites;
    // The world's extent in x and z, which the sites wrap round; zero for
    // none.
    spatial_extent_t period {};
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
                  const position_t& trailhead);

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
    std::optional<std::uint32_t>
    within_reach (const position_t& feet, const direction_t& heading) const;

    // The unpicked mushroom nearest `at`, horizontally, within `within`,
    // passing over those in `skip`.
    std::optional<std::uint32_t>
    nearest (const position_t& at,
             meters_t within,
             std::span<const std::uint32_t> skip = {}) const;

    // Every unpicked mushroom within `within` of `at`, horizontally.
    std::vector<std::uint32_t> within (const position_t& at,
                                       meters_t within) const;

    // Brings the ground in line with the basket: anything it holds is no
    // longer standing, and anything only an earlier basket held (one
    // restored from a checkpoint) grows again.
    void follow (const Basket& basket);

    // Someone else picked this one: it no longer stands, though no
    // basket here holds it.
    void take (std::uint32_t index);

    // Where mushroom `index` stands, at the copy of the world nearest
    // `reference`: a walker past the seam sees the ones beyond it.
    position_t base_near (std::uint32_t index,
                          const position_t& reference) const;

    void draw (render::Renderer& renderer, const position_t& camera);

  private:
    struct TileMesh {
      render::MeshPtr mesh;
      bool dirty = true;
    };

    std::uint64_t tile_key (int x, int z) const;
    int tile_x (meters_t x) const;
    int tile_z (meters_t z) const;
    TileMesh* mesh_at (const position_t& at);

    MushroomPlan m_plan;
    // The tiles' size and how many there are to the world (none counted
    // when the plan does not wrap).
    meters_t m_tile_x = 16.0f * si::metre;
    meters_t m_tile_z = 16.0f * si::metre;
    int m_tiles_x = 0;
    int m_tiles_z = 0;
    std::vector<bool> m_picked;
    // The basket's picks as last followed.
    std::vector<std::uint32_t> m_followed;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_tiles;
    // Only recently visible tiles; empty meshes stay cached too.
    std::unordered_map<std::uint64_t, TileMesh> m_meshes;
  };
}

#endif
