#include <moppe/terrain/flood.hh>

#include <tests/test.hh>

#include <array>

using namespace moppe::terrain;

MOPPE_TEST (priority_flood_fills_a_basin_to_its_lowest_spill) {
  const std::array heights {
    0.f, 0.f, 0.f, 0.f, 0.f,
    0.f, 3.f, 2.f, 3.f, 0.f,
    0.f, 3.f, 1.f, 3.f, 0.f,
    0.f, 3.f, 3.f, 3.f, 0.f,
    0.f, 0.f, 0.f, 0.f, 0.f
  };
  const TerrainView terrain ({ .width = 5, .height = 5 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);

  MOPPE_CHECK_NEAR (flood.water_level.at (2, 2), 2.0f, 0.0f);
  MOPPE_CHECK_NEAR (flood.water_depth.at (2, 2), 1.0f, 0.0f);
  MOPPE_CHECK_NEAR (flood.water_depth.at (2, 1), 0.0f, 0.0f);
}

MOPPE_TEST (standing_sea_uses_the_global_water_plane) {
  const std::array heights { -2.f, -1.f, 1.f, 2.f };
  const TerrainView terrain ({ .width = 2, .height = 2 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);

  MOPPE_CHECK_NEAR (flood.water_level.at (0, 0), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (flood.water_depth.at (0, 0), 2.0f, 0.0f);
  MOPPE_CHECK_NEAR (flood.water_depth.at (1, 0), 1.0f, 0.0f);
}

MOPPE_TEST (periodic_flood_can_spill_across_the_duplicated_seam) {
  const std::array heights {
    1.f, 3.f, 0.f, 1.f,
    1.f, 3.f, 0.f, 1.f,
    1.f, 3.f, 0.f, 1.f,
    1.f, 3.f, 0.f, 1.f
  };
  const TerrainView terrain
    ({ .width = 4, .height = 4, .topology = Topology::Torus }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);

  MOPPE_CHECK (flood.width () == 3);
  MOPPE_CHECK (flood.spill_receiver[0] == 2);
}

MOPPE_TEST (all_land_torus_uses_its_global_minimum_as_an_outlet) {
  const std::array heights {
    4.f, 3.f, 2.f, 4.f,
    4.f, 1.f, 3.f, 4.f,
    4.f, 4.f, 4.f, 4.f,
    4.f, 3.f, 2.f, 4.f
  };
  const TerrainView terrain
    ({ .width = 4, .height = 4, .topology = Topology::Torus }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);

  MOPPE_CHECK (flood.outlets.size () == 1);
  MOPPE_CHECK (flood.outlets[0] == 4);
  MOPPE_CHECK (flood.spill_receiver[4] == 4);
}

MOPPE_TEST (every_spill_receiver_path_reaches_an_outlet) {
  const std::array heights {
    0.f, 4.f, 3.f, 0.f,
    2.f, 1.f, 5.f, 2.f,
    3.f, 2.f, 4.f, 3.f,
    0.f, 4.f, 3.f, 0.f
  };
  const TerrainView terrain
    ({ .width = 4, .height = 4, .topology = Topology::Torus }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const std::size_t count = flood.width () * flood.height ();

  for (std::uint32_t origin = 0; origin < count; ++origin) {
    std::uint32_t cell = origin;
    std::size_t steps = 0;
    while (flood.spill_receiver[cell] != cell && steps < count) {
      const std::uint32_t next = flood.spill_receiver[cell];
      MOPPE_CHECK (flood.water_level.values ()[next]
		   <= flood.water_level.values ()[cell]);
      cell = next;
      ++steps;
    }
    MOPPE_CHECK (steps < count);
  }
}

MOPPE_TEST (lake_census_measures_physical_area_depth_and_volume) {
  const std::array heights {
    0.f, 0.f, 0.f, 0.f, 0.f,
    0.f, 3.f, 2.f, 3.f, 0.f,
    0.f, 3.f, 1.f, 3.f, 0.f,
    0.f, 3.f, 3.f, 3.f, 0.f,
    0.f, 0.f, 0.f, 0.f, 0.f
  };
  const TerrainView terrain
    ({ .width = 5, .height = 5, .spacing_x = 2.0f,
	.height_scale = 10.0f }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);

  MOPPE_CHECK (census.bodies.size () == 1);
  MOPPE_CHECK (census.bodies[0].cells == 1);
  MOPPE_CHECK_NEAR (census.bodies[0].area_m2, 2.0f, 0.0f);
  MOPPE_CHECK_NEAR (census.bodies[0].maximum_depth_m, 10.0f, 0.0f);
  MOPPE_CHECK_NEAR (census.bodies[0].volume_m3, 20.0f, 0.0f);
  MOPPE_CHECK
    (census.bodies[0].classification == WaterBodyClass::Puddle);
}

MOPPE_TEST (permanence_removes_small_ponds_but_never_the_sea) {
  const std::array heights { -2.f, -1.f, 1.f, 2.f };
  const TerrainView terrain ({ .width = 2, .height = 2 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);
  const ScalarRaster permanent = permanent_water_surface
    (flood, census, { .minimum_area_m2 = 1000000.0f });

  MOPPE_CHECK (census.bodies.size () == 1);
  MOPPE_CHECK
    (census.bodies[0].classification == WaterBodyClass::Sea);
  MOPPE_CHECK_NEAR (permanent.at (0, 0), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (permanent.at (1, 0), 0.0f, 0.0f);
}
