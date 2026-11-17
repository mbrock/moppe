// The renderer's construction and the resources the game hands it that
// belong to no one feature: textures, meshes, glyphs, and world text. The
// class is in nhal_renderer_impl.hh; each feature, the frame, and the
// scene's generic drawing are in the nhal_renderer_*.cc files beside this
// one.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    // The shaders read streamed vertices by word (world.lisp's
    // *streamed-vertex*); a field moved in render::Vertex or there stops
    // the build here.
    namespace streamed = streamed_vertex;
    constexpr std::size_t word = 4;
    using V = render::Vertex;
    static_assert (sizeof (V) == word * streamed::stride);
    static_assert (offsetof (V, px) == word * streamed::px);
    static_assert (offsetof (V, py) == word * streamed::py);
    static_assert (offsetof (V, pz) == word * streamed::pz);
    static_assert (offsetof (V, nx) == word * streamed::nx);
    static_assert (offsetof (V, ny) == word * streamed::ny);
    static_assert (offsetof (V, nz) == word * streamed::nz);
    static_assert (offsetof (V, u) == word * streamed::u);
    static_assert (offsetof (V, v) == word * streamed::v);
    static_assert (offsetof (V, color) == word * streamed::color);
    static_assert (offsetof (V, lit) == word * streamed::lit);
    static_assert (offsetof (V, surface) == word * streamed::surface);

    NhalRenderer::NhalRenderer (std::unique_ptr<Device> device,
                                const WorldShaders& shaders,
                                float scale)
        : m_device (std::move (device)), m_shaders (shaders), m_scale (scale),
          m_first_vertex_counts (m_device->info ().indices_count_from_first) {
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
      m_terrain.detail = m_device->create_texture ({ render::GroundDetail::size,
                                                     render::GroundDetail::size,
                                                     Format::rgba8_unorm,
                                                     usage_sampled,
                                                     1,
                                                     "ground detail" });
      m_device->write_texture (m_terrain.detail,
                               std::as_bytes (std::span (detail.pixels)));
      // No current at all, for water drawn before (or without) a flow.
      const std::uint16_t still[4] = { 0, 0, 0, 0 };
      m_water.still = m_device->create_texture (
        { 1, 1, Format::rgba16_float, usage_sampled, 1, "still water" });
      m_device->write_texture (m_water.still,
                               std::as_bytes (std::span (still)));
      std::cerr << "moppe: NHAL renderer on " << m_device->info ().backend
                << " (" << m_device->info ().adapter << ")" << std::endl;
    }

    void NhalRenderer::make_pipelines () {
      make_terrain_pipelines ();
      make_scene_pipelines ();
      make_forest_pipelines ();
      make_shadow_pipelines ();
      make_boulders_pipelines ();
      make_water_pipelines ();
      make_grass_pipelines ();
      make_frame_pipelines ();
      make_post_pipelines ();
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
