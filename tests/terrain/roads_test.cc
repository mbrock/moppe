#include <moppe/terrain/roads.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace moppe::terrain;

namespace {
  // Two kilometres of level country 10 m above the sea. The trailhead lies
  // between two long deep lakes, one each side of it (the second across the
  // torus's seam), and the sea lies beyond them both: the short ways there
  // cross a lake, and a road must go a long way round by the narrow gap at
  // the lakes' ends.
  constexpr std::size_t side = 100;
  constexpr float spacing = 20.0f;
  constexpr float sea_level = 50.0f;

  // Shelving gently, so that only the depth of the water keeps a road out.
  float strip_depth (float across, float z) {
    const float along = std::min (z - 3.0f, 97.0f - z);
    return along < 0.0f
             ? 0.0f
             : 6.0f * std::clamp ((10.0f - across) / 3.0f, 0.0f, 1.0f) *
                 std::min (1.0f, along / 3.0f);
  }

  ElevationMap lake_country () {
    std::vector<float> heights (side * side);
    for (std::size_t z = 0; z < side; ++z)
      for (std::size_t x = 0; x < side; ++x) {
        const float fx = static_cast<float> (x);
        const float fz = static_cast<float> (z);
        const float depth =
          std::max (strip_depth (std::fabs (fx - 40.0f), fz),
                    strip_depth (std::min (fx, 100.0f - fx), fz));
        heights[z * side + x] = x >= 62 && x <= 78 ? 30.0f : 60.0f - depth;
      }
    return make_elevation_map (TerrainDomain (side,
                                              side,
                                              spacing * mp_units::si::metre,
                                              spacing * mp_units::si::metre),
                               heights);
  }
}

MOPPE_TEST (roads_reach_the_sea_round_a_lake_rather_than_through_it) {
  const ElevationMap terrain = lake_country ();
  const FloodField flood = analyze_standing_water (terrain, sea_level);
  const DrainageGraph drainage = analyze_drainage (terrain);
  const RoadNetwork network =
    plan_roads (terrain.domain (),
                elevations (terrain),
                flood,
                drainage,
                1.0e9f * mp_units::si::metre * mp_units::si::metre,
                CellIndex { static_cast<std::uint32_t> (50 * side + 20) },
                sea_level);

  MOPPE_CHECK (!network.places.empty ());
  MOPPE_CHECK (network.places.front ().kind == RoadPlace::Kind::Trailhead);
  bool landing = false;
  for (const RoadPlace& place : network.places)
    landing = landing || place.kind == RoadPlace::Kind::Landing;
  MOPPE_CHECK (landing);

  // The lakes are six metres deep down their middles; no road may cross
  // more than their shelving edges.
  const float period = side * spacing;
  float deepest = 0.0f;
  for (const Road& road : network.roads)
    for (const RoadPoint& point : road.points) {
      const auto cell = [&] (float m) {
        return static_cast<std::size_t> (
                 std::lround (wrap_coordinate (m, period) / spacing)) %
               side;
      };
      deepest = std::max (
        deepest,
        flood.water_depth_m (cell (point.z_m) * side + cell (point.x_m)));
    }
  MOPPE_CHECK (deepest < 3.0f);
}
