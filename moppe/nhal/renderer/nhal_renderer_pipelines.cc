// Making the renderer's pipelines from the world programs' code, once, when
// it starts.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    void NhalRenderer::make_pipelines () {
      RenderPipelineDesc scene;
      scene.color_formats[0] = Format::rgba16_float;
      scene.color_formats[1] = Format::rg16_float;
      scene.color_count = 2;
      scene.depth_format = Format::d32_float;
      scene.cull = Cull::back;

      RenderPipelineDesc terrain = scene;
      terrain.program = &shaders::terrain::program;
      terrain.vertex = m_terrain_code.stage (0);
      terrain.fragment = m_terrain_code.stage (1);
      terrain.topology = Topology::triangle_strip;
      m_terrain = m_device->create_render_pipeline (terrain);

      RenderPipelineDesc ribbon = terrain;
      ribbon.program = &shaders::road_ribbon::program;
      ribbon.vertex = m_road_ribbon_code.stage (0);
      ribbon.fragment = m_road_ribbon_code.stage (1);
      ribbon.label = "road ribbon";
      m_road_ribbon = m_device->create_render_pipeline (ribbon);

      RenderPipelineDesc sky = scene;
      sky.program = &shaders::sky::program;
      sky.vertex = m_sky_code.stage (0);
      sky.fragment = m_sky_code.stage (1);
      sky.cull = Cull::none;
      sky.depth_write = false;
      m_sky = m_device->create_render_pipeline (sky);

      RenderPipelineDesc forest = scene;
      forest.program = &shaders::forest::program;
      forest.vertex = m_forest_code.stage (0);
      forest.fragment = m_forest_code.stage (1);
      forest.cull = Cull::none;
      forest.label = "forest";
      m_forest = m_device->create_render_pipeline (forest);
      m_exposure = m_device->create_compute_pipeline (
        { &shaders::exposure::program, m_exposure_code.code (), "exposure" });
      m_forest_cull =
        m_device->create_compute_pipeline ({ &shaders::forest_cull::program,
                                             m_forest_cull_code.code (),
                                             "forest culling" });

      RenderPipelineDesc caster;
      caster.color_count = 0;
      caster.depth_format = Format::d32_float;
      caster.depth_compare = CompareOp::less_equal;
      caster.depth_bias = 2.0f;
      caster.slope_scaled_depth_bias = 2.0f;
      RenderPipelineDesc terrain_caster = caster;
      terrain_caster.program = &shaders::terrain_shadow::program;
      terrain_caster.vertex = m_terrain_shadow_code.stage (0);
      terrain_caster.topology = Topology::triangle_strip;
      terrain_caster.label = "terrain shadow";
      m_terrain_shadow = m_device->create_render_pipeline (terrain_caster);
      RenderPipelineDesc tree_caster = caster;
      tree_caster.program = &shaders::forest_shadow::program;
      tree_caster.vertex = m_forest_shadow_code.stage (0);
      tree_caster.label = "forest shadow";
      m_forest_shadow = m_device->create_render_pipeline (tree_caster);
      RenderPipelineDesc far_tree_caster = caster;
      far_tree_caster.program = &shaders::forest_far_shadow::program;
      far_tree_caster.vertex = m_forest_far_shadow_code.stage (0);
      far_tree_caster.cull = Cull::none;
      far_tree_caster.label = "far forest shadow";
      m_forest_far_shadow = m_device->create_render_pipeline (far_tree_caster);
      {
        // The far casters' double cone: six faces up to the top (vertex
        // 0) and six down to the base (1) from the ring (2 to 7).
        std::array<std::uint16_t, far_tree_indices> cone {};
        for (std::uint16_t side = 0; side < 6; ++side) {
          const std::uint16_t a = 2 + side, b = 2 + (side + 1) % 6;
          const std::array<std::uint16_t, 6> faces { 0, a, b, 1, b, a };
          std::copy (faces.begin (), faces.end (), cone.begin () + 6 * side);
        }
        m_far_tree_indices = m_device->create_buffer (
          { .size = sizeof cone, .label = "far tree shadow cone" },
          std::as_bytes (std::span (cone)));
      }
      RenderPipelineDesc rock_caster = caster;
      rock_caster.program = &shaders::boulders_shadow::program;
      rock_caster.vertex = m_boulders_shadow_code.stage (0);
      rock_caster.label = "boulder shadow";
      m_boulders_shadow = m_device->create_render_pipeline (rock_caster);
      // Baked meshes are not all closed, so both their faces cast.
      RenderPipelineDesc mesh_caster = caster;
      mesh_caster.program = &shaders::mesh_shadow::program;
      mesh_caster.vertex = m_mesh_shadow_code.stage (0);
      mesh_caster.cull = Cull::none;
      mesh_caster.label = "mesh shadow";
      m_mesh_shadow = m_device->create_render_pipeline (mesh_caster);
      // The sun's two cascades side by side: near on the left, far on
      // the right.
      m_shadow_map = m_device->create_texture ({ 2 * shadow_size,
                                                 shadow_size,
                                                 Format::d32_float,
                                                 usage_depth | usage_sampled,
                                                 1,
                                                 "sun shadow" });

      RenderPipelineDesc rock = scene;
      rock.program = &shaders::boulders::program;
      rock.vertex = m_boulders_code.stage (0);
      rock.fragment = m_boulders_code.stage (1);
      rock.cull = Cull::none;
      rock.label = "boulders";
      m_boulders = m_device->create_render_pipeline (rock);
      m_boulder_cull =
        m_device->create_compute_pipeline ({ &shaders::boulder_cull::program,
                                             m_boulder_cull_code.code (),
                                             "boulder culling" });

      RenderPipelineDesc water = scene;
      water.program = &shaders::water::program;
      water.vertex = m_water_code.stage (0);
      water.fragment = m_water_code.stage (1);
      water.topology = Topology::triangle_strip;
      water.blend[0] = Blend::premultiplied;
      water.depth_write = false;
      water.cull = Cull::none;
      water.label = "water";
      m_water = m_device->create_render_pipeline (water);

      RenderPipelineDesc leaves = scene;
      leaves.program = &shaders::leaves::program;
      leaves.vertex = m_leaves_code.stage (0);
      leaves.fragment = m_leaves_code.stage (1);
      leaves.cull = Cull::none;
      leaves.label = "falling leaves";
      m_leaves = m_device->create_render_pipeline (leaves);

      // Lettering over the surface it lies on: premultiplied by its
      // coverage, tested against the surface's depth but not writing it.
      RenderPipelineDesc lettering = scene;
      lettering.program = &shaders::world_text::program;
      lettering.vertex = m_world_text_code.stage (0);
      lettering.fragment = m_world_text_code.stage (1);
      lettering.blend[0] = Blend::premultiplied;
      lettering.depth_write = false;
      lettering.cull = Cull::none;
      lettering.label = "world text";
      m_world_text = m_device->create_render_pipeline (lettering);
      // What the scene hides of a label: where its surfaces are nearer.
      RenderPipelineDesc behind = lettering;
      behind.depth_compare = CompareOp::less;
      behind.label = "world labels behind";
      m_world_labels_behind = m_device->create_render_pipeline (behind);

      RenderPipelineDesc waterfall = scene;
      waterfall.program = &shaders::waterfall::program;
      waterfall.vertex = m_waterfall_code.stage (0);
      waterfall.fragment = m_waterfall_code.stage (1);
      waterfall.blend[0] = Blend::alpha;
      waterfall.depth_write = false;
      waterfall.cull = Cull::none;
      waterfall.label = "waterfalls";
      m_waterfall = m_device->create_render_pipeline (waterfall);

      RenderPipelineDesc rain = scene;
      rain.program = &shaders::rain::program;
      rain.vertex = m_rain_code.stage (0);
      rain.fragment = m_rain_code.stage (1);
      rain.blend[0] = Blend::alpha;
      rain.depth_write = false;
      rain.cull = Cull::none;
      rain.label = "drizzle";
      m_rain = m_device->create_render_pipeline (rain);

      RenderPipelineDesc grass = scene;
      grass.program = &shaders::grass::program;
      grass.vertex = m_grass_code.stage (0);
      grass.fragment = m_grass_code.stage (1);
      grass.cull = Cull::none;
      grass.label = "grass";
      m_grass_pipeline = m_device->create_render_pipeline (grass);
      RenderPipelineDesc sward = grass;
      sward.program = &shaders::sward::program;
      sward.vertex = m_sward_code.stage (0);
      sward.fragment = m_sward_code.stage (1);
      sward.blend[0] = Blend::alpha;
      sward.depth_write = false;
      sward.label = "sward canopy";
      m_sward_pipeline = m_device->create_render_pipeline (sward);
      m_sward_patches_pipeline =
        m_device->create_compute_pipeline ({ &shaders::sward_patches::program,
                                             m_sward_patches_code.code (),
                                             "sward patches" });
      m_grass_tiles_pipeline =
        m_device->create_compute_pipeline ({ &shaders::grass_tiles::program,
                                             m_grass_tiles_code.code (),
                                             "grass tiles" });

      RenderPipelineDesc resolve;
      resolve.program = &shaders::resolve::program;
      resolve.vertex = m_resolve_code.stage (0);
      resolve.fragment = m_resolve_code.stage (1);
      resolve.color_formats[0] = Format::rgba16_float;
      m_resolve = m_device->create_render_pipeline (resolve);
      RenderPipelineDesc neighbourhood = resolve;
      neighbourhood.program = &shaders::neighbourhood::program;
      neighbourhood.vertex = m_neighbourhood_code.stage (0);
      neighbourhood.fragment = m_neighbourhood_code.stage (1);
      neighbourhood.color_count = 2;
      neighbourhood.color_formats[1] = Format::rgba16_float;
      m_neighbourhood = m_device->create_render_pipeline (neighbourhood);

      RenderPipelineDesc present;
      present.program = &shaders::present::program;
      present.vertex = m_present_code.stage (0);
      present.fragment = m_present_code.stage (1);
      present.color_formats[0] = m_device->surface_format ();
      m_present = m_device->create_render_pipeline (present);

      RenderPipelineDesc bright = present;
      bright.program = &shaders::bloom_bright::program;
      bright.vertex = m_bloom_bright_code.stage (0);
      bright.fragment = m_bloom_bright_code.stage (1);
      bright.color_formats[0] = Format::rgba16_float;
      m_bloom_bright = m_device->create_render_pipeline (bright);
      RenderPipelineDesc blur = bright;
      blur.program = &shaders::bloom_blur::program;
      blur.vertex = m_bloom_blur_code.stage (0);
      blur.fragment = m_bloom_blur_code.stage (1);
      m_bloom_blur = m_device->create_render_pipeline (blur);
      RenderPipelineDesc smear = bright;
      smear.program = &shaders::motion_blur::program;
      smear.vertex = m_motion_blur_code.stage (0);
      smear.fragment = m_motion_blur_code.stage (1);
      m_motion_blur = m_device->create_render_pipeline (smear);

      RenderPipelineDesc underwater = bright;
      underwater.program = &shaders::underwater::program;
      underwater.vertex = m_underwater_code.stage (0);
      underwater.fragment = m_underwater_code.stage (1);
      underwater.label = "underwater";
      m_underwater = m_device->create_render_pipeline (underwater);

      RenderPipelineDesc occlusion = present;
      occlusion.program = &shaders::gtao::program;
      occlusion.vertex = m_gtao_code.stage (0);
      occlusion.fragment = m_gtao_code.stage (1);
      occlusion.color_formats[0] = Format::r16_float;
      m_gtao = m_device->create_render_pipeline (occlusion);
      RenderPipelineDesc occlusion_blur = occlusion;
      occlusion_blur.program = &shaders::gtao_blur::program;
      occlusion_blur.vertex = m_gtao_blur_code.stage (0);
      occlusion_blur.fragment = m_gtao_blur_code.stage (1);
      m_gtao_blur = m_device->create_render_pipeline (occlusion_blur);
      RenderPipelineDesc occlusion_apply = occlusion_blur;
      occlusion_apply.color_formats[0] = Format::rgba16_float;
      occlusion_apply.blend[0] = Blend::premultiplied;
      m_gtao_apply = m_device->create_render_pipeline (occlusion_apply);
      RenderPipelineDesc shafts = present;
      shafts.program = &shaders::shafts::program;
      shafts.vertex = m_shafts_code.stage (0);
      shafts.fragment = m_shafts_code.stage (1);
      shafts.color_formats[0] = Format::rgba16_float;
      shafts.blend[0] = Blend::additive;
      m_shafts_pipeline = m_device->create_render_pipeline (shafts);
      RenderPipelineDesc copy = present;
      copy.program = &shaders::scene_copy::program;
      copy.vertex = m_scene_copy_code.stage (0);
      copy.fragment = m_scene_copy_code.stage (1);
      copy.color_count = 2;
      copy.color_formats[0] = Format::rgba16_float;
      copy.color_formats[1] = Format::r32_float;
      m_scene_copy = m_device->create_render_pipeline (copy);

      RenderPipelineDesc hud = present;
      hud.program = &shaders::hud::program;
      hud.vertex = m_hud_code.stage (0);
      hud.fragment = m_hud_code.stage (1);
      hud.blend[0] = Blend::alpha;
      m_hud = m_device->create_render_pipeline (hud);

      RenderPipelineDesc text = hud;
      text.program = &shaders::slug_text::program;
      text.vertex = m_slug_text_code.stage (0);
      text.fragment = m_slug_text_code.stage (1);
      m_slug_text = m_device->create_render_pipeline (text);
    }
  }
}
