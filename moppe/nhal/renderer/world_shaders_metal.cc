// The world programs' MSL, as luv-shaderc lowered world.lisp into the build
// directory, embedded so the game carries no loose shader files.
#include <moppe/nhal/renderer/nhal_renderer.hh>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"

namespace moppe::nhal {
  namespace {
    // #embed cannot sit in a macro; each stage is spelled out.
    const char terrain_vertex[] = {
#embed "terrain.vertex.metal"
      , 0 };
    const char terrain_fragment[] = {
#embed "terrain.fragment.metal"
      , 0 };
    const char sky_vertex[] = {
#embed "sky.vertex.metal"
      , 0 };
    const char sky_fragment[] = {
#embed "sky.fragment.metal"
      , 0 };
    const char uber_vertex[] = {
#embed "uber.vertex.metal"
      , 0 };
    const char uber_fragment[] = {
#embed "uber.fragment.metal"
      , 0 };
    const char hud_vertex[] = {
#embed "hud.vertex.metal"
      , 0 };
    const char hud_fragment[] = {
#embed "hud.fragment.metal"
      , 0 };
    const char resolve_vertex[] = {
#embed "resolve.vertex.metal"
      , 0 };
    const char resolve_fragment[] = {
#embed "resolve.fragment.metal"
      , 0 };
    const char present_vertex[] = {
#embed "present.vertex.metal"
      , 0 };
    const char present_fragment[] = {
#embed "present.fragment.metal"
      , 0 };
    const char slug_text_vertex[] = {
#embed "slug_text.vertex.metal"
      , 0 };
    const char slug_text_fragment[] = {
#embed "slug_text.fragment.metal"
      , 0 };
    const char forest_vertex[] = {
#embed "forest.vertex.metal"
      , 0 };
    const char forest_fragment[] = {
#embed "forest.fragment.metal"
      , 0 };
    const char forest_cull_compute[] = {
#embed "forest_cull.compute.metal"
      , 0 };
    const char exposure_compute[] = {
#embed "exposure.compute.metal"
      , 0 };
    const char terrain_shadow_vertex[] = {
#embed "terrain_shadow.vertex.metal"
      , 0 };
    const char forest_shadow_vertex[] = {
#embed "forest_shadow.vertex.metal"
      , 0 };
    const char bloom_bright_vertex[] = {
#embed "bloom_bright.vertex.metal"
      , 0 };
    const char bloom_bright_fragment[] = {
#embed "bloom_bright.fragment.metal"
      , 0 };
    const char bloom_blur_vertex[] = {
#embed "bloom_blur.vertex.metal"
      , 0 };
    const char bloom_blur_fragment[] = {
#embed "bloom_blur.fragment.metal"
      , 0 };
    const char grass_vertex[] = {
#embed "grass.vertex.metal"
      , 0 };
    const char grass_fragment[] = {
#embed "grass.fragment.metal"
      , 0 };
    const char grass_tiles_compute[] = {
#embed "grass_tiles.compute.metal"
      , 0 };
    const char sward_vertex[] = {
#embed "sward.vertex.metal"
      , 0 };
    const char sward_fragment[] = {
#embed "sward.fragment.metal"
      , 0 };
    const char sward_patches_compute[] = {
#embed "sward_patches.compute.metal"
      , 0 };
    const char gtao_vertex[] = {
#embed "gtao.vertex.metal"
      , 0 };
    const char gtao_fragment[] = {
#embed "gtao.fragment.metal"
      , 0 };
    const char gtao_blur_vertex[] = {
#embed "gtao_blur.vertex.metal"
      , 0 };
    const char gtao_blur_fragment[] = {
#embed "gtao_blur.fragment.metal"
      , 0 };
    const char shafts_vertex[] = {
#embed "shafts.vertex.metal"
      , 0 };
    const char shafts_fragment[] = {
#embed "shafts.fragment.metal"
      , 0 };
    const char boulders_vertex[] = {
#embed "boulders.vertex.metal"
      , 0 };
    const char boulders_fragment[] = {
#embed "boulders.fragment.metal"
      , 0 };
    const char boulders_shadow_vertex[] = {
#embed "boulders_shadow.vertex.metal"
      , 0 };
    const char boulder_cull_compute[] = {
#embed "boulder_cull.compute.metal"
      , 0 };
  }

  const WorldShaders& world_shaders_metal () {
    static const WorldShaders shaders {
      { { terrain_vertex, {} }, { terrain_fragment, {} } },
      { { sky_vertex, {} }, { sky_fragment, {} } },
      { { uber_vertex, {} }, { uber_fragment, {} } },
      { { hud_vertex, {} }, { hud_fragment, {} } },
      { { resolve_vertex, {} }, { resolve_fragment, {} } },
      { { present_vertex, {} }, { present_fragment, {} } },
      { { slug_text_vertex, {} }, { slug_text_fragment, {} } },
      { { forest_vertex, {} }, { forest_fragment, {} } },
      { forest_cull_compute, {} },
      { exposure_compute, {} },
      { { terrain_shadow_vertex, {} }, {} },
      { { forest_shadow_vertex, {} }, {} },
      { { bloom_bright_vertex, {} }, { bloom_bright_fragment, {} } },
      { { bloom_blur_vertex, {} }, { bloom_blur_fragment, {} } },
      { { grass_vertex, {} }, { grass_fragment, {} } },
      { grass_tiles_compute, {} },
      { { sward_vertex, {} }, { sward_fragment, {} } },
      { sward_patches_compute, {} },
      { { gtao_vertex, {} }, { gtao_fragment, {} } },
      { { gtao_blur_vertex, {} }, { gtao_blur_fragment, {} } },
      { { shafts_vertex, {} }, { shafts_fragment, {} } },
      { { boulders_vertex, {} }, { boulders_fragment, {} } },
      { { boulders_shadow_vertex, {} }, {} },
      { boulder_cull_compute, {} },
    };
    return shaders;
  }
}

#pragma clang diagnostic pop
