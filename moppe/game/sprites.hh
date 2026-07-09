#ifndef MOPPE_GAME_SPRITES_HH
#define MOPPE_GAME_SPRITES_HH

#include <moppe/render/renderer.hh>

namespace moppe {
namespace game {
  // The soft round sprite shared by Dust and BlobShadow: white with
  // a smooth radial alpha falloff, so puffs read as smoke and blob
  // shadows as shade instead of hard-edged squares.  (The GL build
  // generated this same 64x64 image twice.)
  render::TexturePtr make_soft_disc_texture (render::Renderer& r);
}
}

#endif
