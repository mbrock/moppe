#ifndef MOPPE_GAME_WATERFALL_SURFACE_HH
#define MOPPE_GAME_WATERFALL_SURFACE_HH

#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/drainage.hh>

namespace moppe::game {
  // Running rivers are reconstructed from the continuous WaterSheets field.
  // Only a nickpoint needs separate geometry because one height per x/z
  // cannot represent a vertical falling sheet.
  render::DrawList
  build_waterfall_curtains (const map::SurfaceGeometry& surface,
                            const terrain::RiverNetwork& rivers);

  class WaterfallSurface {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::SurfaceGeometry& surface,
                  const terrain::RiverNetwork& rivers);
    void clear ();
    void draw (render::Renderer& renderer, const position_t& camera) const;
    bool empty () const {
      return !m_mesh;
    }

  private:
    render::MeshPtr m_mesh;
    plan_extent_t m_period;
  };
}

#endif
