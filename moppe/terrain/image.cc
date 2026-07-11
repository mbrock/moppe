#include <moppe/terrain/image.hh>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <ostream>
#include <span>
#include <stdexcept>
#include <vector>

#include <zlib.h>

namespace moppe::terrain {
  namespace {
    void write_u32 (std::ostream& stream, std::uint32_t value) {
      const std::array<unsigned char, 4> bytes {
        static_cast<unsigned char> (value >> 24),
        static_cast<unsigned char> (value >> 16),
        static_cast<unsigned char> (value >> 8),
        static_cast<unsigned char> (value)
      };
      stream.write (reinterpret_cast<const char*> (bytes.data ()),
                    bytes.size ());
    }

    uInt zlib_size (std::size_t size) {
      if (size > std::numeric_limits<uInt>::max ())
        throw std::invalid_argument ("PNG chunk exceeds zlib size limit");
      return static_cast<uInt> (size);
    }

    void write_chunk (std::ostream& stream,
                      const std::array<unsigned char, 4>& type,
                      std::span<const unsigned char> data) {
      write_u32 (stream, static_cast<std::uint32_t> (data.size ()));
      stream.write (reinterpret_cast<const char*> (type.data ()), type.size ());
      stream.write (reinterpret_cast<const char*> (data.data ()),
                    static_cast<std::streamsize> (data.size ()));

      uLong crc = crc32 (0, Z_NULL, 0);
      crc = crc32 (crc, type.data (), zlib_size (type.size ()));
      crc = crc32 (crc, data.data (), zlib_size (data.size ()));
      write_u32 (stream, static_cast<std::uint32_t> (crc));
    }
  }

  void write_grayscale_png (std::ostream& stream,
                            const ScalarRaster& raster,
                            float black,
                            float white) {
    const Domain2D& domain = raster.domain ();
    if (domain.width > std::numeric_limits<std::uint32_t>::max () ||
        domain.height > std::numeric_limits<std::uint32_t>::max ())
      throw std::invalid_argument ("PNG dimensions exceed 32-bit limits");
    if (!(white > black))
      throw std::invalid_argument ("display white must exceed black");

    std::vector<unsigned char> scanlines;
    scanlines.reserve ((domain.width + 1) * domain.height);
    for (std::size_t y = 0; y < domain.height; ++y) {
      scanlines.push_back (0); // PNG filter: none
      for (std::size_t x = 0; x < domain.width; ++x) {
        const float value = raster.at (x, y);
        const float normalized =
          std::clamp ((value - black) / (white - black), 0.0f, 1.0f);
        scanlines.push_back (
          static_cast<unsigned char> (normalized * 255.0f + 0.5f));
      }
    }

    std::vector<unsigned char> compressed (compressBound (scanlines.size ()));
    uLongf compressed_size = compressed.size ();
    if (compress2 (compressed.data (),
                   &compressed_size,
                   scanlines.data (),
                   scanlines.size (),
                   Z_BEST_SPEED) != Z_OK)
      throw std::runtime_error ("failed to compress grayscale PNG");
    compressed.resize (compressed_size);

    static constexpr std::array<unsigned char, 8> signature { 0x89, 'P',  'N',
                                                              'G',  0x0d, 0x0a,
                                                              0x1a, 0x0a };
    stream.write (reinterpret_cast<const char*> (signature.data ()),
                  signature.size ());

    std::array<unsigned char, 13> header {};
    const auto width = static_cast<std::uint32_t> (domain.width);
    const auto height = static_cast<std::uint32_t> (domain.height);
    header[0] = width >> 24;
    header[1] = width >> 16;
    header[2] = width >> 8;
    header[3] = width;
    header[4] = height >> 24;
    header[5] = height >> 16;
    header[6] = height >> 8;
    header[7] = height;
    header[8] = 8; // bit depth
    header[9] = 0; // grayscale

    write_chunk (stream, { 'I', 'H', 'D', 'R' }, header);
    write_chunk (stream, { 'I', 'D', 'A', 'T' }, compressed);
    write_chunk (stream, { 'I', 'E', 'N', 'D' }, {});
    if (!stream)
      throw std::runtime_error ("failed to write grayscale PNG");
  }
}
