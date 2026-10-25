#include <moppe/game/boulders.hh>
#include <moppe/game/river_stones.hh>
#include <moppe/game/walker.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>

#include <tests/recording_renderer.hh>
#include <tests/surface_fixture.hh>
#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace moppe;

namespace {
  // A 256 m square at one-metre spacing with one straight channel down it
  // along x, centred on z = 128: a parabolic bed `depth` deep and `width`
  // across at bankfull, banks rising one in three beyond it, the whole
  // falling `slope` toward +x, and the water standing level with the banks
  // and running down the channel at `speed`.
  struct RiverStoneTestChannel {
    map::SurfaceGeometry surface;
    terrain::WaterSheets water;
    map::SurfaceReadings readings;
    terrain::RiverNetwork rivers;
  };

  constexpr float river_test_side = 256.0f;
  constexpr float river_test_centre = 128.0f;

  RiverStoneTestChannel
  river_test_channel (float slope, float depth, float width, float speed) {
    const terrain::TerrainDomain domain (
      256,
      256,
      spatial_extent_in_metres (Vec3 (river_test_side, 0, river_test_side)));
    const float half = 0.5f * width;
    const auto surface_at = [&] (float x) { return 100.0f - slope * x; };
    const auto ground_at = [&] (float x, float z) {
      const float across = std::abs (z - river_test_centre);
      const float bankfull = surface_at (x);
      if (across < half)
        return bankfull - depth * (1.0f - (across / half) * (across / half));
      return bankfull + std::min (3.0f, (across - half) / 3.0f);
    };

    map::SurfaceGeometry surface (domain);
    terrain::WaterSheets water (domain);
    for (std::size_t z = 0; z < domain.height (); ++z)
      for (std::size_t x = 0; x < domain.width (); ++x) {
        const terrain::TerrainIndex at { x, z };
        const float fx = static_cast<float> (x);
        const float fz = static_cast<float> (z);
        const float ground = ground_at (fx, fz);
        const bool wet = std::abs (fz - river_test_centre) < half;
        spatial::get<terrain::surface_elevation> (surface[at]) =
          terrain::surface_elevation_point (ground * u::m);
        spatial::get<terrain::surface_elevation> (water[at]) =
          terrain::surface_elevation_point (
            (wet ? surface_at (fx) : ground - 0.01f) * u::m);
        spatial::get<terrain::water_velocity> (water[at]) =
          (wet ? Vec3 (speed, -slope * speed, 0.0f) : Vec3 ()) *
          terrain::water_velocity[u::m / u::s];
      }
    map::rebuild_geometry (surface);
    map::SurfaceReadings readings = test::complete_readings (surface);

    terrain::RiverNetwork rivers;
    terrain::RiverReach reach {
      .id = terrain::RiverReachId { 0 },
      .cells = {},
      .upstream_body = terrain::no_water_body,
      .downstream_body = terrain::no_water_body,
      .downstream_ocean = false,
      .downstream_reach = terrain::RiverReach::no_id,
      .upstream_area = {},
      .downstream_area = {},
      .maximum_slope = slope * terrain::terrain_slope[one],
      .alignment = {},
    };
    // A regime width of `width`: w = 0.012 sqrt(A).
    const float area = (width / 0.012f) * (width / 0.012f);
    for (float x = 10.0f; x <= river_test_side - 10.0f; x += 1.0f)
      reach.alignment.points.push_back (
        { .x_m = x,
          .z_m = river_test_centre,
          .distance_m = x - 10.0f,
          .flow_distance_m = river_test_side - 10.0f - x,
          .contributing_area_m2 = area,
          .slope = slope,
          .water_level_m = surface_at (x) });
    rivers.reaches.push_back (std::move (reach));
    return { std::move (surface),
             std::move (water),
             std::move (readings),
             std::move (rivers) };
  }

  game::RiverStones river_test_stones (const RiverStoneTestChannel& channel) {
    return game::plan_river_stones (channel.surface,
                                    channel.readings,
                                    channel.water,
                                    channel.rivers,
                                    0x5703eu);
  }

  bool in_channel (const game::BoulderSite& site, float width) {
    const Vec3 centre = position_value (site.centre);
    return std::abs (centre[2] - river_test_centre) < 0.5f * width &&
           centre[0] > 20.0f && centre[0] < river_test_side - 20.0f;
  }

  // Walks a walker toward `to`, steering for it, until within half a
  // metre or out of time. Returns the seconds taken, or a negative number
  // if it never arrived; `wettest` is the deepest the water stood over its
  // feet on the way.
  float river_test_walk (const RiverStoneTestChannel& channel,
                         Vec3 from,
                         Vec3 to,
                         const mov::TrunkField* stones,
                         float& wettest) {
    game::WorldParams world;
    world.map_size =
      spatial_extent_in_metres (Vec3 (river_test_side, 60, river_test_side));
    world.water_level = -100 * u::m;
    game::Walker walker;
    Vec3 heading = to - from;
    heading[1] = 0.0f;
    walker.spawn (position (from), normalized (heading));
    wettest = -1.0f;
    constexpr float step = 1.0f / 120.0f;
    for (int i = 0; i < 120 * 12; ++i) {
      Vec3 ahead = to - walker.position ();
      ahead[1] = 0.0f;
      if (length (ahead) < 0.5f)
        return static_cast<float> (i) * step;
      const Vec3 facing = walker.heading ();
      walker.turn_by (-std::atan2 (facing[2] * ahead[0] - facing[0] * ahead[2],
                                   dot (facing, ahead)));
      walker.set_walk (1.0f * one);
      walker.update (
        seconds (step), channel.surface, world, stones, &channel.water);
      const Vec3 feet = walker.position ();
      wettest = std::max (
        wettest,
        (game::still_water_at (world, channel.surface, &channel.water, feet) -
         feet[1] * u::m)
          .numerical_value_in (u::m));
    }
    return -1.0f;
  }
}

MOPPE_TEST (bed_competence_is_shields_with_a_threshold_rising_on_steep_beds) {
  // A brook 0.3 m deep falling one in ten: theta_c = 0.15 * 0.1^0.25.
  MOPPE_CHECK_NEAR (
    game::bed_competence (0.3f * u::m, 0.1f).numerical_value_in (u::m),
    0.3f * 0.1f / (1.65f * 0.15f * 0.5623413f),
    1e-4f);
  // A river 2 m deep falling one in a thousand: theta_c holds at 0.03.
  MOPPE_CHECK_NEAR (
    game::bed_competence (2.0f * u::m, 0.001f).numerical_value_in (u::m),
    2.0f * 0.001f / (1.65f * 0.03f),
    1e-5f);
  MOPPE_CHECK_NEAR (
    game::bed_competence (1.0f * u::m, 0.0f).numerical_value_in (u::m),
    0.0f,
    0.0f);
  // Sand is fine, cobbles coarse, and halfway between on the logarithmic
  // scale lies their geometric mean.
  MOPPE_CHECK_NEAR (game::bed_fineness (0.002f * u::m), 1.0f, 1e-5f);
  MOPPE_CHECK_NEAR (game::bed_fineness (0.064f * u::m), 0.0f, 1e-5f);
  MOPPE_CHECK_NEAR (game::bed_fineness (0.0113137f * u::m), 0.5f, 1e-3f);
  MOPPE_CHECK_NEAR (game::bed_fineness (0.5f * u::m), 0.0f, 0.0f);
}

MOPPE_TEST (steep_brooks_keep_their_stones_and_slow_rivers_lay_none) {
  const RiverStoneTestChannel brook =
    river_test_channel (0.08f, 0.4f, 6.0f, 1.5f);
  const game::RiverStones steep = river_test_stones (brook);
  const auto in_brook = std::ranges::count_if (
    steep.sites, [] (const auto& site) { return in_channel (site, 6.0f); });
  const auto footholds =
    std::ranges::count_if (steep.sites, [] (const auto& site) {
      return site.solid && in_channel (site, 6.0f);
    });
  // Rows of keystones every ten metres or so, and the bed's stones.
  MOPPE_CHECK (in_brook > 40);
  MOPPE_CHECK (footholds > 5);

  // A metre and a half of water falling one in two thousand, at the spate
  // twice that, moves fine gravel: the bed lies smooth, and only a great
  // rock no flood moved stands in it.
  const RiverStoneTestChannel river =
    river_test_channel (0.0005f, 1.5f, 20.0f, 0.8f);
  const game::RiverStones slow = river_test_stones (river);
  MOPPE_CHECK (slow.crossings.empty ());
  for (const game::BoulderSite& site : slow.sites)
    if (in_channel (site, 20.0f))
      MOPPE_CHECK (site.radius >= 0.7f * u::m);
}

MOPPE_TEST (stepping_stones_carry_a_walker_across_dry_shod) {
  // Shallow, gentle, and narrow: a ford, and where it is crossed on stones.
  const RiverStoneTestChannel brook =
    river_test_channel (0.01f, 0.45f, 6.0f, 1.0f);
  const game::RiverStones stones = river_test_stones (brook);
  MOPPE_CHECK (stones.crossings.size () == 1);
  if (stones.crossings.empty ())
    return;
  const game::StoneCrossing& crossing = stones.crossings.front ();
  // A walker keeps to the row by eye, a hand's breadth or so off the line
  // through the stones' middles.
  const Vec3 off (0.15f, 0.0f, 0.0f);
  const Vec3 from = position_value (crossing.from) + off;
  const Vec3 to = position_value (crossing.to) + off;
  // Bank to bank, across the water.
  MOPPE_CHECK ((from[2] - river_test_centre) * (to[2] - river_test_centre) <
               0.0f);
  MOPPE_CHECK (std::abs (from[2] - river_test_centre) > 3.0f);

  game::BoulderPlan plan;
  plan.sites = stones.sites;
  plan.period =
    spatial_extent_in_metres (Vec3 (river_test_side, 0, river_test_side));
  test::RecordingRenderer renderer;
  game::BoulderLandscape landscape;
  landscape.rebuild (renderer, plan);
  mov::TrunkField field;
  field.set_trunks (landscape.colliders (), river_test_side, river_test_side);
  field.focus (from);

  float dry = 0.0f;
  const float on_stones = river_test_walk (brook, from, to, &field, dry);
  float wet = 0.0f;
  const float wading = river_test_walk (brook, from, to, nullptr, wet);
  MOPPE_CHECK (on_stones > 0.0f);
  MOPPE_CHECK (wading > 0.0f);
  // The feet never meet the water on the stones; without them the water
  // comes over the shins, and wading is slower than walking.
  MOPPE_CHECK (dry <= 0.0f);
  MOPPE_CHECK (wet > 0.3f);
  MOPPE_CHECK (on_stones < wading);
}
