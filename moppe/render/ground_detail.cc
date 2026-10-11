#include <moppe/render/ground_detail.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace moppe::render {
  namespace {
    std::uint32_t
    detail_hash (std::uint32_t x, std::uint32_t y, std::uint32_t lane) {
      std::uint32_t h = x * 0x9e3779b9u ^ y * 0x85ebca6bu ^ lane * 0xc2b2ae35u;
      h ^= h >> 16;
      h *= 0x7feb352du;
      h ^= h >> 15;
      h *= 0x846ca68bu;
      h ^= h >> 16;
      return h;
    }

    float detail_unit (std::uint32_t h) {
      return float (h & 0xffffffu) / 16777216.0f;
    }

    int detail_wrap (int i, int period) {
      return ((i % period) + period) % period;
    }

    float detail_smoothstep (float low, float high, float x) {
      const float t = std::clamp ((x - low) / (high - low), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    // Sixteen directions round the circle, written out so that no
    // platform's trigonometry has a say in the sheet.
    constexpr float detail_c = 0.9238795f, detail_s = 0.3826834f,
                    detail_d = 0.7071068f;
    constexpr std::array<std::array<float, 2>, 16> detail_directions { {
      { 1.0f, 0.0f },
      { detail_c, detail_s },
      { detail_d, detail_d },
      { detail_s, detail_c },
      { 0.0f, 1.0f },
      { -detail_s, detail_c },
      { -detail_d, detail_d },
      { -detail_c, detail_s },
      { -1.0f, 0.0f },
      { -detail_c, -detail_s },
      { -detail_d, -detail_d },
      { -detail_s, -detail_c },
      { 0.0f, -1.0f },
      { detail_s, -detail_c },
      { detail_d, -detail_d },
      { detail_c, -detail_s },
    } };

    struct DetailRelief {
      float value, du, dv;
    };

    // Gradient noise that repeats every `period` cells, and its slope a
    // cell, under a quintic fade so the slope has no creases.
    DetailRelief
    detail_noise (float x, float y, int period, std::uint32_t lane) {
      const float fx = std::floor (x), fy = std::floor (y);
      const int ix = int (fx), iy = int (fy);
      const float tx = x - fx, ty = y - fy;
      const auto corner = [&] (int dx, int dy) {
        const std::uint32_t h =
          detail_hash (std::uint32_t (detail_wrap (ix + dx, period)),
                       std::uint32_t (detail_wrap (iy + dy, period)),
                       lane);
        return detail_directions[h & 15u];
      };
      const auto g00 = corner (0, 0), g10 = corner (1, 0);
      const auto g01 = corner (0, 1), g11 = corner (1, 1);
      const float a = g00[0] * tx + g00[1] * ty;
      const float b = g10[0] * (tx - 1.0f) + g10[1] * ty;
      const float c = g01[0] * tx + g01[1] * (ty - 1.0f);
      const float d = g11[0] * (tx - 1.0f) + g11[1] * (ty - 1.0f);
      const auto fade = [] (float t) {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
      };
      const auto fade_slope = [] (float t) {
        return 30.0f * t * t * (t * (t - 2.0f) + 1.0f);
      };
      const float ux = fade (tx), uy = fade (ty);
      const float k = a - b - c + d;
      DetailRelief relief;
      relief.value = a + ux * (b - a) + uy * (c - a) + ux * uy * k;
      relief.du = g00[0] + ux * (g10[0] - g00[0]) + uy * (g01[0] - g00[0]) +
                  ux * uy * (g00[0] - g10[0] - g01[0] + g11[0]) +
                  fade_slope (tx) * (b - a + uy * k);
      relief.dv = g00[1] + ux * (g10[1] - g00[1]) + uy * (g01[1] - g00[1]) +
                  ux * uy * (g00[1] - g10[1] - g01[1] + g11[1]) +
                  fade_slope (ty) * (c - a + ux * k);
      return relief;
    }

    // The stone mosaic at a point in cells: the nearest stone's tone,
    // falling away over the stone's shoulder to nothing at the seam it
    // shares with its neighbours.
    float detail_stone (float x, float y) {
      constexpr int cells = GroundDetail::cells;
      const int cx = int (std::floor (x)), cy = int (std::floor (y));
      const auto site = [&] (int dx, int dy) {
        const std::uint32_t wx = std::uint32_t (detail_wrap (cx + dx, cells));
        const std::uint32_t wy = std::uint32_t (detail_wrap (cy + dy, cells));
        return std::array<float, 3> {
          float (cx + dx) + 0.5f +
            0.9f * (detail_unit (detail_hash (wx, wy, 101u)) - 0.5f),
          float (cy + dy) + 0.5f +
            0.9f * (detail_unit (detail_hash (wx, wy, 103u)) - 0.5f),
          detail_unit (detail_hash (wx, wy, 107u)),
        };
      };
      std::array<float, 3> nearest {};
      float least = 1.0e9f;
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
          const auto s = site (dx, dy);
          const float distance =
            (s[0] - x) * (s[0] - x) + (s[1] - y) * (s[1] - y);
          if (distance < least) {
            least = distance;
            nearest = s;
          }
        }
      // The seam is the nearest of the bisectors with every other site.
      float seam = 1.0e9f;
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
          const auto s = site (dx, dy);
          const float ax = s[0] - nearest[0], ay = s[1] - nearest[1];
          const float span = std::sqrt (ax * ax + ay * ay);
          if (span < 1.0e-4f)
            continue;
          const float mx = 0.5f * (s[0] + nearest[0]) - x;
          const float my = 0.5f * (s[1] + nearest[1]) - y;
          seam = std::min (seam, (mx * ax + my * ay) / span);
        }
      const float tone = GroundDetail::stone_floor +
                         (1.0f - GroundDetail::stone_floor) * nearest[2];
      return tone * detail_smoothstep (0.015f, 0.22f, seam);
    }

    std::uint8_t detail_byte (float unit) {
      return std::uint8_t (
        std::lround (std::clamp (unit, 0.0f, 1.0f) * 255.0f));
    }
  }

  GroundDetail make_ground_detail () {
    constexpr int size = GroundDetail::size;
    constexpr std::array<int, 4> periods { 8, 16, 32, 64 };
    constexpr std::array<float, 4> weights { 1.0f, 0.75f, 0.55f, 0.4f };
    const std::size_t count = std::size_t (size) * size;
    std::vector<float> grain (count), slope_u (count), slope_v (count);

    double sum = 0.0, squares = 0.0, steepness = 0.0;
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const std::size_t at = std::size_t (y) * size + x;
        const float u = (float (x) + 0.5f) / float (size);
        const float v = (float (y) + 0.5f) / float (size);
        for (std::size_t octave = 0; octave < periods.size (); ++octave) {
          const float cells = float (periods[octave]);
          const DetailRelief relief = detail_noise (
            u * cells, v * cells, periods[octave], std::uint32_t (octave));
          grain[at] += weights[octave] * relief.value;
          slope_u[at] += relief.du;
          slope_v[at] += relief.dv;
        }
        sum += grain[at];
        squares += double (grain[at]) * grain[at];
        steepness += double (slope_u[at]) * slope_u[at] +
                     double (slope_v[at]) * slope_v[at];
      }
    const double mean = sum / double (count);
    const float deviation = float (
      std::sqrt (std::max (squares / double (count) - mean * mean, 1.0e-12)));
    const float rms =
      float (std::sqrt (std::max (steepness / double (2 * count), 1.0e-12)));

    GroundDetail detail;
    detail.pixels.resize (count * 4);
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const std::size_t at = std::size_t (y) * size + x;
        std::uint8_t* pixel = detail.pixels.data () + at * 4;
        // Two and a half deviations either side of the mean fill the lane.
        pixel[0] =
          detail_byte (0.5f + (grain[at] - float (mean)) / (5.0f * deviation));
        const float unit = GroundDetail::slope_range * rms;
        pixel[1] = detail_byte (0.5f + 0.5f * slope_u[at] / unit);
        pixel[2] = detail_byte (0.5f + 0.5f * slope_v[at] / unit);
        pixel[3] = detail_byte (detail_stone (
          (float (x) + 0.5f) / float (size) * float (GroundDetail::cells),
          (float (y) + 0.5f) / float (size) * float (GroundDetail::cells)));
      }
    return detail;
  }
}
