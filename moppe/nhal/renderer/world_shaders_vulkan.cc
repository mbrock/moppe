// The world programs' SPIR-V, as luv-shaderc lowered world.lisp into the
// build directory, embedded so the game carries no loose shader files.
#include <moppe/nhal/renderer/nhal_renderer.hh>

#include <cstddef>
#include <cstdint>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"

namespace moppe::nhal {
  namespace {
    // #embed cannot sit in a macro; each stage is spelled out. The modules
    // are words, so their bytes are word-aligned.
    alignas (4) const unsigned char terrain_vertex[] = {
#embed "terrain.vertex.spv"
    };
    alignas (4) const unsigned char terrain_fragment[] = {
#embed "terrain.fragment.spv"
    };
    alignas (4) const unsigned char sky_vertex[] = {
#embed "sky.vertex.spv"
    };
    alignas (4) const unsigned char sky_fragment[] = {
#embed "sky.fragment.spv"
    };
    alignas (4) const unsigned char uber_vertex[] = {
#embed "uber.vertex.spv"
    };
    alignas (4) const unsigned char uber_fragment[] = {
#embed "uber.fragment.spv"
    };
    alignas (4) const unsigned char hud_vertex[] = {
#embed "hud.vertex.spv"
    };
    alignas (4) const unsigned char hud_fragment[] = {
#embed "hud.fragment.spv"
    };
    alignas (4) const unsigned char resolve_vertex[] = {
#embed "resolve.vertex.spv"
    };
    alignas (4) const unsigned char resolve_fragment[] = {
#embed "resolve.fragment.spv"
    };
    alignas (4) const unsigned char present_vertex[] = {
#embed "present.vertex.spv"
    };
    alignas (4) const unsigned char present_fragment[] = {
#embed "present.fragment.spv"
    };
    alignas (4) const unsigned char slug_text_vertex[] = {
#embed "slug_text.vertex.spv"
    };
    alignas (4) const unsigned char slug_text_fragment[] = {
#embed "slug_text.fragment.spv"
    };
    alignas (4) const unsigned char forest_vertex[] = {
#embed "forest.vertex.spv"
    };
    alignas (4) const unsigned char forest_fragment[] = {
#embed "forest.fragment.spv"
    };
    alignas (4) const unsigned char forest_cull_compute[] = {
#embed "forest_cull.compute.spv"
    };
    alignas (4) const unsigned char exposure_compute[] = {
#embed "exposure.compute.spv"
    };
    alignas (4) const unsigned char terrain_shadow_vertex[] = {
#embed "terrain_shadow.vertex.spv"
    };
    alignas (4) const unsigned char forest_shadow_vertex[] = {
#embed "forest_shadow.vertex.spv"
    };
    alignas (4) const unsigned char bloom_bright_vertex[] = {
#embed "bloom_bright.vertex.spv"
    };
    alignas (4) const unsigned char bloom_bright_fragment[] = {
#embed "bloom_bright.fragment.spv"
    };
    alignas (4) const unsigned char bloom_blur_vertex[] = {
#embed "bloom_blur.vertex.spv"
    };
    alignas (4) const unsigned char bloom_blur_fragment[] = {
#embed "bloom_blur.fragment.spv"
    };
    alignas (4) const unsigned char grass_vertex[] = {
#embed "grass.vertex.spv"
    };
    alignas (4) const unsigned char grass_fragment[] = {
#embed "grass.fragment.spv"
    };
    alignas (4) const unsigned char grass_tiles_compute[] = {
#embed "grass_tiles.compute.spv"
    };
    alignas (4) const unsigned char sward_vertex[] = {
#embed "sward.vertex.spv"
    };
    alignas (4) const unsigned char sward_fragment[] = {
#embed "sward.fragment.spv"
    };
    alignas (4) const unsigned char sward_patches_compute[] = {
#embed "sward_patches.compute.spv"
    };
    alignas (4) const unsigned char gtao_vertex[] = {
#embed "gtao.vertex.spv"
    };
    alignas (4) const unsigned char gtao_fragment[] = {
#embed "gtao.fragment.spv"
    };
    alignas (4) const unsigned char gtao_blur_vertex[] = {
#embed "gtao_blur.vertex.spv"
    };
    alignas (4) const unsigned char gtao_blur_fragment[] = {
#embed "gtao_blur.fragment.spv"
    };
    alignas (4) const unsigned char shafts_vertex[] = {
#embed "shafts.vertex.spv"
    };
    alignas (4) const unsigned char shafts_fragment[] = {
#embed "shafts.fragment.spv"
    };
    alignas (4) const unsigned char boulders_vertex[] = {
#embed "boulders.vertex.spv"
    };
    alignas (4) const unsigned char boulders_fragment[] = {
#embed "boulders.fragment.spv"
    };
    alignas (4) const unsigned char boulders_shadow_vertex[] = {
#embed "boulders_shadow.vertex.spv"
    };
    alignas (4) const unsigned char boulder_cull_compute[] = {
#embed "boulder_cull.compute.spv"
    };
    alignas (4) const unsigned char rain_vertex[] = {
#embed "rain.vertex.spv"
    };
    alignas (4) const unsigned char rain_fragment[] = {
#embed "rain.fragment.spv"
    };

    alignas (4) const unsigned char water_vertex[] = {
#embed "water.vertex.spv"
    };
    alignas (4) const unsigned char water_fragment[] = {
#embed "water.fragment.spv"
    };
    alignas (4) const unsigned char leaves_vertex[] = {
#embed "leaves.vertex.spv"
    };
    alignas (4) const unsigned char leaves_fragment[] = {
#embed "leaves.fragment.spv"
    };

    template <std::size_t N>
    StageCode spirv (const unsigned char (&code)[N]) {
      static_assert (N % 4 == 0);
      return { {}, {},
               { reinterpret_cast<const std::uint32_t*> (code), N / 4 } };
    }
  }

  const WorldShaders& world_shaders_vulkan () {
    static const WorldShaders shaders {
      { spirv (terrain_vertex), spirv (terrain_fragment) },
      { spirv (sky_vertex), spirv (sky_fragment) },
      { spirv (uber_vertex), spirv (uber_fragment) },
      { spirv (hud_vertex), spirv (hud_fragment) },
      { spirv (resolve_vertex), spirv (resolve_fragment) },
      { spirv (present_vertex), spirv (present_fragment) },
      { spirv (slug_text_vertex), spirv (slug_text_fragment) },
      { spirv (forest_vertex), spirv (forest_fragment) },
      spirv (forest_cull_compute),
      spirv (exposure_compute),
      { spirv (terrain_shadow_vertex), {} },
      { spirv (forest_shadow_vertex), {} },
      { spirv (bloom_bright_vertex), spirv (bloom_bright_fragment) },
      { spirv (bloom_blur_vertex), spirv (bloom_blur_fragment) },
      { spirv (grass_vertex), spirv (grass_fragment) },
      spirv (grass_tiles_compute),
      { spirv (sward_vertex), spirv (sward_fragment) },
      spirv (sward_patches_compute),
      { spirv (gtao_vertex), spirv (gtao_fragment) },
      { spirv (gtao_blur_vertex), spirv (gtao_blur_fragment) },
      { spirv (shafts_vertex), spirv (shafts_fragment) },
      { spirv (boulders_vertex), spirv (boulders_fragment) },
      { spirv (boulders_shadow_vertex), {} },
      spirv (boulder_cull_compute),
      { spirv (rain_vertex), spirv (rain_fragment) },
      { spirv (water_vertex), spirv (water_fragment) },
      { spirv (leaves_vertex), spirv (leaves_fragment) },
    };
    return shaders;
  }
}

#pragma clang diagnostic pop
