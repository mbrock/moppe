#ifndef MOPPE_GAME_FOREST_HH
#define MOPPE_GAME_FOREST_HH

#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace moppe::game {
  enum class ForestForm { broadleaf, conifer };

  struct ForestSite {
    Vec3 position;
    Vec3 normal;
    float cover = 0.0f;
    float scale = 1.0f;
    std::uint32_t seed = 0;
    ForestForm form = ForestForm::broadleaf;
  };

  struct ForestPlan {
    std::vector<ForestSite> sites;
    Vec3 period;
    bool periodic = false;
  };

  // Convert the continuous canopy field into stable individuals on a
  // jittered grid. Positions and identities depend only on world seed and
  // lattice cell, so revisiting an area never produces a different forest.
  [[nodiscard]] ForestPlan plan_global_forest (const map::Surface& surface,
                                               std::uint32_t seed,
                                               float spacing = 12.0f);

  // Terrain-scale presentation of the population. Each retained mesh owns a
  // cullable world chunk of deliberately cheap trunks and crown volumes; the
  // detailed Atelier stand remains a separate near/hero representation.
  class ForestLandscape {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::Surface& surface,
                  std::uint32_t seed);
    void draw (render::Renderer& renderer,
               const Vec3& camera,
               const Vec3& view_direction) const;

    std::size_t tree_count () const noexcept {
      return m_tree_count;
    }

  private:
    struct Chunk {
      Vec3 center;
      float radius = 0.0f;
      render::MeshPtr mesh;
    };

    std::vector<Chunk> m_chunks;
    Vec3 m_period;
    std::size_t m_tree_count = 0;
    int m_chunks_x = 0;
    int m_chunks_z = 0;
    bool m_periodic = false;
  };
}

#endif
