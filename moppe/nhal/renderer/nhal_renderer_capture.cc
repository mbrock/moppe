// What is taken of a finished frame: screenshots, written as small
// uncompressed PNGs, and frames handed to a sink (the video recorder).

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    namespace {
      inline std::uint32_t crc (const std::uint8_t* data,
                                std::size_t size,
                                std::uint32_t c = 0xffffffffu) {
        static std::uint32_t table[256];
        if (!table[1])
          for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t v = n;
            for (int k = 0; k < 8; ++k)
              v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
            table[n] = v;
          }
        for (std::size_t i = 0; i < size; ++i)
          c = table[(c ^ data[i]) & 255] ^ (c >> 8);
        return c;
      }

      inline void write_png (const std::string& path,
                             std::uint32_t width,
                             std::uint32_t height,
                             const std::vector<std::uint8_t>& rgb) {
        std::vector<std::uint8_t> raw;
        raw.reserve ((width * 3 + 1) * height);
        for (std::uint32_t y = 0; y < height; ++y) {
          raw.push_back (0);
          raw.insert (raw.end (),
                      rgb.begin () + std::size_t (y) * width * 3,
                      rgb.begin () + std::size_t (y + 1) * width * 3);
        }
        // zlib with stored deflate blocks.
        std::vector<std::uint8_t> z { 0x78, 0x01 };
        std::uint32_t a = 1, b = 0;
        for (std::size_t at = 0; at < raw.size ();) {
          const std::size_t n = std::min<std::size_t> (65535, raw.size () - at);
          z.push_back (at + n == raw.size () ? 1 : 0);
          z.push_back (n & 255);
          z.push_back (n >> 8);
          z.push_back (~n & 255);
          z.push_back ((~n >> 8) & 255);
          z.insert (z.end (), raw.begin () + at, raw.begin () + at + n);
          for (std::size_t i = at; i < at + n; ++i) {
            a = (a + raw[i]) % 65521;
            b = (b + a) % 65521;
          }
          at += n;
        }
        const std::uint32_t adler = (b << 16) | a;
        for (int s = 24; s >= 0; s -= 8)
          z.push_back ((adler >> s) & 255);

        FILE* file = std::fopen (path.c_str (), "wb");
        if (!file)
          return;
        auto u32 = [] (std::vector<std::uint8_t>& v, std::uint32_t x) {
          for (int s = 24; s >= 0; s -= 8)
            v.push_back ((x >> s) & 255);
        };
        auto chunk = [&] (const char* type,
                          const std::vector<std::uint8_t>& d) {
          std::vector<std::uint8_t> out;
          u32 (out, std::uint32_t (d.size ()));
          out.insert (out.end (), type, type + 4);
          out.insert (out.end (), d.begin (), d.end ());
          u32 (out, ~crc (out.data () + 4, out.size () - 4));
          std::fwrite (out.data (), 1, out.size (), file);
        };
        const std::uint8_t signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
        std::fwrite (signature, 1, 8, file);
        std::vector<std::uint8_t> header;
        u32 (header, width);
        u32 (header, height);
        header.insert (header.end (), { 8, 2, 0, 0, 0 });
        chunk ("IHDR", header);
        chunk ("IDAT", z);
        chunk ("IEND", {});
        std::fclose (file);
      }

      inline float half_to_float (std::uint16_t h) {
        const std::uint32_t e = (h >> 10) & 31, m = h & 1023;
        const float v = e == 0    ? std::ldexp (float (m), -24)
                        : e == 31 ? 65504.0f
                                  : std::ldexp (float (m | 1024), int (e) - 25);
        return (h >> 15) ? -v : v;
      }
    }

    void NhalRenderer::request_screenshot (const std::string& path) {
      if (moppe::environment ("MOPPE_MEMORY_REPORT"))
        std::fprintf (
          stderr,
          "moppe: forest cull lanes: trees=%u camera=%zu near=%zu far=%zu\n",
          m_forest.count,
          m_forest.lanes[0],
          m_forest.lanes[1],
          m_forest.lanes[2]);
      m_screenshot = path;
    }

    void NhalRenderer::request_frame (
      std::function<void (const render::FramePixels&)> sink) {
      m_frame_sink = std::move (sink);
    }

    const std::array<std::uint8_t, 65536>& NhalRenderer::display_bytes () {
      static const std::unique_ptr<std::array<std::uint8_t, 65536>> table = [] {
        auto bytes = std::make_unique<std::array<std::uint8_t, 65536>> ();
        for (std::uint32_t h = 0; h < 65536; ++h) {
          const float v = std::clamp (
            half_to_float (static_cast<std::uint16_t> (h)), 0.0f, 1.0f);
          (*bytes)[h] = std::uint8_t (std::pow (v, 1 / 2.2f) * 255 + 0.5f);
        }
        return bytes;
      }();
      return *table;
    }

    std::vector<std::uint8_t>
    NhalRenderer::capture_rgb (const Capture& capture) {
      const Format format = capture.format;
      const std::size_t pixel_bytes = bytes_per_pixel (format);
      std::vector<std::uint8_t> rgb (std::size_t (capture.width) *
                                     capture.height * 3);
      const std::array<std::uint8_t, 65536>& shown = display_bytes ();
      const bool bgra = format == Format::bgra8_unorm;
      parallel_for (
        capture.height, 64, [&] (std::size_t begin, std::size_t end) {
          for (std::size_t y = begin; y < end; ++y) {
            const std::byte* p = capture.pixels.data () + y * capture.row_bytes;
            std::uint8_t* out = &rgb[y * capture.width * 3];
            for (std::uint32_t x = 0; x < capture.width;
                 ++x, p += pixel_bytes, out += 3) {
              if (format == Format::rgba16_float) {
                std::uint16_t h[3];
                std::memcpy (h, p, sizeof (h));
                out[0] = shown[h[0]];
                out[1] = shown[h[1]];
                out[2] = shown[h[2]];
              } else {
                out[0] = std::uint8_t (p[bgra ? 2 : 0]);
                out[1] = std::uint8_t (p[1]);
                out[2] = std::uint8_t (p[bgra ? 0 : 2]);
              }
            }
          }
        });
      return rgb;
    }

    void NhalRenderer::end_device_frame () {
      if (!m_screenshot.empty () || m_frame_sink) {
        m_device->capture_frame (
          [path = std::move (m_screenshot),
           sink = std::move (m_frame_sink)] (const Capture& capture) {
            const std::vector<std::uint8_t> rgb = capture_rgb (capture);
            if (sink)
              sink ({ int (capture.width), int (capture.height), rgb });
            if (!path.empty ()) {
              write_png (path, capture.width, capture.height, rgb);
              std::cerr << "moppe: wrote " << path << std::endl;
            }
          });
        m_screenshot.clear ();
        m_frame_sink = nullptr;
        // Hosts may quit straight after the frame they asked to capture,
        // so the readback completes here rather than next frame.
        m_device->end_frame ();
        m_device->wait_idle ();
      } else {
        m_device->end_frame ();
      }
    }
  }
}
