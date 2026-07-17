#include <moppe/game/water_presentation.hh>
#include <moppe/map/generate.hh>
#include <moppe/map/surface.hh>
#include <moppe/map/water_surface.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>

MOPPE_TEST (water_surface_is_a_distinct_bundle_in_the_ground_elevation_frame) {
  using namespace moppe;
  map::RandomHeightMap map (
    2, 2, Vec3 (20, 100, 20), 51, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 4, 0.05f);
  map.recompute_normals ();
  map::Surface ground (map);

  const std::array level_and_amplitude {
    0.10f, 0.20f, 0.20f, 0.30f, 0.30f, 0.40f, 0.40f, 0.50f,
  };
  const std::array flow {
    1.0f, -2.0f, 2.0f, -3.0f, 3.0f, -4.0f, 4.0f, -5.0f,
  };
  map::WaterSurface water (
    ground.atlas ().domain (), level_and_amplitude, flow, 100.0f * u::m);

  const map::SurfaceIndex first { 0, 0 };
  const auto water_elevation =
    spatial::get<map::surface_elevation> (water.sections ()[first]);
  const auto ground_elevation =
    spatial::get<map::surface_elevation> (ground.atlas ().geometry ()[first]);
  const auto depth = water_elevation - ground_elevation;
  MOPPE_CHECK_NEAR (depth.numerical_value_in (u::m), 5.0f, 1e-6f);
  MOPPE_CHECK_NEAR (spatial::get<map::wave_amplitude> (water.sections ()[first])
                      .numerical_value_in (one),
                    0.2f,
                    1e-6f);
  const Vec3 velocity =
    spatial::get<map::water_velocity> (water.sections ()[first])
      .numerical_value_in (u::m / u::s);
  MOPPE_CHECK_NEAR (velocity[0], 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (velocity[1], 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (velocity[2], -2.0f, 1e-6f);
}

MOPPE_TEST (water_presentation_is_the_only_normalization_and_packing_bridge) {
  using namespace moppe;
  const map::SurfaceDomain domain (
    2, 2, 10.0f * u::m, 10.0f * u::m, terrain::Topology::Bounded);
  const std::array level_and_amplitude {
    0.10f, 0.20f, 0.20f, 0.30f, 0.30f, 0.40f, 0.40f, 0.50f,
  };
  const std::array flow {
    1.0f, -2.0f, 2.0f, -3.0f, 3.0f, -4.0f, 4.0f, -5.0f,
  };
  const map::WaterSurface water (
    domain, level_and_amplitude, flow, 100.0f * u::m);

  render::OceanSetup ocean;
  ocean.level = 10.0f;
  game::WaterPresentation presentation;
  presentation.reset (ocean);
  MOPPE_CHECK (presentation.levels ().empty ());
  MOPPE_CHECK (presentation.flow ().empty ());
  presentation.refresh (water, 100.0f * u::m);

  MOPPE_CHECK (presentation.levels ().size () == level_and_amplitude.size ());
  MOPPE_CHECK (presentation.flow ().size () == flow.size ());
  for (std::size_t index = 0; index < level_and_amplitude.size (); ++index)
    MOPPE_CHECK_NEAR (
      presentation.levels ()[index], level_and_amplitude[index], 1e-6f);
  for (std::size_t index = 0; index < flow.size (); ++index)
    MOPPE_CHECK_NEAR (presentation.flow ()[index], flow[index], 1e-6f);
}
