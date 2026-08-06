#ifndef MOPPE_RENDER_TEXTURE_PIXELS_HH
#define MOPPE_RENDER_TEXTURE_PIXELS_HH

#include <moppe/gfx/math.hh>
#include <moppe/spatial/bundle.hh>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// A texture that has not been built yet.
//
// Every terrain overlay used to reach the GPU the same tired way: copy a
// typed column into a vector of floats, interleave those into another vector
// of floats, narrow that into a vector of halves, and hand the result to a
// backend that immediately copied it once more into a staging buffer. The
// float stages carried no information -- a reading and its pixel are the same
// four bytes -- and the last one was actively wrong, since most of these
// textures are half precision.
//
// A TexturePixels replaces the whole chain with a rule. It knows the lattice
// it covers and the format its bytes will be in, and it can write exactly
// those bytes wherever a backend wants them. Gathering channels, selecting
// components, and narrowing all happen in that one pass, straight into
// GPU-visible memory.
//
// The rule borrows what it reads and stores no copy, so a backend must
// consume a TexturePixels within the call that receives it. Every upload path
// today commits and waits before returning, which satisfies that; anything
// that later defers an upload has to materialize the bytes first.

namespace moppe::render {
  enum class PixelFormat {
    r32f,
    rg32f,
    r16f,
    rg16f,
    rg16snorm,
    rgba8unorm,
    rg8snorm
  };

  constexpr std::size_t channels_in (PixelFormat format) {
    switch (format) {
    case PixelFormat::r32f:
    case PixelFormat::r16f:
      return 1;
    case PixelFormat::rg32f:
    case PixelFormat::rg16f:
    case PixelFormat::rg16snorm:
    case PixelFormat::rg8snorm:
      return 2;
    case PixelFormat::rgba8unorm:
      return 4;
    }
    return 0;
  }

  constexpr std::size_t channel_bytes (PixelFormat format) {
    switch (format) {
    case PixelFormat::r32f:
    case PixelFormat::rg32f:
      return 4;
    case PixelFormat::r16f:
    case PixelFormat::rg16f:
    case PixelFormat::rg16snorm:
      return 2;
    case PixelFormat::rgba8unorm:
    case PixelFormat::rg8snorm:
      return 1;
    }
    return 0;
  }

  constexpr std::size_t bytes_per_pixel (PixelFormat format) {
    return channels_in (format) * channel_bytes (format);
  }

  // IEEE binary16. Written out rather than borrowed from a compiler extension
  // so the packing is the same on every backend this builds for.
  constexpr std::uint16_t float_to_half (float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t> (value);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::int32_t exponent =
      static_cast<std::int32_t> ((bits >> 23) & 0xffu) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7fffffu;
    if (exponent >= 0x1f)
      return static_cast<std::uint16_t> (sign | 0x7c00u |
                                         (mantissa != 0 ? 0x200u : 0u));
    if (exponent <= 0) {
      if (exponent < -10)
        return static_cast<std::uint16_t> (sign);
      mantissa |= 0x800000u;
      const std::uint32_t shift = static_cast<std::uint32_t> (14 - exponent);
      return static_cast<std::uint16_t> (sign | (mantissa >> shift));
    }
    return static_cast<std::uint16_t> (
      sign | (static_cast<std::uint32_t> (exponent) << 10) | (mantissa >> 13));
  }

  // The inverse, for reading a packed texture back. Tests and tools want it;
  // the upload path never does.
  inline float half_to_float (std::uint16_t half) {
    const std::uint32_t sign = (static_cast<std::uint32_t> (half) & 0x8000u)
                               << 16;
    std::uint32_t exponent = (half >> 10) & 0x1fu;
    std::uint32_t mantissa = half & 0x3ffu;
    if (exponent == 0) {
      if (mantissa == 0)
        return std::bit_cast<float> (sign);
      exponent = 1;
      while ((mantissa & 0x400u) == 0) {
        mantissa <<= 1;
        --exponent;
      }
      mantissa &= 0x3ffu;
    } else if (exponent == 0x1f) {
      return std::bit_cast<float> (sign | 0x7f800000u | (mantissa << 13));
    }
    return std::bit_cast<float> (sign | ((exponent + 127 - 15) << 23) |
                                 (mantissa << 13));
  }

  namespace detail {
    inline void write_channel (std::byte* at, PixelFormat format, float value) {
      switch (format) {
      case PixelFormat::r32f:
      case PixelFormat::rg32f:
        std::memcpy (at, &value, sizeof (float));
        return;
      case PixelFormat::r16f:
      case PixelFormat::rg16f: {
        const std::uint16_t half = float_to_half (value);
        std::memcpy (at, &half, sizeof half);
        return;
      }
      case PixelFormat::rg16snorm: {
        const std::int16_t packed = static_cast<std::int16_t> (
          std::clamp (value, -1.0f, 1.0f) * 32767.0f);
        std::memcpy (at, &packed, sizeof packed);
        return;
      }
      case PixelFormat::rgba8unorm: {
        const std::uint8_t packed = static_cast<std::uint8_t> (
          std::clamp (value, 0.0f, 1.0f) * 255.0f + 0.5f);
        std::memcpy (at, &packed, sizeof packed);
        return;
      }
      case PixelFormat::rg8snorm: {
        const std::int8_t packed =
          static_cast<std::int8_t> (std::clamp (value, -1.0f, 1.0f) * 127.0f);
        std::memcpy (at, &packed, sizeof packed);
        return;
      }
      }
    }

    // The stored number, in whatever unit the column already holds. This is
    // the representation, not a conversion: nothing is rescaled on the way to
    // a pixel, because a texture lane has no unit to rescale into.
    template <typename Value>
    float stored_scalar (const Value& value) {
      if constexpr (mp_units::QuantityPoint<Value>)
        return value.quantity_from_zero ().numerical_value_in (Value::unit);
      else
        return value.numerical_value_in (Value::unit);
    }

    template <typename Value>
    Vec3 stored_vector (const Value& value) {
      return value.numerical_value_in (Value::unit);
    }
  }

  class TexturePixels {
  public:
    using Writer = void (*) (const void* source,
                             PixelFormat format,
                             std::byte* destination);

    TexturePixels () = default;

    TexturePixels (std::size_t width,
                   std::size_t height,
                   PixelFormat format,
                   const void* source,
                   Writer writer)
        : m_width (width), m_height (height), m_format (format),
          m_source (source), m_writer (writer) {}

    // An empty source means there is nothing to upload, which is how a
    // caller says "leave this overlay off" without inventing a null span.
    bool empty () const noexcept {
      return m_writer == nullptr;
    }
    std::size_t width () const noexcept {
      return m_width;
    }
    std::size_t height () const noexcept {
      return m_height;
    }
    PixelFormat format () const noexcept {
      return m_format;
    }
    std::size_t byte_size () const noexcept {
      return m_width * m_height * bytes_per_pixel (m_format);
    }

    void write_into (std::byte* destination) const {
      m_writer (m_source, m_format, destination);
    }

  private:
    std::size_t m_width = 0;
    std::size_t m_height = 0;
    PixelFormat m_format = PixelFormat::r32f;
    const void* m_source = nullptr;
    Writer m_writer = nullptr;
  };

  // Materialize a source and hand back one vector per channel, in the
  // representation the pixels actually carry.
  inline std::vector<std::vector<float>>
  decode_channels (const TexturePixels& pixels) {
    std::vector<std::vector<float>> lanes (channels_in (pixels.format ()));
    if (pixels.empty ())
      return lanes;
    std::vector<std::byte> bytes (pixels.byte_size ());
    pixels.write_into (bytes.data ());
    const std::size_t stride = bytes_per_pixel (pixels.format ());
    const std::size_t lane = channel_bytes (pixels.format ());
    const std::size_t count = pixels.width () * pixels.height ();
    for (std::size_t channel = 0; channel < lanes.size (); ++channel) {
      lanes[channel].resize (count);
      for (std::size_t pixel = 0; pixel < count; ++pixel) {
        const std::byte* at = bytes.data () + pixel * stride + channel * lane;
        if (pixels.format () == PixelFormat::r32f ||
            pixels.format () == PixelFormat::rg32f) {
          float value = 0.0f;
          std::memcpy (&value, at, sizeof value);
          lanes[channel][pixel] = value;
        } else if (pixels.format () == PixelFormat::rg16snorm) {
          std::int16_t packed = 0;
          std::memcpy (&packed, at, sizeof packed);
          lanes[channel][pixel] = static_cast<float> (packed) / 32767.0f;
        } else if (pixels.format () == PixelFormat::rgba8unorm) {
          std::uint8_t packed = 0;
          std::memcpy (&packed, at, sizeof packed);
          lanes[channel][pixel] = static_cast<float> (packed) / 255.0f;
        } else if (pixels.format () == PixelFormat::rg8snorm) {
          std::int8_t packed = 0;
          std::memcpy (&packed, at, sizeof packed);
          lanes[channel][pixel] = static_cast<float> (packed) / 127.0f;
        } else {
          std::uint16_t packed = 0;
          std::memcpy (&packed, at, sizeof packed);
          lanes[channel][pixel] = half_to_float (packed);
        }
      }
    }
    return lanes;
  }

  namespace detail {
    template <typename Bundle, auto... QS>
    void write_scalar_channels (const void* source,
                                PixelFormat format,
                                std::byte* destination) {
      const Bundle& bundle = *static_cast<const Bundle*> (source);
      const std::size_t stride = bytes_per_pixel (format);
      const std::size_t lane = channel_bytes (format);
      std::size_t channel = 0;
      (
        [&] {
          const auto& column = spatial::get<QS> (bundle);
          for (std::size_t pixel = 0; pixel < column.size (); ++pixel)
            write_channel (destination + pixel * stride + channel * lane,
                           format,
                           stored_scalar (column[pixel]));
          ++channel;
        }(),
        ...);
    }

    template <typename Bundle, auto QS>
    void write_xz_channels (const void* source,
                            PixelFormat format,
                            std::byte* destination) {
      const Bundle& bundle = *static_cast<const Bundle*> (source);
      const auto& column = spatial::get<QS> (bundle);
      const std::size_t stride = bytes_per_pixel (format);
      const std::size_t lane = channel_bytes (format);
      for (std::size_t pixel = 0; pixel < column.size (); ++pixel) {
        const Vec3 value = stored_vector (column[pixel]);
        write_channel (destination + pixel * stride, format, value[0]);
        write_channel (destination + pixel * stride + lane, format, value[2]);
      }
    }
  }

  // One channel per named section, in the order named. The bundle is borrowed
  // and must outlive the upload.
  template <auto... QS, typename Bundle>
  TexturePixels texture_pixels (const Bundle& bundle, PixelFormat format) {
    if (sizeof...(QS) != channels_in (format))
      throw std::invalid_argument (
        "texture format wants a different number of sections");
    return TexturePixels (bundle.domain ().width (),
                          bundle.domain ().height (),
                          format,
                          &bundle,
                          &detail::write_scalar_channels<Bundle, QS...>);
  }

  // A planar section: one vector-valued quantity whose horizontal components
  // become the two channels. The vertical one is left out because every
  // consumer of these reconstructs it in the shader.
  template <auto QS, typename Bundle>
  TexturePixels planar_texture_pixels (const Bundle& bundle,
                                       PixelFormat format) {
    if (channels_in (format) != 2)
      throw std::invalid_argument ("a planar section needs two channels");
    return TexturePixels (bundle.domain ().width (),
                          bundle.domain ().height (),
                          format,
                          &bundle,
                          &detail::write_xz_channels<Bundle, QS>);
  }
}

#endif
