// moppe's renderer over NHAL: the game-shaped render::Renderer interface,
// drawn with Luv-language shaders through Metal 4, Direct3D 12, or Vulkan.
// It grows feature by feature beside the Metal renderer; the Mac host
// chooses one with --renderer, and the Linux host has only this one.
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
    StageCode forest_far_shadow[2];
    StageCode bloom_bright[2];
    StageCode bloom_blur[2];
    StageCode grass[2];
    StageCode grass_tiles;
    StageCode sward[2];
    StageCode sward_patches;
    StageCode gtao[2];
    StageCode gtao_blur[2];
    StageCode shafts[2];
    StageCode boulders[2];
    StageCode boulders_shadow[2];
    StageCode boulder_cull;
    StageCode rain[2];
    StageCode water[2];
    StageCode leaves[2];
    StageCode waterfall[2];
    StageCode scene_copy[2];
  };

  // The programs' MSL, embedded in the binary at build time.
  const WorldShaders& world_shaders_metal ();
  // Their DXIL, compiled ahead of time by DXC.
  const WorldShaders& world_shaders_d3d12 ();
  // Their SPIR-V, embedded in the binary at build time.
  const WorldShaders& world_shaders_vulkan ();

  // A renderer drawing into the device's surface. `scale` is the
  // surface's pixels per point, which the HUD and the game's layout use.
  std::unique_ptr<render::Renderer>
  create_renderer (std::unique_ptr<Device> device,
                   const WorldShaders& shaders,
                   float scale);
}

#endif
