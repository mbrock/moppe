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
  // A field 60 m above the sea with a ridge across it, 24 m high and
  // 240 m through: a 20% climb each side, more than a road may cut or fill
  // its way over within its allowances.
  ElevationMap ridge_country () {
    std::vector<float> heights (side * side);
    for (std::size_t z = 0; z < side; ++z)
      for (std::size_t x = 0; x < side; ++x) {
        const float from_ridge =
          std::fabs (static_cast<float> (x) * 5.0f - 250.0f);
        heights[z * side + x] =
          60.0f + std::max (0.0f, 24.0f - 0.2f * from_ridge);
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
  // It cut through the crest rather than climbing it, deeper than the cut
  // allowed, rather than steepening.
  MOPPE_CHECK (formed.heights[formed.points.size () / 2] <
               84.0f - 0.4f - parameters.maximum_cut);
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

// The exact road surface is the crowned tread over a built road, the
// field past its blend, and it is the same road across the torus's seam.
MOPPE_TEST (road_surface_is_the_crowned_tread_and_the_field_beyond) {
  RoadNetwork network;
  // A road climbing east at 5% across the seam at x = 1000, and a footpath.
  Road road { .kind = Road::Kind::Road };
  for (float x = 960.0f; x <= 1040.0f; x += 4.0f) {
    road.points.push_back ({ x, 300.0f });
    road.heights.push_back (100.0f + 0.05f * (x - 960.0f));
  }
  Road path { .kind = Road::Kind::Footpath };
  for (float z = 500.0f; z <= 560.0f; z += 4.0f) {
    path.points.push_back ({ 200.0f, z });
    path.heights.push_back (80.0f);
  }
  network.roads = { road, path };
  const RoadFormation formation;
  const RoadSurface surface (network, 1000.0f, 1000.0f, formation);
  constexpr float field = 70.0f;

  // On the line, past the seam: x = 1010 is x = 10. A gravel road's crown
  // of loose stone lies on the formed tread.
  const float top = 102.5f + RoadSurface::gravel_m;
  MOPPE_CHECK_NEAR (surface.ground (10.0f, 300.0f, field), top, 1e-3f);
  MOPPE_CHECK_NEAR (surface.ground (1010.0f, 300.0f, field), top, 1e-3f);
  // Crowned: two metres off the line it lies a crown's slope lower, the
  // same on either side.
  const float off = top - 2.0f * formation.crown;
  MOPPE_CHECK_NEAR (surface.ground (10.0f, 302.0f, field), off, 1e-3f);
  MOPPE_CHECK_NEAR (surface.ground (10.0f, 298.0f, field), off, 1e-3f);
  // Past the shoulder and the blend it is the field again.
  const float beyond =
    formation.half_width + formation.shoulder + RoadSurface::blend_m + 0.1f;
  MOPPE_CHECK (surface.ground (10.0f, 300.0f + beyond, field) == field);
  // The sample says where along and which way.
  const RoadSample at = surface.nearest (990.0f, 301.0f);
  MOPPE_CHECK (at.found && at.kind == Road::Kind::Road);
  MOPPE_CHECK_NEAR (at.along, 30.0f, 1e-3f);
  MOPPE_CHECK_NEAR (std::fabs (at.across), 1.0f, 1e-3f);
  MOPPE_CHECK_NEAR (at.grade, 0.05f, 1e-4f);
  // A footpath is found but leaves the ground alone.
  MOPPE_CHECK (surface.nearest (200.5f, 530.0f).kind == Road::Kind::Footpath);
  MOPPE_CHECK (surface.ground (200.5f, 530.0f, field) == field);
}

namespace {
  // A field 60 m above the sea, falling gently south, cut by a valley
  // 4 m deep down its middle whose sides climb at one in five.
  // `knoll` metres of a hummock rise on the valley floor beside the river.
  ElevationMap valley_country (float knoll = 0.0f) {
    std::vector<float> heights (side * side);
    for (std::size_t z = 0; z < side; ++z)
      for (std::size_t x = 0; x < side; ++x) {
        const float from_floor =
          std::fabs (static_cast<float> (x) * 5.0f - 250.0f);
        heights[z * side + x] = 60.0f - 0.01f * static_cast<float> (z) * 5.0f -
                                std::max (0.0f, 4.0f - 0.2f * from_floor);
      }
    heights[49 * side + 49] += knoll;
    heights[51 * side + 49] += knoll;
    return make_elevation_map (
      TerrainDomain (
        side, side, 5.0f * mp_units::si::metre, 5.0f * mp_units::si::metre),
      heights);
  }
}

// A road across a river's valley bridges it: the deck stands clear of
// the water on abutments at its banks, the ground beneath is left to the
// river, and every body on the road drives over the deck.
MOPPE_TEST (roads_bridge_the_rivers_they_cross) {
  const ElevationMap terrain = valley_country ();
  const FloodField flood = analyze_standing_water (terrain, sea_level);
  const DrainageGraph drainage = analyze_drainage (terrain);
  const std::size_t floor = 50 * side + 50;
  const float river =
    drainage.contributing_area_at (CellIndex { floor })
      .numerical_value_in (mp_units::si::metre * mp_units::si::metre);
  MOPPE_CHECK (river > 2000.0f);
  RoadNetwork network;
  Road over { .kind = Road::Kind::Road };
  for (float x = 100.0f; x <= 400.0f; x += 4.0f)
    over.points.push_back ({ x, 250.0f });
  network.roads = { over };
  RoadFormation parameters;
  parameters.culvert_area_m2 = 1.0e12f;
  parameters.bridge_area_m2 = 0.5f * river;
  const RoadFormationResult built = form_roads (terrain.domain (),
                                                elevations (terrain),
                                                network,
                                                flood,
                                                drainage,
                                                parameters);

  const Road& formed = network.roads[0];
  MOPPE_CHECK (built.bridges == 1);
  MOPPE_CHECK (formed.bridges.size () == 1);
  const RoadBridge& bridge = formed.bridges.front ();
  const float from = formed.points[bridge.first].x_m;
  const float to = formed.points[bridge.last].x_m;
  // Over the river, not the whole valley's walk across.
  MOPPE_CHECK (from < 250.0f && to > 250.0f);
  MOPPE_CHECK (to - from < parameters.longest_bridge);
  // Clear of the water over the valley floor, which is left as it was.
  const float bed = surface_elevation_value (elevations (terrain)[floor]);
  const std::size_t middle = (bridge.first + bridge.last) / 2;
  MOPPE_CHECK (formed.heights[middle] > bed + parameters.clearance);
  MOPPE_CHECK (surface_elevation_value (built.heights[floor]) == bed);
  // Driven on, the deck: level across to its kerbs, the river below
  // past them.
  const RoadSurface surface (network, 500.0f, 500.0f, parameters);
  const RoadSample at = surface.nearest (250.0f, 251.0f);
  MOPPE_CHECK (at.found && at.bridge);
  MOPPE_CHECK_NEAR (
    surface.ground (250.0f, 251.0f, bed), formed.heights[middle], 0.2f);
  MOPPE_CHECK (surface.ground (250.0f,
                               250.0f + parameters.deck_half_width + 0.5f,
                               bed) == bed);
  // Off the bridge the road is the crowned tread again.
  MOPPE_CHECK (!surface.nearest (130.0f, 250.0f).bridge);
}

// A knoll of the floodplain under a bridge's span stands no higher than
// the deck's underside: it is cut away rather than poking up through the
// planks, where the deck's riders would meet it.
MOPPE_TEST (a_bridge_span_is_cut_clear_of_the_ground_beneath) {
  const ElevationMap terrain = valley_country (6.0f);
  const FloodField flood = analyze_standing_water (terrain, sea_level);
  const DrainageGraph drainage = analyze_drainage (terrain);
  const float river =
    drainage.contributing_area_at (CellIndex { 50 * side + 50 })
      .numerical_value_in (mp_units::si::metre * mp_units::si::metre);
  RoadNetwork network;
  Road over { .kind = Road::Kind::Road };
  for (float x = 100.0f; x <= 400.0f; x += 4.0f)
    over.points.push_back ({ x, 250.0f });
  network.roads = { over };
  RoadFormation parameters;
  parameters.culvert_area_m2 = 1.0e12f;
  parameters.bridge_area_m2 = 0.5f * river;
  const RoadFormationResult built = form_roads (terrain.domain (),
                                                elevations (terrain),
                                                network,
                                                flood,
                                                drainage,
                                                parameters);
  const Road& formed = network.roads[0];
  MOPPE_CHECK (formed.bridges.size () == 1);
  const RoadBridge& bridge = formed.bridges.front ();
  // The knoll stood above the deck, beside its line.
  const std::size_t knoll = 51 * side + 49;
  const RoadSurface surface (network, 500.0f, 500.0f, parameters);
  const RoadSample over_knoll = surface.nearest (245.0f, 255.0f);
  MOPPE_CHECK (over_knoll.bridge);
  MOPPE_CHECK (surface_elevation_value (elevations (terrain)[knoll]) >
               over_knoll.centre_m);
  MOPPE_CHECK (surface_elevation_value (built.heights[knoll]) <=
               over_knoll.centre_m - parameters.deck_depth + 1e-3f);
  // Out past the tread and a lattice step, the floodplain is its own.
  const std::size_t aside = 53 * side + 49;
  MOPPE_CHECK (surface_elevation_value (built.heights[aside]) ==
               surface_elevation_value (elevations (terrain)[aside]));
}

// A road across a small stream crosses it on an embankment over a
// culvert: the tread stands clear of the water, and along the pipe's line
// the ground is left for the water to pass under.
MOPPE_TEST (roads_cross_small_streams_on_embankments_over_culverts) {
  const ElevationMap terrain = valley_country ();
  const FloodField flood = analyze_standing_water (terrain, sea_level);
  const DrainageGraph drainage = analyze_drainage (terrain);
  const std::size_t floor = 50 * side + 50;
  const float stream =
    drainage.contributing_area_at (CellIndex { floor })
      .numerical_value_in (mp_units::si::metre * mp_units::si::metre);
  RoadNetwork network;
  Road over { .kind = Road::Kind::Road };
  for (float x = 100.0f; x <= 400.0f; x += 4.0f)
    over.points.push_back ({ x, 250.0f });
  network.roads = { over };
  RoadFormation parameters;
  parameters.culvert_area_m2 = 0.5f * stream;
  parameters.bridge_area_m2 = 1.0e12f;
  const RoadFormationResult built = form_roads (terrain.domain (),
                                                elevations (terrain),
                                                network,
                                                flood,
                                                drainage,
                                                parameters);

  const Road& formed = network.roads[0];
  MOPPE_CHECK (formed.bridges.empty ());
  MOPPE_CHECK (formed.culverts.size () == 1);
  const RoadCulvert& culvert = formed.culverts.front ();
  MOPPE_CHECK_NEAR (formed.points[culvert.at].x_m, 250.0f, 6.0f);
  const float bed = surface_elevation_value (elevations (terrain)[floor]);
  // Over the pipe and a freeboard, not down at the bed.
  MOPPE_CHECK (formed.heights[culvert.at] >=
               bed + culvert.diameter_m + parameters.freeboard - 0.05f);
  MOPPE_CHECK (built.steep_points == 0);
  // The pipe's line is left to the water; beside it the embankment
  // stands under the tread.
  MOPPE_CHECK (surface_elevation_value (built.heights[floor]) == bed);
  const std::size_t beside =
    50 * side + static_cast<std::size_t> (
                  std::lround ((formed.points[culvert.at].x_m + 7.0f) / 5.0f));
  MOPPE_CHECK (surface_elevation_value (built.heights[beside]) >
               surface_elevation_value (elevations (terrain)[beside]) + 0.3f);
}

// Round a bend on a grade the tread is one surface: both segments give a
// point on the mitre between them the same height, where measuring along
// each segment square to it would leave a step on the inside of the bend.
MOPPE_TEST (road_surface_keeps_its_height_round_a_bend) {
  RoadNetwork network;
  Road road { .kind = Road::Kind::Road };
  // East at 10% up to (500, 500), then on at thirty degrees to it.
  float height = 100.0f;
  for (float x = 440.0f; x <= 500.0f; x += 4.0f) {
    road.points.push_back ({ x, 500.0f });
    road.heights.push_back (height);
    height += 0.4f;
  }
  for (int k = 1; k <= 15; ++k) {
    road.points.push_back (
      { 500.0f + 4.0f * k * 0.8660254f, 500.0f + 4.0f * k * 0.5f });
    road.heights.push_back (height);
    height += 0.4f;
  }
  network.roads = { road };
  const RoadSurface surface (network, 1000.0f, 1000.0f);
  // Round the bend's corner two metres out, a degree at a time: on the
  // inside across the mitre, on the outside round the corner.
  float steepest = 0.0f;
  float last = surface.ground (502.0f, 500.0f, 0.0f);
  for (int degree = 1; degree <= 360; ++degree) {
    const float angle = static_cast<float> (degree) * 0.01745329f;
    const float here = surface.ground (
      500.0f + 2.0f * std::cos (angle), 500.0f + 2.0f * std::sin (angle), 0.0f);
    steepest = std::max (steepest, std::fabs (here - last));
    last = here;
  }
  // A degree is 3.5 cm of the way round: the grade and the crown make
  // half a centimetre of that at most.
  MOPPE_CHECK (steepest < 0.01f);
}

// A cart road that joins no other ends in a turning place lying back
// along it, level with its end, and is no road past it.
MOPPE_TEST (roads_end_in_turning_places_and_no_further_on) {
  const ElevationMap terrain = ridge_country ();
  const FloodField flood = analyze_standing_water (terrain, sea_level);
  const DrainageGraph drainage = analyze_drainage (terrain);
  RoadNetwork network;
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

  // Both ends of the road over the ridge, and the branch's far end; not
  // where the branch leaves the road.
  const std::vector<RoadTurning> turnings =
    road_turnings (network, 500.0f, 500.0f);
  MOPPE_CHECK (turnings.size () == 3);
  MOPPE_CHECK (turnings[0].road == 0 && !turnings[0].back);
  MOPPE_CHECK (turnings[1].road == 0 && turnings[1].back);
  MOPPE_CHECK (turnings[2].road == 1 && turnings[2].back);
  // The pad's length of road before each is level, and so is the pad.
  const Road& formed = network.roads[0];
  MOPPE_CHECK (formed.heights[0] == formed.heights[1] &&
               formed.heights[1] == formed.heights[2]);
  for (const float pad : turnings[0].heights)
    MOPPE_CHECK_NEAR (pad, formed.heights[0], 1e-4f);
  // The ring passes through the end and nowhere beyond it.
  MOPPE_CHECK_NEAR (turnings[0].points.front ().x_m, 100.0f, 1e-3f);
  for (const RoadPoint& point : turnings[0].points)
    MOPPE_CHECK (point.x_m >= 100.0f - 1e-3f);
  // The ground is formed for the pad: trodden five metres to the side of
  // the end, where the road alone leaves the ground all but untouched.
  MOPPE_CHECK (built.influence[51 * side + 20] > 0.9f);

  const RoadSurface surface (network, 500.0f, 500.0f, parameters);
  constexpr float field = 10.0f;
  // On the pad, four metres to the side of its middle, the tread.
  MOPPE_CHECK_NEAR (
    surface.ground (102.0f, 254.0f, field), formed.heights[0], 0.1f);
  // Six metres past the end, on the road's own line, the field: the
  // tread ends rounded at the end and does not run on.
  MOPPE_CHECK (surface.ground (94.0f, 250.0f, field) == field);
  MOPPE_CHECK (!surface.nearest (89.0f, 250.0f).found ||
               std::fabs (surface.nearest (89.0f, 250.0f).across) > 9.0f);
}

namespace {
  // Level country with one pond in it, a pit twenty metres wide, and the
  // sea far off for it to stand above.
  ElevationMap pond_country () {
    constexpr std::size_t side = 100;
    std::vector<float> heights (side * side, 60.0f);
    for (std::size_t z = 44; z <= 56; ++z)
      for (std::size_t x = 40; x <= 44; ++x)
        heights[z * side + x] = 55.0f;
    for (std::size_t z = 0; z < side; ++z)
      for (std::size_t x = 80; x <= 90; ++x)
        heights[z * side + x] = 30.0f;
    return make_elevation_map (
      TerrainDomain (
        side, side, 5.0f * mp_units::si::metre, 5.0f * mp_units::si::metre),
      heights);
  }
}

// A road planned out into water stops on the bank, and stands a
// freeboard over the water beside it.
MOPPE_TEST (a_road_into_water_ends_on_the_bank_above_it) {
  const ElevationMap terrain = pond_country ();
  const FloodField flood = analyze_standing_water (terrain, sea_level);
  MOPPE_CHECK (flood.water_depth_m (50 * 100 + 42) > 4.0f);
  const DrainageGraph drainage = analyze_drainage (terrain);
  RoadNetwork network;
  Road road { .kind = Road::Kind::Road };
  for (float x = 100.0f; x <= 212.0f; x += 4.0f)
    road.points.push_back ({ x, 250.0f });
  network.roads = { road };
  RoadFormation parameters;
  parameters.culvert_area_m2 = 1.0e12f;
  parameters.bridge_area_m2 = 1.0e12f;
  const RoadFormationResult built = form_roads (terrain.domain (),
                                                elevations (terrain),
                                                network,
                                                flood,
                                                drainage,
                                                parameters);
  const Road& formed = network.roads[0];
  // The pond's first cell is at x = 200; the last dry point is at 196.
  MOPPE_CHECK_NEAR (formed.points.back ().x_m, 196.0f, 1e-3f);
  MOPPE_CHECK (formed.heights.size () == formed.points.size ());
  MOPPE_CHECK (formed.heights.back () >= 60.0f + parameters.freeboard - 1e-3f);
  // Away from the water it runs on the ground.
  MOPPE_CHECK_NEAR (formed.heights[5], 60.0f, 0.05f);
  MOPPE_CHECK (built.steep_points == 0);
}
