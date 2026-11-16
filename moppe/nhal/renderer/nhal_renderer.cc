// The renderer's construction and the resources the game hands it: textures,
// meshes, glyphs, world text, the terrain and its materials, the forest, the
// boulders, the roads, and the water's fields. The class is in
// nhal_renderer_impl.hh; the frame, the scene's drawing, and the pipelines
// are in the nhal_renderer_*.cc files beside this one.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    NhalRenderer::NhalRenderer (std::unique_ptr<Device> device,
                                const WorldShaders& shaders,
                                float scale)
        : m_device (std::move (device)), m_scale (scale),
          m_first_vertex_counts (m_device->info ().indices_count_from_first) {
      m_terrain_code.keep (shaders.terrain);
      m_road_ribbon_code.keep (shaders.road_ribbon);
      m_sky_code.keep (shaders.sky);
      m_uber_code.keep (shaders.uber);
      m_hud_code.keep (shaders.hud);
      m_resolve_code.keep (shaders.resolve);
      m_present_code.keep (shaders.present);
      m_grass_code.keep (shaders.grass);
      m_sward_code.keep (shaders.sward);
      m_sward_patches_code.keep (shaders.sward_patches);
      m_grass_tiles_code.keep (shaders.grass_tiles);
      m_bloom_bright_code.keep (shaders.bloom_bright);
      m_bloom_blur_code.keep (shaders.bloom_blur);
      m_motion_blur_code.keep (shaders.motion_blur);
      m_world_text_code.keep (shaders.world_text);
      m_underwater_code.keep (shaders.underwater);
      m_gtao_code.keep (shaders.gtao);
      m_gtao_blur_code.keep (shaders.gtao_blur);
      m_shafts_code.keep (shaders.shafts);
      m_forest_code.keep (shaders.forest);
      m_rain_code.keep (shaders.rain);
      m_water_code.keep (shaders.water);
      m_leaves_code.keep (shaders.leaves);
      m_waterfall_code.keep (shaders.waterfall);
      m_scene_copy_code.keep (shaders.scene_copy);
      m_neighbourhood_code.keep (shaders.neighbourhood);
      m_boulders_code.keep (shaders.boulders);
      m_boulders_shadow_code.keep (shaders.boulders_shadow);
      m_mesh_shadow_code.keep (shaders.mesh_shadow);
      m_boulder_cull_code.keep (shaders.boulder_cull);
      m_terrain_shadow_code.keep (shaders.terrain_shadow);
      m_forest_shadow_code.keep (shaders.forest_shadow);
      m_forest_far_shadow_code.keep (shaders.forest_far_shadow);
      m_forest_cull_code.keep (shaders.forest_cull);
      m_exposure_code.keep (shaders.exposure);
      m_slug_text_code.keep (shaders.slug_text);
      make_pipelines ();
      const float neutral = 1.0f;
      m_exposure_value = m_device->create_buffer (
        { .size = 4, .usage = buffer_storage_write, .label = "exposure" },
        std::as_bytes (std::span (&neutral, 1)));
      const std::uint8_t white[4] = { 255, 255, 255, 255 };
      m_white = m_device->create_texture (
        { 1, 1, Format::rgba8_unorm, usage_sampled, 1, "white" });
      m_device->write_texture (m_white, std::as_bytes (std::span (white)));
      const std::uint8_t black[4] = { 0, 0, 0, 255 };
      m_black = m_device->create_texture (
        { 1, 1, Format::rgba8_unorm, usage_sampled, 1, "black" });
      m_device->write_texture (m_black, std::as_bytes (std::span (black)));
      const std::uint8_t clear[4] = { 0, 0, 0, 0 };
      m_clear = m_device->create_texture (
        { 1, 1, Format::rgba8_unorm, usage_sampled, 1, "clear" });
      m_device->write_texture (m_clear, std::as_bytes (std::span (clear)));
      const render::GroundDetail detail = render::make_ground_detail ();
      m_ground_detail = m_device->create_texture ({ render::GroundDetail::size,
                                                    render::GroundDetail::size,
                                                    Format::rgba8_unorm,
                                                    usage_sampled,
                                                    1,
                                                    "ground detail" });
      m_device->write_texture (m_ground_detail,
                               std::as_bytes (std::span (detail.pixels)));
      // No current at all, for water drawn before (or without) a flow.
      const std::uint16_t still[4] = { 0, 0, 0, 0 };
      m_still_water = m_device->create_texture (
        { 1, 1, Format::rgba16_float, usage_sampled, 1, "still water" });
      m_device->write_texture (m_still_water,
                               std::as_bytes (std::span (still)));
      std::cerr << "moppe: NHAL renderer on " << m_device->info ().backend
                << " (" << m_device->info ().adapter << ")" << std::endl;
    }

    NhalRenderer::~NhalRenderer () {
      m_device->wait_idle ();
    }

    render::TexturePtr
    NhalRenderer::create_texture (const render::TextureDesc& desc,
                                  const void* pixels) {
      auto texture = std::make_shared<NhalTexture> ();
      texture->device = m_device.get ();
      texture->width = desc.width;
      texture->height = desc.height;
      texture->handle = m_device->create_texture ({ std::uint32_t (desc.width),
                                                    std::uint32_t (desc.height),
                                                    Format::rgba8_unorm_srgb,
                                                    usage_sampled,
                                                    1,
                                                    "game texture" });
      const std::size_t texels = std::size_t (desc.width) * desc.height;
      std::vector<std::uint8_t> rgba (texels * 4, 255);
      const auto* source = static_cast<const std::uint8_t*> (pixels);
      if (source && desc.format == render::TextureFormat::RGB8) {
        for (std::size_t i = 0; i < texels; ++i)
          std::memcpy (&rgba[i * 4], &source[i * 3], 3);
      } else if (source) {
        std::memcpy (rgba.data (), source, rgba.size ());
      }
      m_device->write_texture (texture->handle,
                               std::as_bytes (std::span (rgba)));
      return texture;
    }

    render::MeshPtr NhalRenderer::create_mesh (const DrawList& recorded) {
      auto mesh = std::make_shared<NhalMesh> ();
      mesh->device = m_device.get ();
      const auto& vertices = recorded.vertices ();
      if (!vertices.empty ())
        mesh->vertices = m_device->create_buffer (
          { .size = vertices.size () * sizeof (render::Vertex),
            .label = "mesh" },
          std::as_bytes (std::span (vertices)));
      mesh->runs = recorded.runs ();
      return mesh;
    }

    render::GlyphSetPtr
    NhalRenderer::create_glyph_set (const render::SlugGlyphData& data) {
      if (data.curves.empty () || data.bands.empty ())
        return nullptr;
      auto set = std::make_shared<NhalGlyphSet> ();
      set->device = m_device.get ();
      set->curves = m_device->create_buffer (
        { .size = data.curves.size () * sizeof (render::SlugCurveTexel),
          .label = "slug curves" },
        std::as_bytes (std::span (data.curves)));
      set->bands = m_device->create_buffer (
        { .size = data.bands.size () * sizeof (std::uint32_t),
          .label = "slug bands" },
        std::as_bytes (std::span (data.bands)));
      return set;
    }

    render::WorldTextPtr
    NhalRenderer::create_world_text (const render::TextList& text) {
      auto placed = std::make_shared<NhalWorldText> ();
      placed->device = m_device.get ();
      const auto& quads = text.quads ();
      for (const render::TextList::Run& run : text.runs ()) {
        if (!run.glyphs || !run.count)
          continue;
        placed->runs.push_back (
          { run.glyphs,
            m_device->create_buffer (
              { .size = run.count * sizeof (render::GlyphQuad),
                .label = "world glyphs" },
              std::as_bytes (std::span (quads).subspan (run.first, run.count))),
            run.count });
      }
      return placed;
    }

    void NhalRenderer::set_terrain (
      const render::TerrainParams& params,
      std::span<const terrain::SurfaceElevation> heights,
      std::span<const terrain::TerrainNormal> normals) {
      m_terrain_params = params;
      const std::uint32_t w = params.width, h = params.height;
      for (Texture* t : { &m_heights, &m_normals })
        if (*t)
          m_device->destroy (*t);
      m_heights = m_device->create_texture (
        { w, h, Format::r32_float, usage_sampled, 1, "terrain heights" });
      static_assert (sizeof (terrain::SurfaceElevation) == sizeof (float));
      m_device->write_texture (m_heights, std::as_bytes (heights));
      // Normals: x and z as RG16 snorm; the shader rebuilds y.
      m_normals = m_device->create_texture (
        { w, h, Format::rg16_snorm, usage_sampled, 1, "terrain normals" });
      std::vector<std::int16_t> packed (normals.size () * 2);
      for (std::size_t i = 0; i < normals.size (); ++i) {
        const Vec3 n = normals[i].numerical_value_in (mp_units::one);
        packed[i * 2] =
          std::int16_t (std::clamp (n[0], -1.0f, 1.0f) * 32767.0f);
        packed[i * 2 + 1] =
          std::int16_t (std::clamp (n[2], -1.0f, 1.0f) * 32767.0f);
      }
      m_device->write_texture (m_normals, std::as_bytes (std::span (packed)));

      // Strip index templates per level, whole chunks and patches, with
      // the all-ones restart between rows.
      if (!m_chunk_indices[0]) {
        auto strips = [&] (int verts, const char* label) {
          std::vector<std::uint32_t> indices;
          for (int row = 0; row + 1 < verts; ++row) {
            for (int x = 0; x < verts; ++x) {
              indices.push_back (std::uint32_t (row * verts + x));
              indices.push_back (std::uint32_t ((row + 1) * verts + x));
            }
            indices.push_back (0xffffffffu);
          }
          const Buffer buffer = m_device->create_buffer (
            { .size = indices.size () * 4, .label = label },
            std::as_bytes (std::span (indices)));
          return std::pair (buffer, std::uint32_t (indices.size ()));
        };
        for (int lod = 0; lod < lod_count; ++lod) {
          std::tie (m_chunk_indices[lod], m_chunk_index_count[lod]) =
            strips (lod_verts[lod], "terrain chunk strips");
          std::tie (m_patch_indices[lod], m_patch_index_count[lod]) =
            strips (patch_verts (lod), "terrain patch strips");
        }
      }
      m_have_terrain = true;
    }

    void NhalRenderer::set_sward_texture (render::TexturePtr grain) {
      m_grass = grain;
    }

    void NhalRenderer::set_ocean (const render::OceanSetup& setup,
                                  const render::TexturePixels& levels) {
      if (m_water_levels)
        m_device->destroy (m_water_levels);
      m_water_levels = {};
      m_wet_patches = setup.wet_patches;
      if (levels.empty () || levels.format () != render::PixelFormat::rg32f)
        return;
      m_water_levels =
        m_device->create_texture ({ std::uint32_t (levels.width ()),
                                    std::uint32_t (levels.height ()),
                                    Format::rg32_float,
                                    usage_sampled,
                                    1,
                                    "water levels" });
      std::vector<std::byte> bytes (levels.byte_size ());
      levels.write_into (bytes.data ());
      m_device->write_texture (m_water_levels, bytes);
    }

    void NhalRenderer::set_water_flow (const render::TexturePixels& flow) {
      if (m_water_flow)
        m_device->destroy (m_water_flow);
      m_water_flow = {};
      if (flow.empty () || flow.format () != render::PixelFormat::rgba16f)
        return;
      m_water_flow = m_device->create_texture ({ std::uint32_t (flow.width ()),
                                                 std::uint32_t (flow.height ()),
                                                 Format::rgba16_float,
                                                 usage_sampled,
                                                 1,
                                                 "water flow" });
      std::vector<std::byte> bytes (flow.byte_size ());
      flow.write_into (bytes.data ());
      m_device->write_texture (m_water_flow, bytes);
    }

    void NhalRenderer::set_shore_surf (const render::TexturePixels& surf) {
      if (m_shore_surf)
        m_device->destroy (m_shore_surf);
      m_shore_surf = {};
      m_shore_size = {};
      if (surf.empty () || surf.format () != render::PixelFormat::rgba16f)
        return;
      m_shore_size = { float (surf.width ()), float (surf.height ()) };
      m_shore_surf = m_device->create_texture ({ std::uint32_t (surf.width ()),
                                                 std::uint32_t (surf.height ()),
                                                 Format::rgba16_float,
                                                 usage_sampled,
                                                 1,
                                                 "coastal surf" });
      std::vector<std::byte> bytes (surf.byte_size ());
      surf.write_into (bytes.data ());
      m_device->write_texture (m_shore_surf, bytes);
    }

    void NhalRenderer::set_forest (
      const render::ForestSetup& setup,
      std::span<const render::ForestInstance> instances) {
      for (Buffer* b : { &m_trees,
                         &m_tree_candidates,
                         &m_tree_arguments,
                         &m_shadow_candidates[0],
                         &m_shadow_arguments[0],
                         &m_shadow_candidates[1],
                         &m_shadow_arguments[1] })
        if (*b)
          m_device->destroy (*b), *b = {};
      m_tree_count = std::uint32_t (instances.size ());
      if (instances.empty ())
        return;
      const Vec3 forest_period = extent_value (setup.period);
      const std::vector<float> closure =
        spread_stand (setup, instances, forest_period);
      // Four rows a tree. The stand closure that decides where a crown
      // starts is read from the splatted canopy over 24 metres.
      std::vector<std::array<float, 4>> rows;
      rows.reserve (instances.size () * 4);
      for (const render::ForestInstance& tree : instances) {
        const Vec3 root = position_value (tree.root);
        const Vec3 up = tree.ground_normal.numerical_value_in (mp_units::one);
        const float cover =
          tree.canopy_cover.numerical_value_in (mp_units::one);
        auto bits = [] (std::uint32_t u) { return std::bit_cast<float> (u); };
        rows.push_back (
          { root[0], root[1], root[2], tree.height.numerical_value_in (u::m) });
        rows.push_back (
          { up[0], up[1], up[2], tree.crown_radius.numerical_value_in (u::m) });
        rows.push_back ({ cover,
                          tree.moisture.numerical_value_in (mp_units::one),
                          closure.empty () ? cover : closure[rows.size () / 4],
                          tree.autumn.numerical_value_in (mp_units::one) });
        rows.push_back ({ bits (tree.seed),
                          bits (std::uint32_t (tree.species)),
                          bits (std::uint32_t (tree.age)),
                          0 });
      }
      m_trees = m_device->create_buffer (
        { .size = rows.size () * 16, .label = "forest records" },
        std::as_bytes (std::span (rows)));
      // Every class can hold every tree, so the cull never overflows.
      m_tree_candidates = m_device->create_buffer (
        { .size = std::uint64_t (forest_classes) * m_tree_count * 8,
          .usage = buffer_storage_write,
          .label = "forest candidates" });
      m_tree_arguments = m_device->create_buffer (
        { .size = forest_classes * sizeof (DrawIndexedIndirectArgs),
          .usage = buffer_storage_write | buffer_indirect,
          .label = "forest draws" });
      for (std::size_t cascade = 0; cascade < 2; ++cascade) {
        m_shadow_candidates[cascade] = m_device->create_buffer (
          { .size = std::uint64_t (forest_species) * m_tree_count * 8,
            .usage = buffer_storage_write,
            .label = "shadow tree candidates" });
        m_shadow_arguments[cascade] = m_device->create_buffer (
          { .size = forest_species * sizeof (DrawIndexedIndirectArgs),
            .usage = buffer_storage_write | buffer_indirect,
            .label = "shadow tree draws" });
      }
      const Vec3 period = extent_value (setup.period);
      m_tree_period[0] = period[0];
      m_tree_period[1] = period[2];

      if (!m_tree_indices) {
        std::vector<std::uint16_t> indices;
        for (std::uint32_t c = 0; c < forest_classes; ++c) {
          m_tree_class_first[c] = std::uint32_t (indices.size ());
          TreeTopology (c / forest_tiers, c % forest_tiers).triangles (indices);
          m_tree_class_count[c] =
            std::uint32_t (indices.size ()) - m_tree_class_first[c];
        }
        m_tree_indices = m_device->create_buffer (
          { .size = indices.size () * 2, .label = "forest topology" },
          std::as_bytes (std::span (indices)));
      }
    }

    void NhalRenderer::set_roads (const RoadSetup& setup) {
      for (Buffer* b : { &m_road_rows, &m_road_index })
        if (*b)
          m_device->destroy (*b), *b = {};
      m_road_setup = setup;
      m_road_setup.segment_rows = {};
      m_road_setup.index = {};
      m_road_segments.assign (setup.segment_rows.begin (),
                              setup.segment_rows.end ());
      if (setup.segment_rows.empty () || setup.index.empty () ||
          setup.buckets_x <= 0 || setup.buckets_z <= 0)
        return;
      m_road_rows = m_device->create_buffer (
        { .size = setup.segment_rows.size_bytes (), .label = "road segments" },
        std::as_bytes (setup.segment_rows));
      m_road_index = m_device->create_buffer (
        { .size = setup.index.size_bytes (), .label = "road buckets" },
        std::as_bytes (setup.index));
    }

    void NhalRenderer::bind_roads (int first) {
      if (!m_no_roads) {
        const std::array<float, 8> none {};
        m_no_roads = m_device->create_buffer (
          { .size = sizeof (none), .label = "no roads" },
          std::as_bytes (std::span (none)));
      }
      m_device->set_buffer (first, m_road_rows ? m_road_rows : m_no_roads);
      m_device->set_buffer (first + 1,
                            m_road_index ? m_road_index : m_no_roads);
    }

    void NhalRenderer::set_boulders (
      const render::BoulderSetup& setup,
      std::span<const render::BoulderInstance> instances) {
      for (Buffer* b : { &m_rocks,
                         &m_rock_candidates,
                         &m_rock_arguments,
                         &m_rock_shadow_candidates,
                         &m_rock_shadow_arguments })
        if (*b)
          m_device->destroy (*b), *b = {};
      std::vector<std::array<float, 4>> rows;
      rows.reserve (instances.size () * 3);
      for (const render::BoulderInstance& rock : instances) {
        const float radius = rock.radius.numerical_value_in (u::m);
        if (!(radius > 0.0f))
          continue;
        const Vec3 centre = position_value (rock.centre);
        const Vec3 up = rock.ground_normal.numerical_value_in (mp_units::one);
        rows.push_back ({ centre[0], centre[1], centre[2], radius });
        rows.push_back ({ up[0],
                          up[1],
                          up[2],
                          rock.moisture.numerical_value_in (mp_units::one) });
        rows.push_back ({ std::bit_cast<float> (rock.seed), 0, 0, 0 });
      }
      m_rock_count = std::uint32_t (rows.size () / 3);
      if (!m_rock_count)
        return;
      m_rocks = m_device->create_buffer (
        { .size = rows.size () * 16, .label = "boulder records" },
        std::as_bytes (std::span (rows)));
      auto candidates = [&] (std::uint32_t classes, const char* label) {
        return m_device->create_buffer (
          { .size = std::uint64_t (classes) * m_rock_count * 8,
            .usage = buffer_storage_write,
            .label = label });
      };
      auto arguments = [&] (const char* label) {
        return m_device->create_buffer (
          { .size = 2 * sizeof (DrawIndexedIndirectArgs),
            .usage = buffer_storage_write | buffer_indirect,
            .label = label });
      };
      m_rock_candidates = candidates (2, "boulder candidates");
      m_rock_arguments = arguments ("boulder draws");
      m_rock_shadow_candidates = candidates (1, "shadow boulder candidates");
      m_rock_shadow_arguments = arguments ("shadow boulder draws");
      const Vec3 period = extent_value (setup.period);
      m_rock_period[0] = period[0];
      m_rock_period[1] = period[2];

      if (!m_rock_indices) {
        std::vector<std::array<float, 4>> corners;
        for (const auto& c : icosahedron)
          corners.push_back ({ c[0], c[1], c[2], 0 });
        m_rock_corners = m_device->create_buffer (
          { .size = corners.size () * 16, .label = "icosahedron" },
          std::as_bytes (std::span (corners)));
        m_rock_faces = m_device->create_buffer (
          { .size = sizeof icosahedron_faces, .label = "icosahedron faces" },
          std::as_bytes (std::span (icosahedron_faces)));
        // Coarse: the faces themselves. Fine: four triangles a face, over
        // its corners (slots 0-2) and edge midpoints (slots 3-5).
        std::vector<std::uint16_t> indices (std::begin (icosahedron_faces),
                                            std::end (icosahedron_faces));
        constexpr std::uint16_t quarters[12] = { 0, 3, 5, 3, 1, 4,
                                                 5, 4, 2, 3, 4, 5 };
        for (std::uint16_t face = 0; face < 20; ++face)
          for (std::uint16_t slot : quarters)
            indices.push_back (std::uint16_t (face * 6 + slot));
        m_rock_indices = m_device->create_buffer (
          { .size = indices.size () * 2, .label = "boulder topology" },
          std::as_bytes (std::span (indices)));
      }
    }

    std::vector<float> NhalRenderer::spread_stand (
      const render::ForestSetup& setup,
      std::span<const render::ForestInstance> instances,
      const Vec3& period) {
      for (Texture* t : { &m_closure, &m_litter })
        if (*t)
          m_device->destroy (*t), *t = {};
      m_stand_inverse_period = { 0, 0 };
      if (period[0] <= 0.0f || period[2] <= 0.0f)
        return {};
      const std::uint32_t size = std::uint32_t (std::clamp (
        std::ceil (std::max (period[0], period[2]) / 3.0f), 256.0f, 1024.0f));
      const float step_x = period[0] / float (size);
      const float step_z = period[2] / float (size);
      const auto wrap = [&] (int v) {
        v %= int (size);
        return v < 0 ? v + int (size) : v;
      };
      const auto periodic = [] (float v, float extent) {
        return v - std::round (v / extent) * extent;
      };
      std::vector<float> depth (std::size_t (size) * size, 0.0f);
      for (const render::ForestInstance& tree : instances) {
        const Vec3 root = position_value (tree.root);
        const float radius = tree.crown_radius.numerical_value_in (u::m);
        const float sigma =
          std::max (0.49f * radius, 0.45f * std::max (step_x, step_z));
        const int reach = std::max (
          1, int (std::ceil (2.4f * sigma / std::min (step_x, step_z))));
        const int cx = int (std::floor (root[0] / step_x));
        const int cz = int (std::floor (root[2] / step_z));
        auto weight = [&] (int x, int z) {
          const float dx = periodic ((x + 0.5f) * step_x - root[0], period[0]);
          const float dz = periodic ((z + 0.5f) * step_z - root[2], period[2]);
          return std::exp (-0.5f * (dx * dx + dz * dz) / (sigma * sigma));
        };
        float sum = 0;
        for (int z = cz - reach; z <= cz + reach; ++z)
          for (int x = cx - reach; x <= cx + reach; ++x)
            sum += weight (wrap (x), wrap (z));
        const float scale = 0.70f * 3.14159265f * radius * radius /
                            (step_x * step_z * std::max (sum, 0.0001f));
        for (int z = cz - reach; z <= cz + reach; ++z)
          for (int x = cx - reach; x <= cx + reach; ++x)
            depth[std::size_t (wrap (z)) * size + wrap (x)] +=
              scale * weight (wrap (x), wrap (z));
      }
      std::vector<float> closure (depth.size ());
      for (std::size_t i = 0; i < depth.size (); ++i)
        closure[i] = 1.0f - std::exp (-depth[i]);

      // Each tree's stand: the mean closure over 24 metres around it.
      std::vector<float> stands;
      stands.reserve (instances.size ());
      const int reach_x = std::max (1, int (std::ceil (12.0f / step_x)));
      const int reach_z = std::max (1, int (std::ceil (12.0f / step_z)));
      for (const render::ForestInstance& tree : instances) {
        const Vec3 root = position_value (tree.root);
        const int cx = int (std::floor (root[0] / step_x));
        const int cz = int (std::floor (root[2] / step_z));
        float sum = 0;
        int samples = 0;
        for (int z = cz - reach_z; z <= cz + reach_z; ++z)
          for (int x = cx - reach_x; x <= cx + reach_x; ++x, ++samples)
            sum += closure[std::size_t (wrap (z)) * size + wrap (x)];
        stands.push_back (sum / float (samples));
      }

      // The ground reads a stand, not one tree: blur the closure over a
      // few texels, as Metal reads it from a coarser mip.
      std::vector<float> blurred = closure, line (size);
      for (int pass = 0; pass < 2; ++pass)
        for (int axis = 0; axis < 2; ++axis)
          for (std::uint32_t a = 0; a < size; ++a) {
            auto at = [&] (int b) -> float& {
              const std::uint32_t w = std::uint32_t (wrap (b));
              return axis == 0 ? blurred[std::size_t (a) * size + w]
                               : blurred[std::size_t (w) * size + a];
            };
            for (int b = 0; b < int (size); ++b)
              line[b] = (at (b - 1) + at (b) + at (b + 1)) / 3.0f;
            for (int b = 0; b < int (size); ++b)
              at (b) = line[b];
          }
      std::vector<std::uint8_t> bytes (blurred.size ());
      for (std::size_t i = 0; i < blurred.size (); ++i)
        bytes[i] = std::uint8_t (
          std::lround (255.0f * std::clamp (blurred[i], 0.0f, 1.0f)));
      m_closure = m_device->create_texture (
        { size, size, Format::r8_unorm, usage_sampled, 1, "canopy closure" });
      m_device->write_texture (m_closure, std::as_bytes (std::span (bytes)));
      if (setup.litter_size && !setup.litter.empty ()) {
        m_litter = m_device->create_texture ({ setup.litter_size,
                                               setup.litter_size,
                                               Format::r8_unorm,
                                               usage_sampled,
                                               1,
                                               "fallen leaves" });
        m_device->write_texture (m_litter, std::as_bytes (setup.litter));
      }
      m_stand_inverse_period = { 1.0f / period[0], 1.0f / period[2] };
      return stands;
    }

    void NhalRenderer::bind_stand () {
      m_device->set_texture (9, m_closure ? m_closure : m_white);
      m_device->set_texture (10, m_litter ? m_litter : m_black);
    }

    std::array<float, 4> NhalRenderer::stand_field () const {
      return { m_closure ? 1.0f : 0.0f,
               m_stand_inverse_period[0],
               m_stand_inverse_period[1],
               0 };
    }

    void NhalRenderer::set_terrain_materials (
      const render::TexturePixels& landscape,
      const render::TexturePixels& ground,
      const render::TexturePixels& plants,
      const render::TexturePixels& more_plants,
      const render::TexturePixels& scars,
      const render::TexturePixels& bedrock,
      bool) {
      auto sheet = [&] (Texture& texture,
                        const render::TexturePixels& pixels,
                        const char* label) {
        if (texture)
          m_device->destroy (texture);
        texture = {};
        if (pixels.empty () ||
            pixels.format () != render::PixelFormat::rgba8unorm)
          return;
        texture = m_device->create_texture ({ std::uint32_t (pixels.width ()),
                                              std::uint32_t (pixels.height ()),
                                              Format::rgba8_unorm,
                                              usage_sampled,
                                              1,
                                              label });
        std::vector<std::byte> bytes (pixels.byte_size ());
        pixels.write_into (bytes.data ());
        m_device->write_texture (texture, bytes);
      };
      sheet (m_landscape, landscape, "landscape materials");
      sheet (m_ground, ground, "ground materials");
      sheet (m_plants, plants, "plants");
      sheet (m_more_plants, more_plants, "more plants");
      sheet (m_scars, scars, "scars");
      sheet (m_bedrock, bedrock, "bedrock");
    }

    void NhalRenderer::offer_platform_drawable (void* drawable) {
      m_device->offer_drawable (drawable);
    }
  }

  std::unique_ptr<render::Renderer> create_renderer (
    std::unique_ptr<Device> device, const WorldShaders& shaders, float scale) {
    return std::make_unique<detail::NhalRenderer> (
      std::move (device), shaders, scale);
  }
}
