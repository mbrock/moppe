// The world programs' SPIR-V, as luv-shaderc lowered world.lisp into the
// build directory, embedded so the game carries no loose shader files. CMake
// generates the arrays (world_stages_spirv.inc) and the program list
// (world_programs.def) from world.lisp. The modules are words, so their
// bytes are word-aligned.
#include <moppe/nhal/renderer/nhal_renderer.hh>

#include <cstddef>
#include <cstdint>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"

namespace moppe::nhal {
  namespace {
#include <world_stages_spirv.inc>

    template <std::size_t N>
    StageCode spirv (const unsigned char (&code)[N]) {
      static_assert (N % 4 == 0);
      return { {},
               {},
               { reinterpret_cast<const std::uint32_t*> (code), N / 4 } };
    }
  }

  const WorldShaders& world_shaders_vulkan () {
    static const WorldShaders shaders {
#define MOPPE_WORLD_DRAWN(name)                                                \
  { spirv (name##_vertex), spirv (name##_fragment) },
#define MOPPE_WORLD_CASTER(name) { spirv (name##_vertex), {} },
#define MOPPE_WORLD_COMPUTE(name) spirv (name##_compute),
#include <world_programs.def>
#undef MOPPE_WORLD_DRAWN
#undef MOPPE_WORLD_CASTER
#undef MOPPE_WORLD_COMPUTE
    };
    return shaders;
  }
}

#pragma clang diagnostic pop
