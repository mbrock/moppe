#ifndef MOPPE_GAME_RIVER_SURFACE_HH
#define MOPPE_GAME_RIVER_SURFACE_HH

#include <moppe/map/generate.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/drainage.hh>

namespace moppe::game {
  render::DrawList build_river_ribbons (const map::HeightMap& map,
                                        const terrain::RiverNetwork& rivers);

  class RiverSurface {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::HeightMap& map,
                  const terrain::RiverNetwork& rivers);
    void clear ();
    void draw (render::Renderer& renderer, const Vec3& camera) const;
    bool empty () const {
      return !m_mesh;
    }

  private:
    render::MeshPtr m_mesh;
    Vec3 m_period;
    bool m_periodic = false;
  };
}

#endif
