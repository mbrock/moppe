#include <moppe/render/ground_detail.hh>

#include <tests/test.hh>

#include <cmath>
#include <cstdlib>

namespace {
  using namespace moppe;
  using render::GroundDetail;

  constexpr int detail_size = GroundDetail::size;

  int lane (const GroundDetail& detail, int x, int y, int channel) {
    const int wx = (x + detail_size) % detail_size;
    const int wy = (y + detail_size) % detail_size;
    return detail.pixels[(std::size_t (wy) * detail_size + wx) * 4 + channel];
  }
}

MOPPE_TEST (ground_detail_is_the_same_sheet_every_time) {
  const GroundDetail a = render::make_ground_detail ();
  const GroundDetail b = render::make_ground_detail ();
  MOPPE_CHECK (a.pixels.size () == std::size_t (detail_size) * detail_size * 4);
  MOPPE_CHECK (a.pixels == b.pixels);
}

MOPPE_TEST (ground_detail_tiles_without_a_seam) {
  const GroundDetail detail = render::make_ground_detail ();
  // The grain and its slopes step no further across the sheet's edge
  // than they do anywhere inside it.
  for (int channel = 0; channel < 3; ++channel) {
    int inside = 0, across = 0;
    for (int i = 0; i < detail_size; ++i) {
      for (int x = 1; x < detail_size; ++x)
        inside = std::max (inside,
                           std::abs (lane (detail, x, i, channel) -
                                     lane (detail, x - 1, i, channel)));
      across = std::max (across,
                         std::abs (lane (detail, 0, i, channel) -
                                   lane (detail, -1, i, channel)));
      across = std::max (across,
                         std::abs (lane (detail, i, 0, channel) -
                                   lane (detail, i, -1, channel)));
    }
    MOPPE_CHECK (across <= inside);
  }
}

MOPPE_TEST (ground_detail_slopes_are_the_grain_s_own) {
  const GroundDetail detail = render::make_ground_detail ();
  // Each lane is centred, and the slope lanes fill a quarter of their
  // range at one root-mean-square.
  double grain = 0.0, squares = 0.0;
  for (int y = 0; y < detail_size; ++y)
    for (int x = 0; x < detail_size; ++x) {
      grain += lane (detail, x, y, 0);
      for (int channel = 1; channel < 3; ++channel) {
        const double slope = (lane (detail, x, y, channel) - 127.5) / 127.5;
        squares += slope * slope;
      }
    }
  const double count = double (detail_size) * detail_size;
  MOPPE_CHECK_NEAR (grain / count, 127.5, 2.0);
  MOPPE_CHECK_NEAR (
    std::sqrt (squares / (2.0 * count)), 1.0 / GroundDetail::slope_range, 0.01);

  // The stored slope along u rises and falls with the grain's own
  // difference along u: the two are one relief.
  double agreement = 0.0, slope_power = 0.0, difference_power = 0.0;
  for (int y = 0; y < detail_size; ++y)
    for (int x = 0; x < detail_size; ++x) {
      const double slope = lane (detail, x, y, 1) - 127.5;
      const double difference =
        lane (detail, x + 2, y, 0) - lane (detail, x - 2, y, 0);
      agreement += slope * difference;
      slope_power += slope * slope;
      difference_power += difference * difference;
    }
  MOPPE_CHECK (agreement / std::sqrt (slope_power * difference_power) > 0.5);
}

MOPPE_TEST (ground_detail_stones_are_flat_faces_between_dark_seams) {
  // A stone's flat top is the inner part of its cell; its shoulders and
  // the seams share the rest.
  const GroundDetail detail = render::make_ground_detail ();
  const int floor_byte = int (GroundDetail::stone_floor * 255.0f);
  std::size_t faces = 0, seams = 0;
  for (int y = 0; y < detail_size; ++y)
    for (int x = 0; x < detail_size; ++x) {
      const int stone = lane (detail, x, y, 3);
      faces += stone >= floor_byte - 1;
      seams += stone < 16;
    }
  const double count = double (detail_size) * detail_size;
  MOPPE_CHECK (double (faces) / count > 0.2);
  MOPPE_CHECK (double (seams) / count > 0.03);
  MOPPE_CHECK (double (seams) / count < 0.3);
}
