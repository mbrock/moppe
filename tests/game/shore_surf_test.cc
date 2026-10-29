#include <moppe/game/shore_surf.hh>

#include <tests/test.hh>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace moppe;

namespace {
  struct SurfFixture {
    terrain::WaterSheets water;
    std::vector<terrain::SurfaceElevation> ground;

    explicit SurfFixture (const terrain::TerrainDomain& domain)
        : water (domain), ground (domain.size ()) {}

    void
    set (std::size_t x, std::size_t z, float bed, float level, float wave) {
      const std::size_t i = z * water.domain ().width () + x;
      ground[i] = terrain::surface_elevation_point (bed * u::m);
      spatial::get<terrain::surface_elevation> (water)[i] =
        terrain::surface_elevation_point (level * u::m);
      spatial::get<terrain::wave_amplitude> (water)[i] =
        wave * terrain::wave_amplitude[one];
    }

    game::ShoreSurf bake () const {
      return game::make_shore_surf (water, ground);
    }
  };

  float surf_test_mask (const std::array<float, 4>& sample) {
    return std::sqrt (sample[2] * sample[2] + sample[3] * sample[3]);
  }

  void check_surf_finite (const game::ShoreSurf& field) {
    MOPPE_CHECK (field.samples.size () == field.width * field.height);
    for (const auto& sample : field.samples) {
      for (const float lane : sample)
        MOPPE_CHECK (std::isfinite (lane));
      MOPPE_CHECK (surf_test_mask (sample) <= 1.00001f);
      if (sample[1] <= -24.0f || sample[1] >= 240.0f)
        MOPPE_CHECK_NEAR (surf_test_mask (sample), 0.0f, 0.0f);
    }
  }

  // A rectangular torus with unequal spacings and an off-centre sea strip.
  // Its level is deliberately NOT zero. Neither world edges nor the centre
  // of the array identify the shore. The dry shelf is low enough to wet.
  SurfFixture surf_test_strip (float depth) {
    SurfFixture fixture (
      terrain::TerrainDomain (119, 43, 3.0f * u::m, 5.0f * u::m));
    for (std::size_t z = 0; z < 43; ++z)
      for (std::size_t x = 0; x < 119; ++x) {
        const bool sea = x >= 19 && x <= 83;
        fixture.set (x,
                     z,
                     sea ? 17.0f - depth : 17.15f,
                     sea ? 17.0f : 17.15f,
                     sea ? 1.0f : 0.0f);
      }
    return fixture;
  }
}

MOPPE_TEST (shore_surf_never_infers_sea_from_altitude_or_freshwater) {
  SurfFixture fixture (
    terrain::TerrainDomain (71, 39, 2.0f * u::m, 4.0f * u::m));
  for (std::size_t z = 0; z < 39; ++z)
    for (std::size_t x = 0; x < 71; ++x) {
      // Dry low ground, an elevated lake, and a below-zero river.
      float bed = -3.0f + 0.02f * static_cast<float> (x + z);
      float level = bed, wave = 1.0f; // Even this lane needs positive depth.
      if (x > 37 && z < 16) {
        bed = 38.0f;
        level = 41.0f;
        wave = 0.1f;
      } else if (x > 6 && x < 12) {
        bed = -4.0f;
        level = -2.0f;
        wave = 0.0f;
      }
      fixture.set (x, z, bed, level, wave);
    }
  const auto field = fixture.bake ();
  check_surf_finite (field);
  for (const auto& sample : field.samples) {
    MOPPE_CHECK_NEAR (sample[0], 0.0f, 0.0f);
    MOPPE_CHECK (sample[1] < -24.0f);
    MOPPE_CHECK_NEAR (surf_test_mask (sample), 0.0f, 0.0f);
  }

  // An all-sea torus has no coastline, and must not seed a frame at its
  // texture edge. It is finite and inactive, with a positive far distance.
  for (std::size_t z = 0; z < 39; ++z)
    for (std::size_t x = 0; x < 71; ++x)
      fixture.set (x, z, 12.0f, 17.0f, 1.0f);
  const auto ocean = fixture.bake ();
  check_surf_finite (ocean);
  for (const auto& sample : ocean.samples) {
    MOPPE_CHECK (sample[1] > 240.0f);
    MOPPE_CHECK_NEAR (surf_test_mask (sample), 0.0f, 0.0f);
  }
}

MOPPE_TEST (shore_surf_constant_depth_has_the_shallow_water_travel_speed) {
  for (const float depth : { 1.0f, 4.0f, 9.0f }) {
    const auto field = surf_test_strip (depth).bake ();
    check_surf_finite (field);
    const float speed = std::sqrt (9.81f * depth);
    for (std::size_t x = 20; x < 40; ++x) {
      const auto& a = field.samples[7 * field.width + x];
      const auto& b = field.samples[7 * field.width + x + 1];
      MOPPE_CHECK_NEAR (b[0] - a[0], 3.0f / speed, 2e-5f);
      MOPPE_CHECK_NEAR (b[1] - a[1], 3.0f, 2e-5f);
      MOPPE_CHECK_NEAR (a[2], -1.0f, 2e-5f);
      MOPPE_CHECK_NEAR (a[3], 0.0f, 2e-5f);
    }
    // The signed extension keeps direction and coherence at the very first
    // sea cell, including when sea and dry propagation speeds differ.
    const auto& wet = field.samples[7 * field.width + 19];
    const auto& dry = field.samples[7 * field.width + 18];
    MOPPE_CHECK (wet[0] > 0.0f && wet[1] > 0.0f);
    MOPPE_CHECK (dry[0] < 0.0f && dry[1] < 0.0f);
    MOPPE_CHECK (wet[2] < -0.99f && dry[2] < -0.99f);
  }
  // Same distance, quarter the depth: twice as long between crests' arrival.
  const auto deep = surf_test_strip (4.0f).bake ();
  const auto shallow = surf_test_strip (1.0f).bake ();
  const float deep_time = deep.samples[35][0] - deep.samples[25][0];
  const float shallow_time = shallow.samples[35][0] - shallow.samples[25][0];
  MOPPE_CHECK_NEAR (shallow_time / deep_time, 2.0f, 2e-5f);
}

MOPPE_TEST (shore_surf_travel_responds_to_asymmetric_sloping_bathymetry) {
  auto fixture = surf_test_strip (4.0f);
  const auto depth = [] (float x) { return 2.0f + 0.12f * (x - 19.0f); };
  for (std::size_t z = 0; z < 43; ++z)
    for (std::size_t x = 19; x <= 83; ++x)
      fixture.set (x, z, 17.0f - depth (static_cast<float> (x)), 17.0f, 1.0f);
  const auto field = fixture.bake ();
  check_surf_finite (field);
  // Independent continuum expectation: integral dx/sqrt(g*(d0+slope*x)).
  // A first-order sampled-speed solve should agree to a few percent, not
  // freeze the whole shelf to either the shore's depth or the deep water's.
  const float slope = 0.12f / 3.0f;
  const float expected = 2.0f *
                         (std::sqrt (depth (35)) - std::sqrt (depth (23))) /
                         (slope * std::sqrt (9.81f));
  MOPPE_CHECK_NEAR (
    field.samples[35][0] - field.samples[23][0], expected, 0.025f * expected);
  const float near_step = field.samples[25][0] - field.samples[24][0];
  const float deep_step = field.samples[35][0] - field.samples[34][0];
  MOPPE_CHECK (near_step > deep_step);
}

MOPPE_TEST (shore_surf_wraps_both_seams_without_changing_the_field) {
  constexpr std::size_t width = 97, height = 61;
  SurfFixture fixture (
    terrain::TerrainDomain (width, height, 2.0f * u::m, 3.0f * u::m));
  SurfFixture shifted (fixture.water.domain ());
  constexpr std::size_t shift_x = 79, shift_z = 53;
  for (std::size_t z = 0; z < height; ++z)
    for (std::size_t x = 0; x < width; ++x) {
      const bool sea = x > 9 + z / 8 && x < 70;
      const bool lake = x >= 68 && x < 76 && z > 20 && z < 35;
      const float depth =
        0.3f + 0.02f * static_cast<float> (x) + 0.03f * static_cast<float> (z);
      const float bed = lake ? 24.0f : sea ? 17.0f - depth : 17.1f;
      const float level = lake ? 26.0f : sea ? 17.0f : bed;
      const float wave = lake ? 0.1f : sea ? 1.0f : 0.0f;
      fixture.set (x, z, bed, level, wave);
      shifted.set (
        (x + shift_x) % width, (z + shift_z) % height, bed, level, wave);
    }
  const auto a = fixture.bake ();
  const auto b = shifted.bake ();
  check_surf_finite (a);
  check_surf_finite (b);
  bool active = false;
  for (std::size_t z = 0; z < height; ++z)
    for (std::size_t x = 0; x < width; ++x) {
      const auto& original = a.samples[z * width + x];
      const auto& translated =
        b.samples[((z + shift_z) % height) * width + (x + shift_x) % width];
      for (std::size_t lane = 0; lane < 4; ++lane)
        MOPPE_CHECK_NEAR (translated[lane], original[lane], 2e-4f);
      active |= surf_test_mask (original) > 0.5f;
    }
  MOPPE_CHECK (active);
}

MOPPE_TEST (shore_surf_confines_wetting_and_suppresses_opposing_fronts) {
  SurfFixture fixture (
    terrain::TerrainDomain (311, 43, 5.5f * u::m, 7.0f * u::m));
  for (std::size_t z = 0; z < 43; ++z)
    for (std::size_t x = 0; x < 311; ++x) {
      const bool sea = x >= 93 && x <= 251;
      const bool cliff = x == 92 && z >= 22 && z <= 29;
      const bool lake = x >= 90 && x <= 91 && z >= 8 && z <= 13;
      const float bed = sea ? 13.0f : lake ? 21.0f : cliff ? 22.0f : 17.1f;
      fixture.set (x,
                   z,
                   bed,
                   sea    ? 17.0f
                   : lake ? 23.0f
                          : bed,
                   sea    ? 1.0f
                   : lake ? 0.1f
                          : 0.0f);
    }
  const auto field = fixture.bake ();
  check_surf_finite (field);
  const auto& beach = field.samples[2 * field.width + 92];
  MOPPE_CHECK (beach[0] < 0.0f && beach[1] < 0.0f);
  MOPPE_CHECK (surf_test_mask (beach) > 0.9f);
  MOPPE_CHECK_NEAR (surf_test_mask (field.samples[2 * field.width + 83]),
                    0.0f,
                    0.0f); // inland, even though it is low
  MOPPE_CHECK_NEAR (surf_test_mask (field.samples[25 * field.width + 92]),
                    0.0f,
                    0.0f); // a cliff at the water's edge
  MOPPE_CHECK_NEAR (surf_test_mask (field.samples[2 * field.width + 150]),
                    0.0f,
                    0.0f); // well offshore, not merely a front collision
  for (std::size_t z = 8; z <= 13; ++z)
    for (std::size_t x = 90; x <= 91; ++x) {
      const auto& lake = field.samples[z * field.width + x];
      MOPPE_CHECK (lake[1] < -24.0f);
      MOPPE_CHECK_NEAR (surf_test_mask (lake), 0.0f, 0.0f);
    }

  const auto strait = surf_test_strip (4.0f).bake ();
  const std::size_t middle = (19 + 83) / 2;
  MOPPE_CHECK (strait.samples[middle][1] < 120.0f);
  MOPPE_CHECK_NEAR (surf_test_mask (strait.samples[middle]), 0.0f, 2e-5f);
  MOPPE_CHECK (surf_test_mask (strait.samples[middle - 8]) > 0.99f);
  MOPPE_CHECK (surf_test_mask (strait.samples[middle + 8]) > 0.99f);
}

MOPPE_TEST (shore_surf_bounds_the_bake_without_changing_world_periods) {
  // Nonmultiples of 512 exercise resampling, not just the usual 2048/4.
  constexpr std::size_t width = 1027, height = 521;
  SurfFixture fixture (terrain::TerrainDomain (
    width, height, spatial_extent_in_metres (Vec3 (5000, 0, 2700))));
  for (std::size_t z = 0; z < height; ++z)
    for (std::size_t x = 0; x < width; ++x) {
      const bool sea = x > width / 7 && x < 5 * width / 7;
      fixture.set (
        x, z, sea ? 13.0f : 17.1f, sea ? 17.0f : 17.1f, sea ? 1.0f : 0.0f);
    }
  const auto field = fixture.bake ();
  check_surf_finite (field);
  MOPPE_CHECK (field.width == 512 && field.height == 512);
  MOPPE_CHECK_NEAR (field.period_x, 5000.0f, 0.001f);
  MOPPE_CHECK_NEAR (field.period_z, 2700.0f, 0.001f);
  const std::size_t x = field.width / 7 + 5;
  const float spacing = field.period_x / static_cast<float> (field.width);
  MOPPE_CHECK_NEAR (field.samples[x + 1][0] - field.samples[x][0],
                    spacing / std::sqrt (9.81f * 4.0f),
                    2e-5f);
  MOPPE_CHECK (surf_test_mask (field.samples[x]) > 0.99f);
}

MOPPE_TEST (shore_surf_rejects_wrong_domains_and_nonfinite_samples) {
  auto fixture = surf_test_strip (4.0f);
  bool rejected = false;
  try {
    (void)game::make_shore_surf (fixture.water,
                                 std::span (fixture.ground).first (17));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  MOPPE_CHECK (rejected);
  fixture.set (30, 17, std::numeric_limits<float>::quiet_NaN (), 17.0f, 1.0f);
  rejected = false;
  try {
    (void)fixture.bake ();
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  MOPPE_CHECK (rejected);
}
