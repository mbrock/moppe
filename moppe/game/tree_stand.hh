#ifndef MOPPE_GAME_TREE_STAND_HH
#define MOPPE_GAME_TREE_STAND_HH

#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace moppe::game {
  struct TreeSite {
    Vec3 position;
    Vec3 normal;
    float habitat = 0.0f;
    float scale = 1.0f;
    std::uint32_t seed = 0;
  };

  struct TreeGrove {
    std::vector<TreeSite> sites;
    Vec3 camera_eye;
    Vec3 camera_target;
  };

  // Select a small, deterministic population from the materialized surface
  // habitat. This is renderer-independent so site ecology can be tested
  // without a GPU.
  [[nodiscard]] TreeGrove plan_tree_grove (const map::Surface& surface,
                                           std::uint32_t seed,
                                           std::size_t desired_count = 9);

  // Moppe's extrinsic presentation of the Atelier organism. The stand bakes
  // several unique trees into one retained world-space mesh; the scene shader
  // supplies continuous wind from per-vertex flexibility weights.
  class TreeStand {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::Surface& surface,
                  std::uint32_t seed,
                  std::size_t desired_count = 9);
    void draw (render::Renderer& renderer) const;

    const TreeGrove& grove () const noexcept {
      return m_grove;
    }
    bool empty () const noexcept {
      return !m_mesh;
    }

  private:
    TreeGrove m_grove;
    render::MeshPtr m_mesh;
  };
}

#endif
