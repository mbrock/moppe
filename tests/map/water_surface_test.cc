#include <moppe/game/water_presentation.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/watercourse.hh>

#include <tests/recording_renderer.hh>
#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <span>

namespace {
  // Water sheets normally come out of the watercourse painter. These tests
  // want a lattice with hand-chosen values, so they lay one out directly in
  // the renderer's interleaved order.
  moppe::terrain::WaterSheets
  water_sheets (moppe::terrain::TerrainDomain domain,
                std::span<const float> level_and_amplitude,
                std::span<const float> planar_flow) {
    using namespace moppe;
    terrain::WaterSheets sheets (std::move (domain));
    for (std::size_t offset = 0; offset < sheets.size (); ++offset) {
      auto site = sheets[sheets.index (offset)];
      spatial::get<terrain::surface_elevation> (site) =
        terrain::SurfaceElevation (level_and_amplitude[2 * offset] *
                                   terrain::surface_elevation[u::m]);
      spatial::get<terrain::wave_amplitude> (site) =
        level_and_amplitude[2 * offset + 1] * terrain::wave_amplitude[one];
      spatial::get<terrain::water_velocity> (site) =
        Vec3 (planar_flow[2 * offset], 0.0f, planar_flow[2 * offset + 1]) *
        terrain::water_velocity[u::m / u::s];
    }
    return sheets;
  }
}

MOPPE_TEST (water_surface_is_a_distinct_bundle_in_the_ground_elevation_frame) {
  using namespace moppe;
  map::SurfaceGeometry ground = map::SurfaceGeometry (
    terrain::TerrainDomain (2, 2, spatial_extent_in_metres (Vec3 (20, 0, 20))));
  std::ranges::fill (
    spatial::get<terrain::surface_elevation> (ground),
    map::SurfaceElevation (5.0f * terrain::surface_elevation[u::m]));
  map::rebuild_geometry (ground);

  const std::array level_and_amplitude {
    10.0f, 0.20f, 20.0f, 0.30f, 30.0f, 0.40f, 40.0f, 0.50f,
  };
  const std::array flow {
    1.0f, -2.0f, 2.0f, -3.0f, 3.0f, -4.0f, 4.0f, -5.0f,
  };
  const terrain::WaterSheets water =
    water_sheets (ground.domain (), level_and_amplitude, flow);

  const terrain::TerrainIndex first { 0, 0 };
  const auto water_elevation =
    spatial::get<terrain::surface_elevation> (water[first]);
  const auto ground_elevation = spatial::sample<terrain::surface_elevation> (
    ground, moppe::position (Vec3 (0, 0, 0)));
  const auto depth = water_elevation - ground_elevation;
  MOPPE_CHECK_NEAR (depth.numerical_value_in (u::m), 5.0f, 1e-6f);
  MOPPE_CHECK_NEAR (spatial::get<terrain::wave_amplitude> (water[first])
                      .numerical_value_in (one),
                    0.2f,
                    1e-6f);
  const Vec3 velocity = spatial::get<terrain::water_velocity> (water[first])
                          .numerical_value_in (u::m / u::s);
  MOPPE_CHECK_NEAR (velocity[0], 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (velocity[1], 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (velocity[2], -2.0f, 1e-6f);
}

MOPPE_TEST (water_presentation_writes_typed_sections_directly) {
  using namespace moppe;
  const terrain::TerrainDomain domain (2, 2, 10.0f * u::m, 10.0f * u::m);
  const std::array level_and_amplitude {
    10.0f, 0.20f, 20.0f, 0.30f, 30.0f, 0.40f, 40.0f, 0.50f,
  };
  const std::array flow {
    1.0f, -2.0f, 2.0f, -3.0f, 3.0f, -4.0f, 4.0f, -5.0f,
  };
  const terrain::WaterSheets water =
    water_sheets (domain, level_and_amplitude, flow);

  test::RecordingRenderer renderer;
  game::upload_water (renderer,
                      water,
                      10.0f * u::m,
                      spatial_extent_in_metres (Vec3 (200, 100, 300)));
  MOPPE_CHECK_NEAR (renderer.ocean.level, 10.0f, 1e-6f);
  MOPPE_CHECK_NEAR (renderer.ocean.center[0], 100.0f, 1e-6f);
  MOPPE_CHECK_NEAR (renderer.ocean.center[2], 150.0f, 1e-6f);
  MOPPE_CHECK_NEAR (renderer.ocean.half_extent, 5500.0f, 1e-6f);
  MOPPE_CHECK (renderer.ocean.cells == 300);
  MOPPE_CHECK (std::ranges::equal (renderer.water_levels, level_and_amplitude));
  MOPPE_CHECK (std::ranges::equal (renderer.water_flow, flow));
}
