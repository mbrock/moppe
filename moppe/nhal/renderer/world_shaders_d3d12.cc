// The world programs' DXIL, as DXC compiled luv-shaderc's HLSL lowering of
// world.lisp into byte-array headers in the build directory. CMake generates
// the includes (world_stages_hlsl.inc) and the program list
// (world_programs.def) from world.lisp.
#include <moppe/nhal/renderer/nhal_renderer.hh>

#include <world_stages_hlsl.inc>

#include <cstddef>

namespace moppe::nhal {
  namespace {
    template <std::size_t N>
    StageCode dxil (const unsigned char (&code)[N]) {
      return { {}, { code, N } };
    }
  }

  const WorldShaders& world_shaders_d3d12 () {
    static const WorldShaders shaders {
#define MOPPE_WORLD_DRAWN(name)                                                \
  { dxil (shader_##name##_vertex), dxil (shader_##name##_fragment) },
#define MOPPE_WORLD_CASTER(name) { dxil (shader_##name##_vertex), {} },
#define MOPPE_WORLD_COMPUTE(name) dxil (shader_##name##_compute),
#include <world_programs.def>
#undef MOPPE_WORLD_DRAWN
#undef MOPPE_WORLD_CASTER
#undef MOPPE_WORLD_COMPUTE
    };
    return shaders;
  }
}
