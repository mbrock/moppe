// The world programs' DXIL, as DXC compiled luv-shaderc's HLSL lowering of
// world.lisp into byte-array headers in the build directory.
#include <moppe/nhal/renderer/nhal_renderer.hh>

#include <shader_bloom_blur_fragment.h>
#include <shader_bloom_blur_vertex.h>
#include <shader_bloom_bright_fragment.h>
#include <shader_bloom_bright_vertex.h>
#include <shader_exposure_compute.h>
#include <shader_forest_cull_compute.h>
#include <shader_forest_fragment.h>
#include <shader_forest_shadow_vertex.h>
#include <shader_forest_vertex.h>
#include <shader_grass_fragment.h>
#include <shader_grass_tiles_compute.h>
#include <shader_grass_vertex.h>
#include <shader_gtao_blur_fragment.h>
#include <shader_gtao_blur_vertex.h>
#include <shader_gtao_fragment.h>
#include <shader_gtao_vertex.h>
#include <shader_hud_fragment.h>
#include <shader_hud_vertex.h>
#include <shader_present_fragment.h>
#include <shader_present_vertex.h>
#include <shader_resolve_fragment.h>
#include <shader_resolve_vertex.h>
#include <shader_shafts_fragment.h>
#include <shader_shafts_vertex.h>
#include <shader_sky_fragment.h>
#include <shader_sky_vertex.h>
#include <shader_slug_text_fragment.h>
#include <shader_sward_fragment.h>
#include <shader_sward_patches_compute.h>
#include <shader_sward_vertex.h>
#include <shader_slug_text_vertex.h>
#include <shader_terrain_fragment.h>
#include <shader_terrain_shadow_vertex.h>
#include <shader_terrain_vertex.h>
#include <shader_uber_fragment.h>
#include <shader_uber_vertex.h>

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
      { dxil (shader_terrain_vertex), dxil (shader_terrain_fragment) },
      { dxil (shader_sky_vertex), dxil (shader_sky_fragment) },
      { dxil (shader_uber_vertex), dxil (shader_uber_fragment) },
      { dxil (shader_hud_vertex), dxil (shader_hud_fragment) },
      { dxil (shader_resolve_vertex), dxil (shader_resolve_fragment) },
      { dxil (shader_present_vertex), dxil (shader_present_fragment) },
      { dxil (shader_slug_text_vertex), dxil (shader_slug_text_fragment) },
      { dxil (shader_forest_vertex), dxil (shader_forest_fragment) },
      dxil (shader_forest_cull_compute),
      dxil (shader_exposure_compute),
      { dxil (shader_terrain_shadow_vertex), {} },
      { dxil (shader_forest_shadow_vertex), {} },
      { dxil (shader_bloom_bright_vertex),
        dxil (shader_bloom_bright_fragment) },
      { dxil (shader_bloom_blur_vertex), dxil (shader_bloom_blur_fragment) },
      { dxil (shader_grass_vertex), dxil (shader_grass_fragment) },
      dxil (shader_grass_tiles_compute),
      { dxil (shader_sward_vertex), dxil (shader_sward_fragment) },
      dxil (shader_sward_patches_compute),
      { dxil (shader_gtao_vertex), dxil (shader_gtao_fragment) },
      { dxil (shader_gtao_blur_vertex), dxil (shader_gtao_blur_fragment) },
      { dxil (shader_shafts_vertex), dxil (shader_shafts_fragment) },
    };
    return shaders;
  }
}
