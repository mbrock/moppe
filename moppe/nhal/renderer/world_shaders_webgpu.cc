// The world programs' WGSL, as luv-shaderc lowered world.lisp into the build
// directory, embedded so the game carries no loose shader files.
#include <moppe/nhal/renderer/nhal_renderer.hh>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"

namespace moppe::nhal {
  namespace {
    // #embed cannot sit in a macro; each stage is spelled out.
    const char terrain_vertex[] = {
#embed "terrain.vertex.wgsl"
      , 0 };
    const char terrain_fragment[] = {
#embed "terrain.fragment.wgsl"
      , 0 };
    const char sky_vertex[] = {
#embed "sky.vertex.wgsl"
      , 0 };
    const char sky_fragment[] = {
#embed "sky.fragment.wgsl"
      , 0 };
    const char uber_vertex[] = {
#embed "uber.vertex.wgsl"
      , 0 };
    const char uber_fragment[] = {
#embed "uber.fragment.wgsl"
      , 0 };
    const char hud_vertex[] = {
#embed "hud.vertex.wgsl"
      , 0 };
    const char hud_fragment[] = {
#embed "hud.fragment.wgsl"
      , 0 };
    const char resolve_vertex[] = {
#embed "resolve.vertex.wgsl"
      , 0 };
    const char resolve_fragment[] = {
#embed "resolve.fragment.wgsl"
      , 0 };
    const char present_vertex[] = {
#embed "present.vertex.wgsl"
      , 0 };
    const char present_fragment[] = {
#embed "present.fragment.wgsl"
      , 0 };
    const char slug_text_vertex[] = {
#embed "slug_text.vertex.wgsl"
      , 0 };
    const char slug_text_fragment[] = {
#embed "slug_text.fragment.wgsl"
      , 0 };
    const char forest_vertex[] = {
#embed "forest.vertex.wgsl"
      , 0 };
    const char forest_fragment[] = {
#embed "forest.fragment.wgsl"
      , 0 };
    const char forest_cull_compute[] = {
#embed "forest_cull.compute.wgsl"
      , 0 };
    const char exposure_compute[] = {
#embed "exposure.compute.wgsl"
      , 0 };
    const char terrain_shadow_vertex[] = {
#embed "terrain_shadow.vertex.wgsl"
      , 0 };
    const char forest_shadow_vertex[] = {
#embed "forest_shadow.vertex.wgsl"
      , 0 };
    const char forest_far_shadow_vertex[] = {
#embed "forest_far_shadow.vertex.wgsl"
      , 0 };
    const char bloom_bright_vertex[] = {
#embed "bloom_bright.vertex.wgsl"
      , 0 };
    const char bloom_bright_fragment[] = {
#embed "bloom_bright.fragment.wgsl"
      , 0 };
    const char bloom_blur_vertex[] = {
#embed "bloom_blur.vertex.wgsl"
      , 0 };
    const char bloom_blur_fragment[] = {
#embed "bloom_blur.fragment.wgsl"
      , 0 };
    const char grass_vertex[] = {
#embed "grass.vertex.wgsl"
      , 0 };
    const char grass_fragment[] = {
#embed "grass.fragment.wgsl"
      , 0 };
    const char grass_tiles_compute[] = {
#embed "grass_tiles.compute.wgsl"
      , 0 };
    const char sward_vertex[] = {
#embed "sward.vertex.wgsl"
      , 0 };
    const char sward_fragment[] = {
#embed "sward.fragment.wgsl"
      , 0 };
    const char sward_patches_compute[] = {
#embed "sward_patches.compute.wgsl"
      , 0 };
    const char gtao_vertex[] = {
#embed "gtao.vertex.wgsl"
      , 0 };
    const char gtao_fragment[] = {
#embed "gtao.fragment.wgsl"
      , 0 };
    const char gtao_blur_vertex[] = {
#embed "gtao_blur.vertex.wgsl"
      , 0 };
    const char gtao_blur_fragment[] = {
#embed "gtao_blur.fragment.wgsl"
      , 0 };
    const char shafts_vertex[] = {
#embed "shafts.vertex.wgsl"
      , 0 };
    const char shafts_fragment[] = {
#embed "shafts.fragment.wgsl"
      , 0 };
    const char boulders_vertex[] = {
#embed "boulders.vertex.wgsl"
      , 0 };
    const char boulders_fragment[] = {
#embed "boulders.fragment.wgsl"
      , 0 };
    const char boulders_shadow_vertex[] = {
#embed "boulders_shadow.vertex.wgsl"
      , 0 };
    const char boulder_cull_compute[] = {
#embed "boulder_cull.compute.wgsl"
      , 0 };
    const char rain_vertex[] = {
#embed "rain.vertex.wgsl"
      , 0 };
    const char rain_fragment[] = {
#embed "rain.fragment.wgsl"
      , 0 };
    const char water_vertex[] = {
#embed "water.vertex.wgsl"
      , 0 };
    const char water_fragment[] = {
#embed "water.fragment.wgsl"
      , 0 };
    const char leaves_vertex[] = {
#embed "leaves.vertex.wgsl"
      , 0 };
    const char leaves_fragment[] = {
#embed "leaves.fragment.wgsl"
      , 0 };
    const char waterfall_vertex[] = {
#embed "waterfall.vertex.wgsl"
      , 0 };
    const char waterfall_fragment[] = {
#embed "waterfall.fragment.wgsl"
      , 0 };
    const char scene_copy_vertex[] = {
#embed "scene_copy.vertex.wgsl"
      , 0 };
    const char scene_copy_fragment[] = {
#embed "scene_copy.fragment.wgsl"
      , 0 };

    StageCode wgsl (const char* code) {
      return { {}, {}, {}, code };
    }
  }

  const WorldShaders& world_shaders_webgpu () {
    static const WorldShaders shaders {
      { wgsl (terrain_vertex), wgsl (terrain_fragment) },
      { wgsl (sky_vertex), wgsl (sky_fragment) },
      { wgsl (uber_vertex), wgsl (uber_fragment) },
      { wgsl (hud_vertex), wgsl (hud_fragment) },
      { wgsl (resolve_vertex), wgsl (resolve_fragment) },
      { wgsl (present_vertex), wgsl (present_fragment) },
      { wgsl (slug_text_vertex), wgsl (slug_text_fragment) },
      { wgsl (forest_vertex), wgsl (forest_fragment) },
      wgsl (forest_cull_compute),
      wgsl (exposure_compute),
      { wgsl (terrain_shadow_vertex), {} },
      { wgsl (forest_shadow_vertex), {} },
      { wgsl (forest_far_shadow_vertex), {} },
      { wgsl (bloom_bright_vertex), wgsl (bloom_bright_fragment) },
      { wgsl (bloom_blur_vertex), wgsl (bloom_blur_fragment) },
      { wgsl (grass_vertex), wgsl (grass_fragment) },
      wgsl (grass_tiles_compute),
      { wgsl (sward_vertex), wgsl (sward_fragment) },
      wgsl (sward_patches_compute),
      { wgsl (gtao_vertex), wgsl (gtao_fragment) },
      { wgsl (gtao_blur_vertex), wgsl (gtao_blur_fragment) },
      { wgsl (shafts_vertex), wgsl (shafts_fragment) },
      { wgsl (boulders_vertex), wgsl (boulders_fragment) },
      { wgsl (boulders_shadow_vertex), {} },
      wgsl (boulder_cull_compute),
      { wgsl (rain_vertex), wgsl (rain_fragment) },
      { wgsl (water_vertex), wgsl (water_fragment) },
      { wgsl (leaves_vertex), wgsl (leaves_fragment) },
      { wgsl (waterfall_vertex), wgsl (waterfall_fragment) },
      { wgsl (scene_copy_vertex), wgsl (scene_copy_fragment) },
    };
    return shaders;
  }
}

#pragma clang diagnostic pop
