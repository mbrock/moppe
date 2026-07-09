#ifndef MOPPE_GAME_BLOB_SHADOW_HH
#define MOPPE_GAME_BLOB_SHADOW_HH

#include <moppe/map/generate.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

namespace moppe {
namespace game {
  // A soft dark disc projected on the ground under a vehicle: the
  // classic cheap stand-in for a real cast shadow, and it reads
  // wonderfully -- especially mid rocket-jump, when it shrinks and
  // fades below you.
  class BlobShadow {
  public:
    void load (render::Renderer& r);

    void draw (render::DrawList& dl, const map::HeightMap& map,
	       const Vector3D& pos, float radius);

  private:
    render::TexturePtr m_tex;
  };
}
}

#endif
