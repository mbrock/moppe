#ifndef MOPPE_GAME_RIVER_SURFACE_HH
#define MOPPE_GAME_RIVER_SURFACE_HH

#include <moppe/map/generate.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

namespace moppe::game {
  float visible_river_minimum_area (const terrain::TerrainGrid& grid) noexcept;

  render::DrawList build_river_ribbons (const map::HeightMap& map,
                                        const terrain::FloodField& flood,
                                        const terrain::LakeCensus& census,
                                        const terrain::DrainageGraph& drainage,
                                        const terrain::RiverNetwork& rivers);

  class RiverSurface {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::HeightMap& map,
                  const terrain::FloodField& flood,
                  const terrain::LakeCensus& census,
                  const terrain::DrainageGraph& drainage,
                  const terrain::RiverNetwork& rivers);
    void clear ();
    void draw (render::Renderer& renderer, const Vector3D& camera) const;
    bool empty () const {
      return !m_mesh;
    }

  private:
    render::MeshPtr m_mesh;
    Vector3D m_period;
    bool m_periodic = false;
  };
}

#endif
