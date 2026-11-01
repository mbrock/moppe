#ifndef MOPPE_GAME_BRIDGE_RENDER_HH
#define MOPPE_GAME_BRIDGE_RENDER_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/roads.hh>

#include <vector>

namespace moppe::game {
  // The roads' timber. Their bridges (terrain::RoadBridge) as trestles: a
  // plank deck between kerbs and railings on two stringers, trestles
  // standing in the river every dozen metres or so, and a stone abutment
  // at each end. Along the paved lanes, the guardrails the road surface
  // lays where the ground drops away (terrain::Guardrail): a galvanized
  // W-beam on posts, its ends turned down into the ground. Each bridge,
  // and each stretch of guardrail, is one mesh, drawn at its image
  // nearest the camera.
  class BridgeSurface {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::SurfaceGeometry& surface,
                  const terrain::RoadNetwork& roads,
                  const terrain::RoadSurface& road_surface);
    void clear ();
    void draw (render::Renderer& renderer, const Vec3& camera) const;

  private:
    struct Bridge {
      render::MeshPtr mesh;
      // The mesh's origin, inside the period.
      Vec3 centre;
      // How far off it is still drawn, metres.
      float reach = 2500.0f;
    };
    void add_guardrails (render::Renderer& renderer,
                         const terrain::RoadSurface& roads);
    std::vector<Bridge> m_bridges;
    float m_period_x = 0.0f;
    float m_period_z = 0.0f;
  };
}

#endif
