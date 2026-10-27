#ifndef MOPPE_GAME_BRIDGE_RENDER_HH
#define MOPPE_GAME_BRIDGE_RENDER_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/roads.hh>

#include <vector>

namespace moppe::game {
  // The roads' bridges (terrain::RoadBridge) as timber trestles: a plank
  // deck between kerbs and railings on two stringers, trestles standing in
  // the river every dozen metres or so, and a stone abutment at each end.
  // Each bridge is one mesh, drawn at its image nearest the camera.
  class BridgeSurface {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::SurfaceGeometry& surface,
                  const terrain::RoadNetwork& roads,
                  const terrain::RoadFormation& formation);
    void clear ();
    void draw (render::Renderer& renderer, const Vec3& camera) const;

  private:
    struct Bridge {
      render::MeshPtr mesh;
      // The mesh's origin, inside the period.
      Vec3 centre;
    };
    std::vector<Bridge> m_bridges;
    float m_period_x = 0.0f;
    float m_period_z = 0.0f;
  };
}

#endif
