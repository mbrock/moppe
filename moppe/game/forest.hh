#ifndef MOPPE_GAME_FOREST_HH
#define MOPPE_GAME_FOREST_HH

#include <moppe/game/forest_plan.hh>
#include <moppe/render/renderer.hh>

#include <cstddef>
#include <cstdint>

namespace moppe::game {
  // Presentation owner for the global population. The game keeps typed sites;
  // the renderer keeps compact GPU instances and decides projected detail.
  // No complete tree mesh is retained on the CPU.
  class ForestLandscape {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::SurfaceGeometry& surface,
                  const map::SurfaceReadings& readings,
                  std::uint32_t seed);
    void rebuild (render::Renderer& renderer, const ForestPlan& plan);
    void draw (render::Renderer& renderer) const;

    std::size_t tree_count () const noexcept {
      return m_tree_count;
    }

    std::size_t resident_bytes () const noexcept {
      return m_resident_bytes;
    }

  private:
    std::size_t m_tree_count = 0;
    std::size_t m_resident_bytes = 0;
  };
}

#endif
