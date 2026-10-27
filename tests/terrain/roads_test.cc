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

MOPPE_TEST (roads_reach_the_sea_round_lakes_and_mires_rather_than_through) {
  const ElevationMap terrain = lake_country ();
  const FloodField flood = analyze_standing_water (terrain, sea_level);
  const DrainageGraph drainage = analyze_drainage (terrain);
  // Two patches of mire straddle the trailhead's way north and south,
  // leaving narrow dry ground either side.
  std::vector<float> mire (side * side, 0.0f);
  for (std::size_t z = 0; z < side; ++z)
    for (std::size_t x = 15; x <= 25; ++x)
      if ((z >= 30 && z <= 45) || (z >= 55 && z <= 70))
        mire[z * side + x] = 1.0f;
  const RoadNetwork network =
    plan_roads (terrain.domain (),
                elevations (terrain),
                flood,
                drainage,
                { .mire = mire },
                1.0e9f * mp_units::si::metre * mp_units::si::metre,
                CellIndex { static_cast<std::uint32_t> (50 * side + 20) },
                sea_level,
                7);

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
  float boggiest = 0.0f;
  for (const Road& road : network.roads) {
    if (road.kind != Road::Kind::Road)
      continue;
    for (const RoadPoint& point : road.points) {
      const auto cell = [&] (float m) {
        return static_cast<std::size_t> (
                 std::lround (wrap_coordinate (m, period) / spacing)) %
               side;
      };
      const std::size_t at = cell (point.z_m) * side + cell (point.x_m);
      deepest = std::max (deepest, flood.water_depth_m (at));
      boggiest = std::max (boggiest, mire[at]);
    }
  }
  MOPPE_CHECK (deepest < 3.0f);
  MOPPE_CHECK (boggiest < 0.5f);

  // Every way carries some journey and is no older than the places it
  // joins; the trailhead is still in use.
  float oldest = 0.0f;
  for (const RoadPlace& place : network.places)
    oldest = std::max (oldest, place.founded_years_ago);
  MOPPE_CHECK (network.places.front ().abandoned_years_ago == 0.0f);
  for (const Road& road : network.roads) {
    MOPPE_CHECK (road.traffic > 0);
    MOPPE_CHECK (road.age_years > 0.0f && road.age_years <= oldest);
  }
}

namespace {
  // A field 60 m above the sea with a sharp ridge across it, 3 m high and
  // 30 m through: a 20% climb each side.
  ElevationMap ridge_country () {
    std::vector<float> heights (side * side);
    for (std::size_t z = 0; z < side; ++z)
      for (std::size_t x = 0; x < side; ++x) {
        const float from_ridge =
          std::fabs (static_cast<float> (x) * 5.0f - 250.0f);
        heights[z * side + x] =
          60.0f + std::max (0.0f, 3.0f - 0.2f * from_ridge);
      }
    return make_elevation_map (
      TerrainDomain (
        side, side, 5.0f * mp_units::si::metre, 5.0f * mp_units::si::metre),
      heights);
  }
}

MOPPE_TEST (built_roads_hold_their_grade_and_meet_where_they_join) {
  const ElevationMap terrain = ridge_country ();
  const FloodField flood = analyze_standing_water (terrain, sea_level);
  const DrainageGraph drainage = analyze_drainage (terrain);
  RoadNetwork network;
  // Over the ridge, west to east, and a branch leaving it on the ridge's
  // flank northward.
  Road over { .kind = Road::Kind::Road };
  for (float x = 100.0f; x <= 400.0f; x += 4.0f)
    over.points.push_back ({ x, 250.0f });
  Road branch { .kind = Road::Kind::Road };
  for (float z = 249.0f; z >= 100.0f; z -= 4.0f)
    branch.points.push_back ({ 236.0f, z });
  network.roads = { over, branch };
  RoadFormation parameters;
  parameters.culvert_area_m2 = 1.0e12f;
  const RoadFormationResult built = form_roads (terrain.domain (),
                                                elevations (terrain),
                                                network,
                                                flood,
                                                drainage,
                                                parameters);

  const Road& formed = network.roads[0];
  float steepest = 0.0f;
  for (std::size_t i = 1; i < formed.points.size (); ++i)
    steepest = std::max (
      steepest, std::fabs (formed.heights[i] - formed.heights[i - 1]) / 4.0f);
  MOPPE_CHECK (steepest <= parameters.maximum_grade + 1e-3f);
  MOPPE_CHECK (built.steep_points == 0);
  // It cut through the crest rather than climbing it.
  MOPPE_CHECK (formed.heights[formed.points.size () / 2] < 62.5f);
  MOPPE_CHECK (built.cut_m3 > 0.0);

  // The branch starts at the height of the road it leaves.
  const std::size_t at = static_cast<std::size_t> ((236.0f - 100.0f) / 4.0f);
  MOPPE_CHECK_NEAR (
    network.roads[1].heights.front (), formed.heights[at], 0.05f);

  // The tread's crown is what the formed ground holds on the line, and
  // ground well away from both roads is left as it was.
  const auto height = [&] (std::size_t x, std::size_t z) {
    return surface_elevation_value (built.heights[z * side + x]);
  };
  MOPPE_CHECK_NEAR (height (52, 50), formed.heights[40], 0.05f);
  MOPPE_CHECK (height (50, 90) ==
               surface_elevation_value (elevations (terrain)[90 * side + 50]));
  MOPPE_CHECK (built.influence[50 * side + 70] > 0.99f);
  MOPPE_CHECK (built.influence[80 * side + 70] == 0.0f);
}
