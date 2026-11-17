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

    proportion_t detail_unit (std::uint32_t h) {
      return float (h & 0xffffffu) / 16777216.0f * proportion[one];
    }

    int detail_wrap (int i, int period) {
      return ((i % period) + period) % period;
    }

    // A smooth step between two distances in cells.
    blend_weight_t detail_smoothstep (float low, float high, float x) {
      const float t = std::clamp ((x - low) / (high - low), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t) * blend_weight[one];
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
      noise_signal_t value;
      grade_t du, dv;
    };

    // Gradient noise that repeats every `period` cells, and its slope a
    // cell, under a quintic fade so the slope has no creases, at a place
    // given as shares of the tile. Inside, the lattice's arithmetic is
    // the noise's own, in cells.
    DetailRelief detail_noise (proportion_t along_u,
                               proportion_t along_v,
                               int period,
                               std::uint32_t lane) {
      const float cells = float (period);
      const float x = along_u.numerical_value_in (one) * cells;
      const float y = along_v.numerical_value_in (one) * cells;
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
      relief.value =
        (a + ux * (b - a) + uy * (c - a) + ux * uy * k) * noise_signal[one];
      relief.du = (g00[0] + ux * (g10[0] - g00[0]) + uy * (g01[0] - g00[0]) +
                   ux * uy * (g00[0] - g10[0] - g01[0] + g11[0]) +
                   fade_slope (tx) * (b - a + uy * k)) *
                  grade[one];
      relief.dv = (g00[1] + ux * (g10[1] - g00[1]) + uy * (g01[1] - g00[1]) +
                   ux * uy * (g00[1] - g10[1] - g01[1] + g11[1]) +
                   fade_slope (ty) * (c - a + ux * k)) *
                  grade[one];
      return relief;
    }

    // The stone mosaic at a point in cells: the nearest stone's tone,
    // falling away over the stone's shoulder to nothing at the seam it
    // shares with its neighbours. The place is given as shares of the
    // tile; the mosaic's geometry inside is in cells.
    proportion_t detail_stone (proportion_t along_u, proportion_t along_v) {
      constexpr int cells = GroundDetail::cells;
      const float x = along_u.numerical_value_in (one) * float (cells);
      const float y = along_v.numerical_value_in (one) * float (cells);
      const int cx = int (std::floor (x)), cy = int (std::floor (y));
      // A stone's centre in cells, and the tone of its face.
      struct Site {
        std::array<float, 2> at;
        proportion_t tone;
      };
      const auto jitter = [] (std::uint32_t h) {
        return 0.9f * (detail_unit (h).numerical_value_in (one) - 0.5f);
      };
      const auto site = [&] (int dx, int dy) {
        const std::uint32_t wx = std::uint32_t (detail_wrap (cx + dx, cells));
        const std::uint32_t wy = std::uint32_t (detail_wrap (cy + dy, cells));
        return Site {
          { float (cx + dx) + 0.5f + jitter (detail_hash (wx, wy, 101u)),
            float (cy + dy) + 0.5f + jitter (detail_hash (wx, wy, 103u)) },
          detail_unit (detail_hash (wx, wy, 107u)),
        };
      };
      Site nearest {};
      float least = 1.0e9f;
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
          const Site s = site (dx, dy);
          const float distance =
            (s.at[0] - x) * (s.at[0] - x) + (s.at[1] - y) * (s.at[1] - y);
          if (distance < least) {
            least = distance;
            nearest = s;
          }
        }
      // The seam is the nearest of the bisectors with every other site.
      float seam = 1.0e9f;
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
          const Site s = site (dx, dy);
          const float ax = s.at[0] - nearest.at[0];
          const float ay = s.at[1] - nearest.at[1];
          const float span = std::sqrt (ax * ax + ay * ay);
          if (span < 1.0e-4f)
            continue;
          const float mx = 0.5f * (s.at[0] + nearest.at[0]) - x;
          const float my = 0.5f * (s.at[1] + nearest.at[1]) - y;
          seam = std::min (seam, (mx * ax + my * ay) / span);
        }
      const proportion_t darkest = GroundDetail::stone_floor;
      const proportion_t tone =
        darkest + (1.0f - darkest.numerical_value_in (one)) * nearest.tone;
      const blend_weight_t shoulder = detail_smoothstep (0.015f, 0.22f, seam);
      return tone * shoulder.numerical_value_in (one);
    }

    std::uint8_t detail_byte (float unit) {
      return std::uint8_t (
        std::lround (std::clamp (unit, 0.0f, 1.0f) * 255.0f));
    }
  }

  GroundDetail make_ground_detail () {
    constexpr int size = GroundDetail::size;
    constexpr std::array<int, 4> periods { 8, 16, 32, 64 };
    constexpr std::array<scale_factor_t, 4> weights {
      1.0f * scale_factor[one],
      0.75f * scale_factor[one],
      0.55f * scale_factor[one],
      0.4f * scale_factor[one],
    };
    const std::size_t count = std::size_t (size) * size;
    std::vector<noise_signal_t> grain (count);
    std::vector<grade_t> slope_u (count), slope_v (count);
    // A texel's centre as shares of the tile.
    const auto across = [] (int texel) {
      return (float (texel) + 0.5f) / float (size) * proportion[one];
    };

    // The sums run in double; a texel's lanes join them as stored.
    double sum = 0.0, squares = 0.0, steepness = 0.0;
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const std::size_t at = std::size_t (y) * size + x;
        const proportion_t u = across (x), v = across (y);
        for (std::size_t octave = 0; octave < periods.size (); ++octave) {
          const DetailRelief relief =
            detail_noise (u, v, periods[octave], std::uint32_t (octave));
          grain[at] += weights[octave].numerical_value_in (one) * relief.value;
          slope_u[at] += relief.du;
          slope_v[at] += relief.dv;
        }
        const double g = grain[at].numerical_value_in (one);
        const double su = slope_u[at].numerical_value_in (one);
        const double sv = slope_v[at].numerical_value_in (one);
        sum += g;
        squares += g * g;
        steepness += su * su + sv * sv;
      }
    const double mean = sum / double (count);
    const noise_signal_t middle = float (mean) * noise_signal[one];
    const noise_signal_t deviation =
      float (std::sqrt (
        std::max (squares / double (count) - mean * mean, 1.0e-12))) *
      noise_signal[one];
    const grade_t rms =
      float (std::sqrt (std::max (steepness / double (2 * count), 1.0e-12))) *
      grade[one];

    GroundDetail detail;
    detail.pixels.resize (count * 4);
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const std::size_t at = std::size_t (y) * size + x;
        std::uint8_t* pixel = detail.pixels.data () + at * 4;
        // Two and a half deviations either side of the mean fill the lane.
        pixel[0] = detail_byte (
          0.5f +
          ((grain[at] - middle) / (5.0f * deviation)).numerical_value_in (one));
        const grade_t unit =
          GroundDetail::slope_range.numerical_value_in (one) * rms;
        pixel[1] = detail_byte (
          0.5f + (0.5f * slope_u[at] / unit).numerical_value_in (one));
        pixel[2] = detail_byte (
          0.5f + (0.5f * slope_v[at] / unit).numerical_value_in (one));
        pixel[3] = detail_byte (
          detail_stone (across (x), across (y)).numerical_value_in (one));
      }
    return detail;
  }
}
