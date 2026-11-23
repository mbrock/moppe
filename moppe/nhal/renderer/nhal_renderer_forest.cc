// The forest: its trees' records and the stand's rasters splatted from
// them, the cull that sorts the trees into their classes' indirect draws
// for the camera and the sun, the trunks and crowns drawn from those, and
// the leaves drifting down where the crowns have turned.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>
#include <moppe/terrain/torus.hh>

namespace moppe::nhal {
  namespace detail {
    namespace {
      // A step function of the tier from one of world.lisp's *tree-steps*
      // rows: its value at the coarsest tier, then each tier it changes at
      // and the value from there on.
      template <std::size_t N>
      constexpr std::uint32_t tree_step (const unsigned (&row)[N],
                                         std::uint32_t tier) {
        std::uint32_t value = row[0];
        for (std::size_t i = 1; i + 1 < N; i += 2)
          if (tier >= row[i])
            value = row[i + 1];
        return value;
      }

      struct TreeTopology {
        std::uint32_t sides, crown_sides, masses;
        bool conifer, branches, fell;

        // SPECIES as the cull numbers classes: 0 birch, 1 spruce, 2 fell
        // birch, whose clumps go in pairs on stems of their own at any tier,
        // each branch bent at a knee through a third ring.
        TreeTopology (std::uint32_t species, std::uint32_t tier)
            : sides (tree_step (tree_steps::sides, tier)),
              crown_sides (sides == 10 ? 9u : sides),
              masses (tree_step (species == 1   ? tree_steps::masses_spruce
                                 : species == 2 ? tree_steps::masses_fell
                                                : tree_steps::masses_birch,
                                 tier)),
              conifer (species == 1),
              branches (species == 2 ||
                        (species == 0 && tier >= tree_steps::branches)),
              fell (species == 2) {}

        std::uint32_t mass_vertices () const {
          return conifer ? crown_sides + 2 : 8 + (fell ? 9 : branches ? 6 : 0);
        }
        std::uint32_t mass_primitives () const {
          return conifer ? 2 * crown_sides
                         : 12 + (fell       ? 12
                                 : branches ? 6
                                            : 0);
        }

        void triangles (std::vector<std::uint16_t>& out) const {
          auto add = [&] (std::uint32_t a, std::uint32_t b, std::uint32_t c) {
            out.insert (
              out.end (),
              { std::uint16_t (a), std::uint16_t (b), std::uint16_t (c) });
          };
          // Three bands of quads between the trunk's four rings.
          for (std::uint32_t p = 0; p < sides * 6; ++p) {
            const std::uint32_t band = p / (2 * sides);
            const std::uint32_t quad = (p / 2) % sides;
            const std::uint32_t next = (quad + 1) % sides;
            const std::uint32_t a = band * sides + quad,
                                b = band * sides + next;
            const std::uint32_t c = a + sides, d = b + sides;
            add ((p & 1) ? b : a, (p & 1) ? d : b, c);
          }
          for (std::uint32_t mass = 0; mass < masses; ++mass) {
            const std::uint32_t first = sides * 4 + mass * mass_vertices ();
            const std::uint32_t n = crown_sides;
            for (std::uint32_t p = 0; p < mass_primitives (); ++p) {
              if (conifer) {
                // A cone: its ring to the apex, then to the closing centre.
                const std::uint32_t side = p % n;
                add (first + side,
                     first + (side + 1) % n,
                     first + (p < n ? n : n + 1));
              } else if (p < 12) {
                // A clump: six faces about its top tip, six about its bottom.
                const std::uint32_t k = p % 6;
                const std::uint32_t e0 = first + 2 + k;
                const std::uint32_t e1 = first + 2 + (k + 1) % 6;
                add (
                  p < 6 ? first : first + 1, p < 6 ? e0 : e1, p < 6 ? e1 : e0);
              } else {
                // Its branch, a three-sided prism in one or two bands.
                const std::uint32_t q = p - 12, band = q / 6, k = q % 6 / 2;
                const std::uint32_t a = first + 8 + band * 3 + k;
                const std::uint32_t b = first + 8 + band * 3 + (k + 1) % 3;
                add ((q & 1) ? b : a, (q & 1) ? b + 3 : b, a + 3);
              }
            }
          }
        }
      };
    }

    void NhalRenderer::make_forest_pipelines () {
      const RenderPipelineDesc scene = scene_target ();
      const RenderPipelineDesc caster = shadow_caster ();
      RenderPipelineDesc forest = scene;
      forest.program = &shaders::forest::program;
      forest.vertex = m_shaders.forest[0];
      forest.fragment = m_shaders.forest[1];
      forest.cull = Cull::none;
      forest.label = "forest";
      m_forest.draw = m_device->create_render_pipeline (forest);
      m_forest.cull =
        m_device->create_compute_pipeline ({ &shaders::forest_cull::program,
                                             m_shaders.forest_cull,
                                             "forest culling" });

      RenderPipelineDesc tree_caster = caster;
      tree_caster.program = &shaders::forest_shadow::program;
      tree_caster.vertex = m_shaders.forest_shadow[0];
      tree_caster.label = "forest shadow";
      m_forest.shadow = m_device->create_render_pipeline (tree_caster);
      RenderPipelineDesc far_tree_caster = caster;
      far_tree_caster.program = &shaders::forest_far_shadow::program;
      far_tree_caster.vertex = m_shaders.forest_far_shadow[0];
      far_tree_caster.cull = Cull::none;
      far_tree_caster.label = "far forest shadow";
      m_forest.far_shadow = m_device->create_render_pipeline (far_tree_caster);
      {
        // The far casters' double cone: six faces up to the top (vertex
        // 0) and six down to the base (1) from the ring (2 to 7).
        std::array<std::uint16_t, far_tree_indices> cone {};
        for (std::uint16_t side = 0; side < 6; ++side) {
          const std::uint16_t a = 2 + side, b = 2 + (side + 1) % 6;
          const std::array<std::uint16_t, 6> faces { 0, a, b, 1, b, a };
          std::copy (faces.begin (), faces.end (), cone.begin () + 6 * side);
        }
        m_forest.far_indices = m_device->create_buffer (
          { .size = sizeof cone, .label = "far tree shadow cone" },
          std::as_bytes (std::span (cone)));
      }
      RenderPipelineDesc leaves = scene;
      leaves.program = &shaders::leaves::program;
      leaves.vertex = m_shaders.leaves[0];
      leaves.fragment = m_shaders.leaves[1];
      leaves.cull = Cull::none;
      leaves.label = "falling leaves";
      m_forest.leaves = m_device->create_render_pipeline (leaves);
    }

    void NhalRenderer::set_forest (
      const render::ForestSetup& setup,
      std::span<const render::ForestInstance> instances) {
      for (Buffer* b : { &m_forest.records,
                         &m_forest.classified,
                         &m_forest.candidates,
                         &m_forest.arguments,
                         &m_forest.shadow_candidates[0],
                         &m_forest.shadow_arguments[0],
                         &m_forest.shadow_candidates[1],
                         &m_forest.shadow_arguments[1] })
        if (*b)
          m_device->destroy (*b), *b = {};
      m_forest.count = std::uint32_t (instances.size ());
      m_forest.capacity = {};
      m_forest.scratch_capacity = 0;
      const Vec3 forest_period = extent_value (setup.period);
      const std::vector<float> closure =
        spread_stand (setup, instances, forest_period);
      const auto order = m_forest.grid.build (instances, forest_period);
      if (instances.empty ())
        return;
      // Four rows a tree. The stand closure that decides where a crown
      // starts is read from the splatted canopy over 24 metres.
      std::vector<std::array<float, 4>> rows;
      rows.reserve (instances.size () * 4);
      for (auto original : order) {
        const auto& tree = instances[original];
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
                          closure.empty () ? cover : closure[original],
                          tree.autumn.numerical_value_in (mp_units::one) });
        rows.push_back ({ bits (tree.seed),
                          bits (std::uint32_t (tree.species)),
                          bits (std::uint32_t (tree.age)),
                          bits (original) });
      }
      m_forest.records = m_device->create_buffer (
        { .size = rows.size () * 16, .label = "forest records" },
        std::as_bytes (std::span (rows)));
      m_forest.arguments = m_device->create_buffer (
        { .size = forest_classes * sizeof (DrawIndexedIndirectArgs),
          .usage = buffer_storage_write | buffer_indirect,
          .label = "forest draws" });
      for (std::size_t cascade = 0; cascade < 2; ++cascade) {
        m_forest.shadow_arguments[cascade] = m_device->create_buffer (
          { .size = forest_classes * sizeof (DrawIndexedIndirectArgs),
            .usage = buffer_storage_write | buffer_indirect,
            .label = "shadow tree draws" });
      }

      if (!m_forest.indices) {
        std::vector<std::uint16_t> indices;
        for (std::uint32_t c = 0; c < forest_classes; ++c) {
          m_forest.class_first[c] = std::uint32_t (indices.size ());
          TreeTopology (c / forest_tiers, c % forest_tiers).triangles (indices);
          m_forest.class_count[c] =
            std::uint32_t (indices.size ()) - m_forest.class_first[c];
        }
        m_forest.indices = m_device->create_buffer (
          { .size = indices.size () * 2, .label = "forest topology" },
          std::as_bytes (std::span (indices)));
      }
    }

    std::vector<float> NhalRenderer::spread_stand (
      const render::ForestSetup& setup,
      std::span<const render::ForestInstance> instances,
      const Vec3& period) {
      for (Texture* t : { &m_forest.closure, &m_forest.litter })
        if (*t)
          m_device->destroy (*t), *t = {};
      m_forest.stand_inverse_period = { 0, 0 };
      if (instances.empty () || period[0] <= 0.0f || period[2] <= 0.0f)
        return {};
      const std::uint32_t size = std::uint32_t (std::clamp (
        std::ceil (std::max (period[0], period[2]) / 3.0f), 256.0f, 1024.0f));
      const float step_x = period[0] / float (size);
      const float step_z = period[2] / float (size);
      const auto wrap = [&] (int v) {
        v %= int (size);
        return v < 0 ? v + int (size) : v;
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
          const float dx = terrain::minimum_image_delta (
            (x + 0.5f) * step_x - root[0], period[0]);
          const float dz = terrain::minimum_image_delta (
            (z + 0.5f) * step_z - root[2], period[2]);
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
      m_forest.closure = m_device->create_texture (
        { size, size, Format::r8_unorm, usage_sampled, 1, "canopy closure" });
      m_device->write_texture (m_forest.closure,
                               std::as_bytes (std::span (bytes)));
      if (setup.litter_size && !setup.litter.empty ()) {
        m_forest.litter = m_device->create_texture ({ setup.litter_size,
                                                      setup.litter_size,
                                                      Format::r8_unorm,
                                                      usage_sampled,
                                                      1,
                                                      "fallen leaves" });
        m_device->write_texture (m_forest.litter, std::as_bytes (setup.litter));
      }
      m_forest.stand_inverse_period = { 1.0f / period[0], 1.0f / period[2] };
      return stands;
    }

    void NhalRenderer::bind_stand () {
      m_device->set_texture (9, m_forest.closure ? m_forest.closure : m_white);
      m_device->set_texture (10, m_forest.litter ? m_forest.litter : m_black);
    }

    std::array<float, 4> NhalRenderer::stand_field () const {
      return { m_forest.closure ? 1.0f : 0.0f,
               m_forest.stand_inverse_period[0],
               m_forest.stand_inverse_period[1],
               0 };
    }

    void NhalRenderer::cull_trees (const Mat4& view,
                                   const Vec3& eye,
                                   float height,
                                   bool shadow,
                                   std::size_t cascade) {
      if (!m_forest.count)
        return;
      shaders::forest_cull::Forest forest {};
      forest.view = matrix (view);
      forest.eye = lanes (eye);
      forest.world = { m_forest.grid.period ()[0],
                       m_forest.grid.period ()[1],
                       float (m_forest.count),
                       float (m_forest.count) };
      forest.cull = { ForestGrid::row_length (view, 0),
                      ForestGrid::row_length (view, 1),
                      ForestGrid::row_length (view, 3),
                      height };
      forest.mode = { shadow ? 1.0f + float (cascade) : 0.0f, 0, 0, 0 };
      Transient& block =
        shadow ? m_forest.shadow_block[cascade] : m_forest.block;
      block = m_device->allocate (sizeof forest);
      std::memcpy (block.data, &forest, sizeof forest);

      std::array<DrawIndexedIndirectArgs, forest_classes> reset {};
      for (std::uint32_t c = 0; c < forest_classes; ++c) {
        // The shadow's classes are each species' tier 0.
        const std::uint32_t topology = shadow ? c * forest_tiers : c;
        if (shadow && cascade == render::LocalShadowParams::far)
          reset[c] = { far_tree_indices, 0, 0, 0, 0 };
        else if (topology < forest_classes)
          reset[c] = { m_forest.class_count[topology],
                       0,
                       m_forest.class_first[topology],
                       0,
                       0 };
      }
      const Buffer arguments =
        shadow ? m_forest.shadow_arguments[cascade] : m_forest.arguments;
      m_device->copy_to_buffer (
        arguments,
        0,
        m_device->upload (std::span<const DrawIndexedIndirectArgs> (reset)));

      const auto blocks = m_forest.grid.visible_blocks (
        view,
        eye,
        height,
        shadow,
        moppe::environment ("MOPPE_FOREST_ALL_TILES"));
      const std::size_t v = shadow ? cascade + 1 : 0;
      m_forest.lanes[v] = 64 * blocks.size ();
      // One output per active lane suffices even when all trees survive
      // in one class. Grow in pages to avoid churn as the view moves.
      const std::size_t capacity = std::max<std::size_t> (
        4096, ((m_forest.lanes[v] + 4095) / 4096) * 4096);
      auto reserve = [&] (Buffer& buffer,
                          std::size_t& allocated,
                          std::size_t header,
                          const char* label) {
        if (capacity <= allocated)
          return;
        if (buffer)
          m_device->destroy (buffer);
        allocated = capacity;
        buffer = m_device->create_buffer ({ .size = header + capacity * 8,
                                            .usage = buffer_storage_write,
                                            .label = label });
      };
      Buffer& candidates =
        shadow ? m_forest.shadow_candidates[cascade] : m_forest.candidates;
      reserve (
        candidates, m_forest.capacity[v], 4 * forest_classes, "visible trees");
      if (blocks.empty ())
        return;
      reserve (m_forest.classified,
               m_forest.scratch_capacity,
               0,
               "tree classification");
      const Transient ranges = m_device->upload (
        std::span<const std::array<std::uint32_t, 2>> (blocks));
      for (int pass = 0; pass != 3; ++pass) {
        m_device->begin_compute_pass ("forest classification and compaction");
        m_device->set_pipeline (m_forest.cull);
        forest.mode[1] = float (pass);
        m_device->set_uniforms (1, forest);
        m_device->set_buffer (2, m_forest.records);
        m_device->set_buffer (3, candidates);
        m_device->set_buffer (4, arguments);
        m_device->set_buffer (5, ranges);
        m_device->set_buffer (6, m_forest.classified);
        m_device->dispatch (pass == 1 ? 1 : std::uint32_t (blocks.size ()));
        m_device->end_compute_pass ();
      }
    }

    void NhalRenderer::draw_forest () {
      if (!m_forest.count)
        return;
      open_scene ();
      m_device->set_pipeline (m_forest.draw);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_buffer (1, m_forest.block);
      m_device->set_buffer (3, m_forest.records);
      m_device->set_buffer (4, m_forest.candidates);
      m_device->set_texture (8, m_sun_shadow.map);
      for (std::uint32_t c = 0; c < forest_classes; ++c) {
        shaders::forest::Draw draw {};
        draw.class_ = { float (c), 0, 0, 0 };
        m_device->set_uniforms (2, draw);
        m_device->draw_indexed_indirect (m_forest.indices,
                                         IndexType::uint16,
                                         m_forest.arguments,
                                         c * sizeof (DrawIndexedIndirectArgs));
      }
    }

    void NhalRenderer::draw_falling_leaves () {
      if (!m_terrain.have || !m_forest.litter || m_scene_closed)
        return;
      open_scene ();
      constexpr float cell_metres = 1.2f;
      constexpr int side = 64;
      const Vec3 camera = position_value (m_params.camera_pos);
      const auto& t = m_terrain.params;
      shaders::leaves::Leaves leaves {};
      leaves.window = { std::floor (camera[0] / cell_metres) - side / 2,
                        std::floor (camera[2] / cell_metres) - side / 2,
                        float (side),
                        cell_metres };
      leaves.lattice = {
        1.0f / t.scale[0], 1.0f / t.scale[2], t.scale[1], float (t.width)
      };
      leaves.stand_field = { 1.0f,
                             m_forest.stand_inverse_period[0],
                             m_forest.stand_inverse_period[1],
                             0.5f * side * cell_metres };
      m_device->set_pipeline (m_forest.leaves);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, leaves);
      m_device->set_texture (0, m_terrain.heights);
      m_device->set_texture (8, m_sun_shadow.map);
      bind_stand ();
      m_device->draw (6, side * side);
    }
  }
}
