#pragma once

#include <moppe/nhal/nhal.hh>
#include <moppe/parallel.hh>
#include <moppe/render/texture_pixels.hh>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace moppe::nhal::detail {
  // A coarse texel covers exactly the same normalized UV interval as its
  // fine source texels. The original terrain half-texel offset then works
  // for both grids, including the interpolated interval across the seam.
  inline std::vector<std::byte> field_pixels (
    const render::TexturePixels& pixels, unsigned width, unsigned height) {
    const unsigned w = pixels.width (), h = pixels.height ();
    const bool half = pixels.format () == render::PixelFormat::rgba16f;
    const size_t stride = half ? 8 : 4;
    std::vector<std::byte> source (pixels.byte_size ());
    pixels.write_into (source.data ());
    if (width != w || height != h) {
      std::vector<std::byte> output (size_t (width) * height * stride);
      parallel_for (height, 16, [&] (size_t first, size_t last) {
        for (unsigned z = first; z < last; ++z)
          for (unsigned x = 0; x < width; ++x) {
            float sum[4] {};
            // Integer overlap lengths avoid shifting odd resolutions.
            const uint64_t x0 = uint64_t (x) * w, x1 = x0 + w;
            const uint64_t z0 = uint64_t (z) * h, z1 = z0 + h;
            for (uint64_t j = z0 / height; j * height < z1; ++j)
              for (uint64_t i = x0 / width; i * width < x1; ++i) {
                const float area = float ((std::min (x1, (i + 1) * width) -
                                           std::max (x0, i * width)) *
                                          (std::min (z1, (j + 1) * height) -
                                           std::max (z0, j * height))) /
                                   (float (w) * h);
                const auto* from = source.data () + (j * w + i) * stride;
                for (unsigned c = 0; c < 4; ++c) {
                  if (half) {
                    uint16_t bits;
                    std::memcpy (&bits, from + 2 * c, sizeof bits);
                    sum[c] += area * render::half_to_float (bits);
                  } else
                    sum[c] += area * std::to_integer<unsigned> (from[c]);
                }
              }
            auto* to = output.data () + (size_t (z) * width + x) * stride;
            for (unsigned c = 0; c < 4; ++c) {
              if (half) {
                const auto bits = render::float_to_half (sum[c]);
                std::memcpy (to + 2 * c, &bits, sizeof bits);
              } else
                to[c] = std::byte (std::clamp (int (sum[c] + 0.5f), 0, 255));
            }
          }
      });
      source = std::move (output);
    }
    return source;
  }

  inline Texture field_texture (Device& device,
                                const render::TexturePixels& pixels,
                                Format format,
                                const char* label,
                                unsigned limit = 2048) {
    const auto width = std::min (unsigned (pixels.width ()), limit);
    const auto height = std::min (unsigned (pixels.height ()), limit);
    auto source = field_pixels (pixels, width, height);
    auto texture = device.create_texture (
      { width, height, format, usage_sampled, 1, label });
    device.write_texture (texture, source);
    return texture;
  }
}
