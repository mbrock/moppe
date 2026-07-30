#ifndef MOPPE_GAME_FOREST_HH
#define MOPPE_GAME_FOREST_HH

#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace moppe::game {
  enum class ForestForm { broadleaf, conifer };

  using TreeSizeFactor =
    mp_units::quantity<tree_size_factor[mp_units::one], float>;

  struct ForestSite {
    position_t position {};
    terrain::TerrainNormal normal {};
    map::ForestCover cover {};
    TreeSizeFactor size = 1.0f * tree_size_factor[mp_units::one];
    std::uint32_t seed = 0;
    ForestForm form = ForestForm::broadleaf;
  };

  struct ForestPlan {
    std::vector<ForestSite> sites;
    spatial_extent_t period {};
  };

  struct ForestView {
    position_t position {};
    Vec3 forward { 0, 0, 1 };
    Vec3 right { 1, 0, 0 };
    Vec3 up { 0, 1, 0 };
    degrees_t vertical_field_of_view = 70.0f * u::deg;
    magnitude_t aspect_ratio = 1.0f * mp_units::one;
  };

  // Convert the continuous canopy field into stable individuals on a
  // jittered grid. Positions and identities depend only on world seed and
  // lattice cell, so revisiting an area never produces a different forest.
  [[nodiscard]] ForestPlan
  plan_global_forest (const map::SurfaceGeometry& surface,
                      const map::SurfaceReadings& readings,
                      std::uint32_t seed,
                      meters_t spacing = 12.0f * u::m);

  // Terrain-scale presentation of the population. Each retained mesh owns a
  // cullable world chunk of deliberately cheap trunks and crown volumes; the
  // detailed Atelier stand remains a separate near/hero representation.
  class ForestLandscape {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::SurfaceGeometry& surface,
                  const map::SurfaceReadings& readings,
                  std::uint32_t seed);
    void draw (render::Renderer& renderer, const ForestView& view) const;

    std::size_t tree_count () const noexcept {
      return m_tree_count;
    }

  private:
    struct Chunk {
      position_t center {};
      meters_t radius {};
      render::MeshPtr detailed_mesh;
      render::MeshPtr distant_mesh;
    };

    std::vector<Chunk> m_chunks;
    spatial_extent_t m_period {};
    std::size_t m_tree_count = 0;
    int m_chunks_x = 0;
    int m_chunks_z = 0;
  };
}

#endif
