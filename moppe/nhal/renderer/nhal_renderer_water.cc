// The water: its levels, current, and coastal surf on the terrain grid,
// the sea's waves, the opaque scene copied out for it to read, the water
// sheet over the terrain's wet chunks, and the waterfalls' curtains.

#include <moppe/nhal/renderer/field_texture.hh>
#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    void NhalRenderer::make_water_pipelines () {
      const RenderPipelineDesc scene = scene_target ();
      RenderPipelineDesc water = scene;
      water.program = &shaders::water::program;
      water.vertex = m_shaders.water[0];
      water.fragment = m_shaders.water[1];
      water.topology = Topology::triangle_strip;
      water.blend[0] = Blend::premultiplied;
      water.depth_write = false;
      water.cull = Cull::none;
      water.label = "water";
      m_water.draw = m_device->create_render_pipeline (water);

      RenderPipelineDesc waterfall = scene;
      waterfall.program = &shaders::waterfall::program;
      waterfall.vertex = m_shaders.waterfall[0];
      waterfall.fragment = m_shaders.waterfall[1];
      waterfall.blend[0] = Blend::alpha;
      waterfall.depth_write = false;
      waterfall.cull = Cull::none;
      waterfall.label = "waterfalls";
      m_water.waterfall = m_device->create_render_pipeline (waterfall);

      RenderPipelineDesc copy;
      copy.program = &shaders::scene_copy::program;
      copy.vertex = m_shaders.scene_copy[0];
      copy.fragment = m_shaders.scene_copy[1];
      copy.color_count = 2;
      copy.color_formats[0] = Format::rgba16_float;
      copy.color_formats[1] = Format::r32_float;
      m_water.scene_copy = m_device->create_render_pipeline (copy);
    }

    void NhalRenderer::set_ocean (const render::OceanSetup& setup,
                                  const render::TexturePixels& levels) {
      if (m_water.levels)
        m_device->destroy (m_water.levels);
      m_water.levels = {};
      m_water.wet_patches = setup.wet_patches;
      if (levels.empty () || levels.format () != render::PixelFormat::rg32f)
        return;
      m_water.levels =
        m_device->create_texture ({ std::uint32_t (levels.width ()),
                                    std::uint32_t (levels.height ()),
                                    Format::rg32_float,
                                    usage_sampled,
                                    1,
                                    "water levels" });
      std::vector<std::byte> bytes (levels.byte_size ());
      levels.write_into (bytes.data ());
      m_device->write_texture (m_water.levels, bytes);
    }

    void NhalRenderer::set_water_flow (const render::TexturePixels& flow) {
      if (m_water.flow)
        m_device->destroy (m_water.flow);
      m_water.flow = {};
      if (flow.empty () || flow.format () != render::PixelFormat::rgba16f)
        return;
      m_water.flow =
        field_texture (*m_device, flow, Format::rgba16_float, "water flow");
    }

    void NhalRenderer::set_shore_surf (const render::TexturePixels& surf) {
      if (m_water.shore_surf)
        m_device->destroy (m_water.shore_surf);
      m_water.shore_surf = {};
      m_water.shore_size = {};
      if (surf.empty () || surf.format () != render::PixelFormat::rgba16f)
        return;
      m_water.shore_size = { float (surf.width ()), float (surf.height ()) };
      m_water.shore_surf =
        m_device->create_texture ({ std::uint32_t (surf.width ()),
                                    std::uint32_t (surf.height ()),
                                    Format::rgba16_float,
                                    usage_sampled,
                                    1,
                                    "coastal surf" });
      std::vector<std::byte> bytes (surf.byte_size ());
      surf.write_into (bytes.data ());
      m_device->write_texture (m_water.shore_surf, bytes);
    }

    shaders::water::Sea NhalRenderer::sea_block (double time) {
      using namespace terrain;
      shaders::water::Sea sea {};
      std::array<float, 4>* lanes[][2] = {
        { &sea.sea_train_0, &sea.sea_pace_0 },
        { &sea.sea_train_1, &sea.sea_pace_1 },
        { &sea.sea_train_2, &sea.sea_pace_2 },
        { &sea.sea_train_3, &sea.sea_pace_3 },
        { &sea.sea_train_4, &sea.sea_pace_4 },
        { &sea.sea_train_5, &sea.sea_pace_5 },
      };
      static_assert (std::size (lanes) == sea_wave_trains.size ());
      for (std::size_t i = 0; i < sea_wave_trains.size (); ++i) {
        const SeaWaveTrain& t = sea_wave_trains[i];
        const float heading = radians_value (sea_wind_heading + t.heading);
        const float k = per_metre_value (sea_wave_number (t));
        const float amplitude = meters_value (t.amplitude);
        *lanes[i][0] = { k * std::cos (heading),
                         k * std::sin (heading),
                         amplitude,
                         radians_value (sea_wave_phase (t, time)) };
        *lanes[i][1] = { per_second_value (sea_wave_frequency (t)),
                         amplitude * sea_wave_lean.numerical_value_in (one),
                         meters_value (t.wavelength),
                         k };
      }
      return sea;
    }

    void NhalRenderer::copy_beneath_water () {
      if (!m_scene_begun)
        open_scene ();
      if (m_scene_open) {
        m_device->end_render_pass ();
        m_scene_open = false;
      }
      RenderPassDesc pass;
      pass.label = "scene beneath water";
      pass.color_count = 2;
      pass.colors[0] = { m_water.beneath, Load::discard, Store::store };
      pass.colors[1] = { m_water.beneath_depth, Load::discard, Store::store };
      m_device->begin_render_pass (pass);
      m_device->set_pipeline (m_water.scene_copy);
      m_device->set_texture (0, m_scene_color);
      m_device->set_texture (1, m_scene_depth);
      m_device->draw (3);
      m_device->end_render_pass ();
      open_scene ();
    }

    void NhalRenderer::collect_water_patches () {
      constexpr int patch = render::terrain_patch_cells;
      const int across = m_terrain.params.width / patch;
      const int down = m_terrain.params.height / patch;
      const bool masked =
        across > 0 && down > 0 &&
        m_water.wet_patches.size () == std::size_t (across) * down;
      m_water.patches.clear ();
      m_water.batches.clear ();
      for (const ChunkDraw& c : m_terrain.chunks) {
        const int lod = std::clamp (int (c.lod), 0, lod_count - 1);
        for (int z = c.z0; z < c.z0 + c.cells; z += patch)
          for (int x = c.x0; x < c.x0 + c.cells; x += patch) {
            if (masked &&
                !m_water.wet_patches[std::size_t (z / patch % down) * across +
                                     x / patch % across])
              continue;
            // Keep the terrain list's order across translucent draws.
            if (m_water.batches.empty () || m_water.batches.back ().lod != lod)
              m_water.batches.push_back ({ lod, m_water.patches.size () });
            m_water.patches.push_back (
              { float (x), float (z), c.offset_x, c.offset_z });
          }
      }
    }

    void NhalRenderer::draw_ocean (const render::OceanParams& params) {
      if (!m_terrain.have || !m_water.levels || m_scene_closed)
        return;
      // A view of dry land needs neither the copy nor the pass's break.
      collect_water_patches ();
      if (m_water.patches.empty ())
        return;
      copy_beneath_water ();
      m_device->set_pipeline (m_water.draw);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, terrain_block ());
      m_device->set_uniforms (3, sea_block (seconds_value (params.time)));
      m_device->set_texture (0, m_terrain.heights);
      m_device->set_texture (8, m_sun_shadow.map);
      m_device->set_texture (11, m_water.levels);
      m_device->set_texture (12, m_water.flow ? m_water.flow : m_water.still);
      m_device->set_texture (13, m_water.beneath);
      m_device->set_texture (14, m_water.beneath_depth);
      m_device->set_texture (
        15, m_water.shore_surf ? m_water.shore_surf : m_water.still);
      const auto patches =
        std::span<const std::array<float, 4>> (m_water.patches);
      for (std::size_t i = 0; i < m_water.batches.size (); ++i) {
        const auto [lod, first] = m_water.batches[i];
        const auto end = i + 1 < m_water.batches.size ()
                           ? m_water.batches[i + 1].first
                           : patches.size ();
        shaders::water::Chunk chunk {};
        chunk.placement = { 0, 0, lod_step[lod], float (patch_verts (lod)) };
        m_device->set_uniforms (2, chunk);
        m_device->set_buffer (
          4, m_device->upload (patches.subspan (first, end - first)));
        m_device->draw_indexed (m_terrain.patch_indices[lod],
                                IndexType::uint32,
                                m_terrain.patch_index_count[lod],
                                std::uint32_t (end - first));
      }
    }

    void NhalRenderer::draw_waterfalls (const render::Mesh& mesh,
                                        const Mat4& model) {
      const auto& m = static_cast<const NhalMesh&> (mesh);
      if (!m.vertices || m_scene_closed)
        return;
      open_scene ();
      draw_runs ({}, &m, m.runs, model, model, m_water.waterfall);
    }
  }
}
