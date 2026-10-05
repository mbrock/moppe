#ifndef MOPPE_GAME_BOULDERS_HH
#define MOPPE_GAME_BOULDERS_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/watercourse.hh>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace moppe::game {
  // One loose stone lying on the ground. The body is a faceted lump of
  // horizontal radius `radius` and vertical half-height
  // boulder_squash * radius, already settled into the ground: `centre` is
  // the body's centre, sunk so its underside sits below the soil.
  struct BoulderSite {
    position_t centre {};
    terrain::TerrainNormal normal {};
    meters_t radius {};
    map::SurfaceMoisture moisture {};
    std::uint32_t seed = 0;
  };

  struct BoulderPlan {
    std::vector<BoulderSite> sites;
    spatial_extent_t period {};
  };

  // A boulder is broader than it is tall, and a third of its radius lies
  // buried, so the planner, the collider, and the shader agree on its shape.
  inline constexpr float boulder_squash = 0.7f;
  inline constexpr float boulder_burial = 0.3f;

  // Where the world's history would have left loose rock: talus on steep
  // eroded slopes, scree below cliffs, cobbles along streams and shores, and
  // erratics on the high open ground. Rocks are scarce on the forest floor
  // and in lush meadows and absent from the trail tread, the home base, and
  // deep water. A jittered lattice of candidates is thinned by that
  // rockiness and by low-frequency clustering, so rocks come in groups, and
  // larger rocks gather a few smaller ones around them.
  [[nodiscard]] BoulderPlan plan_boulders (const map::SurfaceGeometry& surface,
                                           const map::SurfaceReadings& readings,
                                           const terrain::WaterSheets& water,
                                           std::uint32_t seed,
                                           meters_t sea_level,
                                           meters_t land_relief);

  // Presentation owner for the planned rocks: compact renderer instances
  // and the colliders for the rocks large enough to stop a bike.
  class BoulderLandscape {
  public:
    void rebuild (render::Renderer& renderer, const BoulderPlan& plan);
    void draw (render::Renderer& renderer) const;

    std::size_t boulder_count () const noexcept {
      return m_boulder_count;
    }

    // Short buried capsules in the shape of the larger boulders. Stones a
    // dirt bike simply rides over have none.
    const std::vector<mov::Trunk>& colliders () const noexcept {
      return m_colliders;
    }

  private:
    std::size_t m_boulder_count = 0;
    std::vector<mov::Trunk> m_colliders;
  };
}

#endif
