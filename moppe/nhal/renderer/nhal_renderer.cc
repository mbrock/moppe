// The renderer's construction and the resources the game hands it that
// belong to no one feature: textures, meshes, glyphs, and world text. The
// class is in nhal_renderer_impl.hh; each feature, the frame, and the
// scene's generic drawing are in the nhal_renderer_*.cc files beside this
// one.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

#include <bit>
#include <cmath>
#include <string_view>

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

    // Box-filter in linear light. Fractional taps include both edges of
    // odd-sized images. Alpha weights keep transparent colours out of RGB.
    static void upload_rgba (Device& device,
                             Texture texture,
                             const TextureDesc& desc,
                             std::span<const std::uint8_t> pixels) {
      const bool srgb = desc.format == Format::rgba8_unorm_srgb;
      static const auto linear = [] {
        std::array<float, 256> values {};
        for (unsigned i = 0; i < values.size (); ++i) {
          const float v = float (i) / 255.0f;
          values[i] =
            v <= 0.04045f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f);
        }
        return values;
      }();
      auto w = desc.width, h = desc.height;
      std::vector<std::uint8_t> storage;
      for (unsigned level = 0; level < desc.mip_levels; ++level) {
        device.write_texture (texture, std::as_bytes (pixels), 0, level);
        if (level + 1 == desc.mip_levels)
          break;
        const auto nw = std::max (1u, w / 2), nh = std::max (1u, h / 2);
        std::vector<std::uint8_t> next (std::size_t (nw) * nh * 4);
        for (unsigned y = 0; y < nh; ++y)
          for (unsigned x = 0; x < nw; ++x) {
            const double x0 = double (x) * w / nw, x1 = double (x + 1) * w / nw;
            const double y0 = double (y) * h / nh, y1 = double (y + 1) * h / nh;
            std::array<double, 4> sum {};
            double weight = 0;
            for (unsigned j = unsigned (y0); j < y1; ++j)
              for (unsigned i = unsigned (x0); i < x1; ++i) {
                const double area =
                  (std::min (x1, i + 1.0) - std::max (x0, double (i))) *
                  (std::min (y1, j + 1.0) - std::max (y0, double (j)));
                const auto* src =
                  pixels.data () + (std::size_t (j) * w + i) * 4;
                // Linear data textures have four independent channels.
                const double rgb_weight = srgb ? area * src[3] : area;
                weight += rgb_weight;
                for (unsigned c = 0; c < 3; ++c)
                  sum[c] +=
                    rgb_weight * (srgb ? linear[src[c]] : src[c] / 255.0);
                sum[3] += area * src[3];
              }
            auto* dst = next.data () + (std::size_t (y) * nw + x) * 4;
            for (unsigned c = 0; c < 3; ++c) {
              double v = weight > 0 ? sum[c] / weight : 0;
              if (srgb)
                v = v <= 0.0031308 ? 12.92 * v
                                   : 1.055 * std::pow (v, 1 / 2.4) - 0.055;
              dst[c] = std::uint8_t (std::lround (255 * v));
            }
            dst[3] =
              std::uint8_t (std::lround (sum[3] / ((x1 - x0) * (y1 - y0))));
          }
        w = nw;
        h = nh;
        storage = std::move (next);
        pixels = storage;
      }
    }

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
      const std::uint8_t gi_open[4] = { 128, 128, 255, 255 };
      m_gi_open = m_device->create_texture (
        { 1, 1, Format::rgba8_unorm, usage_sampled, 1, "open sky gi" });
      m_device->write_texture (m_gi_open, std::as_bytes (std::span (gi_open)));
      const render::GroundDetail detail = render::make_ground_detail ();
      constexpr auto side = render::GroundDetail::size;
      const TextureDesc sheet { side,
                                side,
                                Format::rgba8_unorm,
                                usage_sampled,
                                1,
                                "ground detail",
                                std::bit_width (unsigned (side)) };
      m_terrain.detail = m_device->create_texture (sheet);
      upload_rgba (*m_device, m_terrain.detail, sheet, detail.pixels);
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
      const auto width = std::uint32_t (desc.width);
      const auto height = std::uint32_t (desc.height);
      const unsigned levels = desc.filter == render::TextureFilter::Mipmap
                                ? std::bit_width (std::max (width, height))
                                : 1;
      const TextureDesc image { width,         height, Format::rgba8_unorm_srgb,
                                usage_sampled, 1,      "game texture",
                                levels };
      texture->handle = m_device->create_texture (image);
      const std::size_t texels = std::size_t (desc.width) * desc.height;
      std::vector<std::uint8_t> rgba (texels * 4, 255);
      const auto* source = static_cast<const std::uint8_t*> (pixels);
      if (source && desc.format == render::TextureFormat::RGB8) {
        for (std::size_t i = 0; i < texels; ++i)
          std::memcpy (&rgba[i * 4], &source[i * 3], 3);
      } else if (source) {
        std::memcpy (rgba.data (), source, rgba.size ());
      }
      upload_rgba (*m_device, texture->handle, image, rgba);
      return texture;
    }

    render::MeshPtr NhalRenderer::create_mesh (const DrawList& recorded) {
      auto mesh = std::make_shared<NhalMesh> ();
      mesh->device = m_device.get ();
      const auto& vertices = recorded.vertices ();
      if (vertices.empty ())
        return mesh;

      // Index the complete shader input, including split normals, paint,
      // UVs and wear. Views point into the immutable recording during bake.
      std::unordered_map<std::string_view, std::uint32_t> ids;
      std::vector<render::Vertex> unique;
      std::vector<std::uint32_t> corners;
      corners.reserve (vertices.size ());
      Vec3 low (vertices[0].px, vertices[0].py, vertices[0].pz);
      Vec3 high = low;
      for (const auto& v : vertices) {
        const std::string_view bytes (reinterpret_cast<const char*> (&v),
                                      sizeof v);
        auto [it, fresh] = ids.try_emplace (bytes, unique.size ());
        corners.push_back (it->second);
        if (fresh) {
          unique.push_back (v);
          mesh->wind |= v.wind != 0 || v.flutter != 0;
          const Vec3 p (v.px, v.py, v.pz);
          for (int i = 0; i < 3; ++i) {
            low[i] = std::min<float> (low[i], p[i]);
            high[i] = std::max<float> (high[i], p[i]);
          }
        }
      }
      mesh->bounds.centre = position ((low + high) * 0.5f);
      mesh->bounds.half_extent = displacement ((high - low) * 0.5f);
      const auto upload = [this] (const auto& data, const char* label) {
        const auto bytes = std::as_bytes (std::span (data));
        return m_device->create_buffer (
          { .size = bytes.size (), .label = label }, bytes);
      };
      mesh->vertices = upload (unique, "mesh");
      if (unique.size () <= 65535) {
        std::vector<std::uint16_t> narrow (corners.begin (), corners.end ());
        mesh->indices = upload (narrow, "mesh indices");
        mesh->index_type = IndexType::uint16;
      } else {
        mesh->indices = upload (corners, "mesh indices");
      }
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
