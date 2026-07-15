#ifndef MOPPE_GAME_TREE_STAND_HH
#define MOPPE_GAME_TREE_STAND_HH

#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace moppe::game {
  enum class TreeCohort { sapling, young, canopy };

  struct TreeSite {
    Vec3 position;
    Vec3 normal;
    float habitat = 0.0f;
    float scale = 1.0f;
    std::uint32_t seed = 0;
    TreeCohort cohort = TreeCohort::young;
  };

  struct TreeGrove {
    std::vector<TreeSite> sites;
    Vec3 camera_eye;
    Vec3 camera_target;
  };

  // Recruit a deterministic, mixed-age forest from the materialized habitat,
  // then let larger crowns self-thin the population. An optional focus keeps
  // the forest near a place in the playable world without weakening the
  // habitat test. This remains renderer-independent so the population process
  // can be tested without a GPU.
  [[nodiscard]] TreeGrove
  plan_tree_grove (const map::Surface& surface,
                   std::uint32_t seed,
                   std::size_t desired_count = 9,
                   std::optional<Vec3> focus = std::nullopt);

  // Moppe's extrinsic presentation of the Atelier organism. The stand bakes
  // several unique trees into one retained world-space mesh; the scene shader
  // supplies continuous wind from per-vertex flexibility weights.
  class TreeStand {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::Surface& surface,
                  std::uint32_t seed,
                  std::size_t desired_count = 9,
                  std::optional<Vec3> focus = std::nullopt);
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
