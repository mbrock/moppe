// moppe's renderer over NHAL: the game-shaped render::Renderer interface,
// drawn with Luv-language shaders through Metal 4 or Direct3D 12. It grows
// feature by feature beside the Metal renderer; the host chooses one with
// --renderer.
#ifndef MOPPE_NHAL_RENDERER_HH
#define MOPPE_NHAL_RENDERER_HH

#include <moppe/nhal/nhal.hh>
#include <moppe/render/renderer.hh>

#include <memory>

namespace moppe::nhal {
  // Each program's two stages, vertex then fragment, as luv-shaderc lowers
  // moppe/nhal/renderer/shaders/world.lisp. The renderer copies the code.
  struct WorldShaders {
    StageCode terrain[2];
    StageCode sky[2];
    StageCode uber[2];
    StageCode hud[2];
    StageCode resolve[2];
    StageCode present[2];
    StageCode slug_text[2];
    StageCode forest[2];
    StageCode forest_cull;
    StageCode exposure;
    // Vertex stages only; their fragment code is empty.
    StageCode terrain_shadow[2];
    StageCode forest_shadow[2];
  };

  // The programs' MSL, embedded in the binary at build time.
  const WorldShaders& world_shaders_metal ();

  // A renderer drawing into the device's surface. `scale` is the
  // surface's pixels per point, which the HUD and the game's layout use.
  std::unique_ptr<render::Renderer>
  create_renderer (std::unique_ptr<Device> device,
                   const WorldShaders& shaders,
                   float scale);
}

#endif
