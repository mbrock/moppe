// The world programs' MSL, as luv-shaderc lowered world.lisp into the build
// directory, embedded so the game carries no loose shader files. CMake
// generates the arrays (world_stages_msl.inc) and the program list
// (world_programs.def) from world.lisp.
#include <moppe/nhal/renderer/nhal_renderer.hh>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"

namespace moppe::nhal {
  namespace {
#include <world_stages_msl.inc>

    StageCode msl (const char* code) {
      return { code, {} };
    }
  }

  const WorldShaders& world_shaders_metal () {
    static const WorldShaders shaders {
#define MOPPE_WORLD_DRAWN(name) { msl (name##_vertex), msl (name##_fragment) },
#define MOPPE_WORLD_CASTER(name) { msl (name##_vertex), {} },
#define MOPPE_WORLD_COMPUTE(name) msl (name##_compute),
#include <world_programs.def>
#undef MOPPE_WORLD_DRAWN
#undef MOPPE_WORLD_CASTER
#undef MOPPE_WORLD_COMPUTE
    };
    return shaders;
  }
}

#pragma clang diagnostic pop
