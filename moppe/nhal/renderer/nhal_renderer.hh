// moppe's renderer over NHAL: the game-shaped render::Renderer interface,
// drawn with Luv-language shaders through Metal 4, Direct3D 12, Vulkan, or
// WebGPU.
#ifndef MOPPE_NHAL_RENDERER_HH
#define MOPPE_NHAL_RENDERER_HH

#include <moppe/nhal/nhal.hh>
#include <moppe/render/renderer.hh>

#include <memory>

namespace moppe::nhal {
  // Each world program's code, as luv-shaderc lowers
  // moppe/nhal/renderer/shaders/world.lisp: a drawn program's vertex and
  // fragment stages, a shadow caster's vertex stage and an empty fragment
  // stage, or a compute program's one stage. The members are the programs
  // of world.lisp, in the order of the world_programs.def that CMake
  // generates from it. The renderer reads the code in place, so it must
  // outlive the renderer; the functions below return static ones.
  struct WorldShaders {
#define MOPPE_WORLD_DRAWN(name) StageCode name[2];
#define MOPPE_WORLD_CASTER(name) StageCode name[2];
#define MOPPE_WORLD_COMPUTE(name) StageCode name;
#include <world_programs.def>
#undef MOPPE_WORLD_DRAWN
#undef MOPPE_WORLD_CASTER
#undef MOPPE_WORLD_COMPUTE
  };

  // The programs' MSL, embedded in the binary at build time.
  const WorldShaders& world_shaders_metal ();
  // Their DXIL, compiled ahead of time by DXC.
  const WorldShaders& world_shaders_d3d12 ();
  // Their SPIR-V, embedded in the binary at build time.
  const WorldShaders& world_shaders_vulkan ();
  // Their WGSL, embedded in the binary at build time.
  const WorldShaders& world_shaders_webgpu ();

  // A renderer drawing into the device's surface. `scale` is the
  // surface's pixels per point, which the HUD and the game's layout use.
  std::unique_ptr<render::Renderer> create_renderer (
    std::unique_ptr<Device> device, const WorldShaders& shaders, float scale);
}

#endif
