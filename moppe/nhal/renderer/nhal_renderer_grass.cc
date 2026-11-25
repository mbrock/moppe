// The undergrowth: the sward canopy's patches and the grass tiles around
// the camera, culled into indirect draws, and the blades drawn from them.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    void NhalRenderer::make_grass_pipelines () {
      const RenderPipelineDesc scene = scene_target ();
      RenderPipelineDesc grass = scene;
      grass.program = &shaders::grass::program;
      grass.vertex = m_shaders.grass[0];
      grass.fragment = m_shaders.grass[1];
      grass.cull = Cull::none;
      grass.label = "grass";
      m_grass.blades = m_device->create_render_pipeline (grass);
      RenderPipelineDesc sward = grass;
      sward.program = &shaders::sward::program;
      sward.vertex = m_shaders.sward[0];
      sward.fragment = m_shaders.sward[1];
      sward.blend[0] = Blend::alpha;
      sward.depth_write = false;
      sward.label = "sward canopy";
      m_grass.sward = m_device->create_render_pipeline (sward);
      m_grass.sward_cells_pipeline =
        m_device->create_compute_pipeline ({ &shaders::sward_cells::program,
                                             m_shaders.sward_cells,
                                             "sward cells" });
      m_grass.sward_patches_pipeline =
        m_device->create_compute_pipeline ({ &shaders::sward_patches::program,
                                             m_shaders.sward_patches,
                                             "sward patches" });
      m_grass.tiles_pipeline =
        m_device->create_compute_pipeline ({ &shaders::grass_tiles::program,
                                             m_shaders.grass_tiles,
                                             "grass tiles" });
    }

    void NhalRenderer::set_sward_texture (render::TexturePtr grain) {
      m_grass.grain = grain;
    }

    NhalRenderer::GrassCullJob
    NhalRenderer::prepare_grass_cull (const Mat4& view_proj,
                                      const FrameParams& params) {
      m_grass.culled = false;
      if (!m_grass.wanted || !m_terrain.have || !m_terrain.landscape ||
          !m_terrain.ground)
        return {};
      constexpr float tile = 0.60f;
      constexpr std::uint32_t shoots = 32, indices_per_shoot = 18;
      const float reach =
        std::max (meters_value (m_grass.params.reach), 1.0f) * 1.7f;
      const std::uint32_t side = std::min (
        std::uint32_t (std::ceil (2.0f * reach / tile)), grass_window_side);
      if (!m_grass.tiles) {
        // A word a tile, and room in every class for the whole window.
        m_grass.tiles = m_device->create_buffer (
          { .size = std::uint64_t (grass_classes) * grass_window_side *
                    grass_window_side * 4,
            .usage = buffer_storage_write,
            .label = "grass tiles" });
        m_grass.arguments = m_device->create_buffer (
          { .size = grass_classes * sizeof (DrawIndexedIndirectArgs),
            .usage = buffer_storage_write | buffer_indirect,
            .label = "grass draw" });
        std::vector<std::uint16_t> indices;
        for (std::uint32_t shoot = 0; shoot < shoots; ++shoot)
          for (std::uint32_t quad = 0; quad < 3; ++quad) {
            const std::uint16_t c = std::uint16_t (shoot * 8 + quad * 2);
            indices.insert (indices.end (),
                            { c,
                              std::uint16_t (c + 1),
                              std::uint16_t (c + 3),
                              c,
                              std::uint16_t (c + 3),
                              std::uint16_t (c + 2) });
          }
        m_grass.indices = m_device->create_buffer (
          { .size = indices.size () * 2, .label = "grass blades" },
          std::as_bytes (std::span (indices)));

        constexpr std::uint32_t sward_cells =
          sward_window_side * sward_window_side;
        m_grass.sward_marks = m_device->create_buffer (
          { .size = std::uint64_t (sward_cells + (sward_cells + 63) / 64) * 4,
            .usage = buffer_storage_write,
            .label = "sward cells" });
        m_grass.sward_patches = m_device->create_buffer (
          { .size = std::uint64_t (sward_window_side) * sward_window_side * 8,
            .usage = buffer_storage_write,
            .label = "sward patches" });
        m_grass.sward_arguments = m_device->create_buffer (
          { .size = sizeof (DrawIndexedIndirectArgs),
            .usage = buffer_storage_write | buffer_indirect,
            .label = "sward draw" });
        std::vector<std::uint16_t> grid;
        for (std::uint16_t z = 0; z < 4; ++z)
          for (std::uint16_t x = 0; x < 4; ++x) {
            const std::uint16_t a = std::uint16_t (z * 5 + x);
            const std::uint16_t b = std::uint16_t (a + 1);
            const std::uint16_t c = std::uint16_t (a + 5);
            const std::uint16_t d = std::uint16_t (c + 1);
            grid.insert (grid.end (), { a, c, b, b, c, d });
          }
        m_grass.sward_indices = m_device->create_buffer (
          { .size = grid.size () * 2, .label = "sward patch" },
          std::as_bytes (std::span (grid)));
      }
      const auto& t = m_terrain.params;
      const Vec3& camera = position_value (params.camera_pos);
      const render::UndergrowthParams& sward_params = m_grass.params;
      const Vec3& bare_centre = position_value (sward_params.bare_centre);
      const Vec3& bare_along = direction_value (sward_params.bare_along);
      const Vec3& yard_centre = position_value (sward_params.yard_centre);
      const Vec3& yard_along = direction_value (sward_params.yard_along);
      shaders::grass::Grass grass {};
      grass.window = { std::floor ((camera[0] - reach) / tile),
                       std::floor ((camera[2] - reach) / tile),
                       float (side),
                       tile };
      grass.lattice = {
        1.0f / t.scale[0], 1.0f / t.scale[2], t.scale[1], float (t.width)
      };
      const float focal =
        0.5f * float (m_scene_height) * std::abs (params.proj.at (1, 1));
      grass.habitat = {
        reach, sward_params.density.numerical_value_in (one), t.tex_scale, focal
      };
      grass.stand_field = stand_field ();
      grass.bare = {
        bare_centre[0], bare_centre[2], bare_along[0], bare_along[2]
      };
      grass.bare_size = { meters_value (sward_params.bare_half_x),
                          meters_value (sward_params.bare_half_z),
                          0.0f,
                          0.0f };
      grass.yard = {
        yard_centre[0], yard_centre[2], yard_along[0], yard_along[2]
      };
      grass.yard_size = { meters_value (sward_params.yard_half_x),
                          meters_value (sward_params.yard_half_z),
                          meters_value (sward_params.yard_margin),
                          0.0f };
      m_grass.block = grass;

      // The canopy's reach follows the projected height of the whole
      // sward, so looking down from the glider asks the same image-space
      // question as riding.
      constexpr float patch = 16.0f;
      const float canopy_reach =
        std::clamp (0.42f * focal / 0.28f, 180.0f, 1200.0f);
      const std::uint32_t patches =
        std::min (std::uint32_t (std::ceil (2.0f * canopy_reach / patch)),
                  sward_window_side);
      auto row = [&] (int r) {
        return std::sqrt (
          view_proj.element (r) * view_proj.element (r) +
          view_proj.element (4 + r) * view_proj.element (4 + r) +
          view_proj.element (8 + r) * view_proj.element (8 + r));
      };
      shaders::grass::Grass sward = grass;
      sward.window = { std::floor ((camera[0] - canopy_reach) / patch),
                       std::floor ((camera[2] - canopy_reach) / patch),
                       float (patches),
                       patch };
      sward.habitat[0] = canopy_reach;
      sward.interaction = { row (0), row (1), 0, 0 };
      m_grass.sward_block = sward;

      std::array<DrawIndexedIndirectArgs, grass_classes> reset {};
      for (std::uint32_t c = 0; c < grass_classes; ++c)
        reset[c] = { (1u << c) * indices_per_shoot, 0, 0, 0, 0 };
      constexpr Stage cull_wait = Stage::vertex | Stage::compute | Stage::copy;
      m_device->copy_to_buffer (
        m_grass.arguments,
        0,
        m_device->upload (std::span<const DrawIndexedIndirectArgs> (reset)),
        cull_wait);
      const DrawIndexedIndirectArgs sward_reset { 96, 0, 0, 0, 0 };
      m_device->copy_to_buffer (
        m_grass.sward_arguments,
        0,
        m_device->upload (
          std::span<const DrawIndexedIndirectArgs> (&sward_reset, 1)),
        cull_wait);
      m_grass.culled = true;
      return { (side * side + 63) / 64, (patches * patches + 63) / 64, true };
    }

    void NhalRenderer::dispatch_grass_wave0 (const GrassCullJob& job) {
      m_device->set_pipeline (m_grass.tiles_pipeline);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, m_grass.block);
      m_device->set_buffer (2, m_grass.tiles);
      m_device->set_buffer (3, m_grass.arguments);
      m_device->set_uniforms (4, terrain_block ());
      bind_roads (5);
      m_device->set_texture (0, m_terrain.heights);
      m_device->set_texture (1, m_terrain.normals);
      m_device->set_texture (6, m_terrain.landscape);
      m_device->set_texture (7, m_terrain.ground);
      m_device->set_texture (5, m_terrain.plants ? m_terrain.plants : m_clear);
      m_device->set_texture (
        4, m_terrain.more_plants ? m_terrain.more_plants : m_clear);
      m_device->set_texture (11, m_terrain.scars ? m_terrain.scars : m_clear);
      m_device->set_texture (12,
                             m_terrain.bedrock ? m_terrain.bedrock : m_clear);
      bind_stand ();
      m_device->dispatch (job.tile_groups);

      m_device->set_pipeline (m_grass.sward_cells_pipeline);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, m_grass.sward_block);
      m_device->set_buffer (2, m_grass.sward_marks);
      m_device->set_buffer (3, m_grass.sward_arguments);
      m_device->set_texture (0, m_terrain.heights);
      m_device->dispatch (job.sward_groups);
    }

    void NhalRenderer::dispatch_grass_wave1 (const GrassCullJob& job) {
      m_device->set_pipeline (m_grass.sward_patches_pipeline);
      m_device->set_uniforms (1, m_grass.sward_block);
      m_device->set_buffer (2, m_grass.sward_marks);
      m_device->set_buffer (3, m_grass.sward_patches);
      m_device->dispatch (job.sward_groups);
    }

    void
    NhalRenderer::draw_undergrowth (const render::UndergrowthParams& params) {
      // This frame draws the tiles begin_frame chose with the last
      // parameters; the next frame's cull takes these.
      m_grass.params = params;
      m_grass.wanted = true;
      if (!m_grass.culled)
        return;
      open_scene ();
      // The canopy first: translucent, it blends over the ground it sits
      // on, and the blades it hands over to stand in it.
      draw_sward ();
      draw_blades (params);
    }

    void NhalRenderer::draw_sward () {
      m_device->set_pipeline (m_grass.sward);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, m_grass.sward_block);
      m_device->set_buffer (2, m_grass.sward_patches);
      m_device->set_uniforms (4, terrain_block ());
      bind_roads (5);
      m_device->set_texture (2, texture_or_white (m_grass.grain));
      bind_ground_textures ();
      m_device->draw_indexed_indirect (
        m_grass.sward_indices, IndexType::uint16, m_grass.sward_arguments);
    }

    void NhalRenderer::draw_blades (const render::UndergrowthParams& params) {
      shaders::grass::Grass grass = m_grass.block;
      grass.interaction = lanes (position_value (params.interaction_position),
                                 meters_value (params.interaction_radius));
      m_device->set_pipeline (m_grass.blades);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, grass);
      m_device->set_buffer (2, m_grass.tiles);
      bind_ground_textures ();
      const float tiles = grass.window[2] * grass.window[2];
      for (std::uint32_t c = 0; c < grass_classes; ++c) {
        shaders::grass::Draw draw {};
        draw.class_ = { float (c) * tiles, 0, 0, 0 };
        m_device->set_uniforms (3, draw);
        m_device->draw_indexed_indirect (m_grass.indices,
                                         IndexType::uint16,
                                         m_grass.arguments,
                                         c * sizeof (DrawIndexedIndirectArgs));
      }
    }
  }
}
