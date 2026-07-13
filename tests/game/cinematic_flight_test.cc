#include <moppe/game/cinematic_flight.hh>

#include <tests/test.hh>

#include <algorithm>
#include <vector>

using namespace moppe;

namespace {
  struct FlightFixture {
    static constexpr int side = 17;
    static constexpr std::size_t count = side * side;

    map::RandomHeightMap map { side, side, Vec3 (1600, 240, 1600), 7 };
    terrain::TerrainGrid grid = map.terrain_view ().grid ();
    terrain::FieldSamplingGrid2D domain { .width = side, .height = side };
    terrain::FloodField flood;
    terrain::LakeCensus census;
    terrain::DrainageGraph drainage;
    terrain::RiverNetwork rivers;

    FlightFixture ()
        : flood { .source_grid = grid,
                  .sea_level = 0.1f,
                  .has_ocean = false,
                  .water_level = terrain::ScalarRaster (
                    domain, std::vector<float> (count, 0.1f)),
                  .water_depth = terrain::ScalarRaster (
                    domain, std::vector<float> (count, 0.0f)),
                  .ocean = std::vector<std::uint8_t> (count, 0),
                  .spill_receiver = std::vector<terrain::CellIndex> (count),
                  .outlets = {} },
          census { .body = std::vector<terrain::WaterBodyId> (
                     count, terrain::LakeCensus::dry) },
          drainage { .source_grid = grid,
                     .receiver = std::vector<terrain::CellIndex> (count),
                     .slope = terrain::SlopeRaster (terrain::ScalarRaster (
                       domain, std::vector<float> (count, 0.05f))),
                     .contributing_area =
                       terrain::ContributingAreaRaster (terrain::ScalarRaster (
                         domain, std::vector<float> (count, 10000.0f))),
                     .basin =
                       std::vector<terrain::CellIndex> (count, count - 1),
                     .sinks = { count - 1 } },
          rivers { .minimum_area =
                     10000.0f * mp_units::si::metre * mp_units::si::metre,
                   .reach_by_cell = std::vector<terrain::RiverReachId> (
                     count, terrain::RiverReach::no_id),
                   .waterfall_by_cell = std::vector<terrain::WaterfallId> (
                     count, terrain::Waterfall::no_id) } {
      for (int z = 0; z < side; ++z)
        for (int x = 0; x < side; ++x) {
          const float peak =
            std::max (0.0f, 1.0f - std::hypot (x - 12.0f, z - 5.0f) / 7.0f);
          const float saddle =
            0.06f * std::abs (z - 11.0f) - 0.025f * std::abs (x - 7.0f);
          map.set (x, z, 0.16f + 0.55f * peak + saddle);
          const std::size_t cell = z * side + x;
          const std::size_t next = z + 1 < side ? cell + side : cell;
          flood.spill_receiver[cell] = next;
          drainage.receiver[cell] = next;
        }
      map.recompute_normals ();

      std::vector<terrain::CellIndex> cells;
      for (int z = 1; z < 15; ++z)
        cells.push_back (z * side + 3);
      for (std::size_t i = 0; i < cells.size (); ++i) {
        rivers.reach_by_cell[cells[i]] = 0;
      }
      rivers.reaches.push_back (
        { .id = 0,
          .cells = cells,
          .upstream_body = terrain::no_water_body,
          .downstream_body = terrain::no_water_body,
          .downstream_ocean = false,
          .downstream_reach = terrain::RiverReach::no_id,
          .upstream_area = 40000.0f * mp_units::si::metre * mp_units::si::metre,
          .downstream_area =
            700000.0f * mp_units::si::metre * mp_units::si::metre,
          .maximum_slope = 0.2f * terrain::terrain_slope[mp_units::one] });
      rivers.waterfalls.push_back (
        { .id = 0,
          .reach_id = 0,
          .lip_cell = cells[7],
          .foot_cell = cells[8],
          .drop = 18.0f * mp_units::si::metre,
          .horizontal_distance = 100.0f * mp_units::si::metre,
          .slope = 0.18f * terrain::terrain_slope[mp_units::one],
          .contributing_area =
            400000.0f * mp_units::si::metre * mp_units::si::metre });
    }
  };
}

MOPPE_TEST (cinematic_flight_reads_landscape_into_distinct_places) {
  FlightFixture fixture;
  const game::CinematicFlightPlan plan =
    game::plan_cinematic_flight (fixture.map,
                                 fixture.flood,
                                 fixture.census,
                                 fixture.drainage,
                                 fixture.rivers,
                                 Vec3 (200, 80, 200));

  MOPPE_CHECK (plan.waypoints.size () >= 14);
  const auto has = [&] (game::CinematicLandmarkKind kind) {
    return std::any_of (
      plan.landmarks.begin (),
      plan.landmarks.end (),
      [kind] (const auto& landmark) { return landmark.kind == kind; });
  };
  MOPPE_CHECK (has (game::CinematicLandmarkKind::Valley));
  MOPPE_CHECK (has (game::CinematicLandmarkKind::Waterfall));
  MOPPE_CHECK (has (game::CinematicLandmarkKind::Peak));
  MOPPE_CHECK (has (game::CinematicLandmarkKind::Arrival));
  for (const game::CinematicFlightWaypoint& waypoint : plan.waypoints)
    MOPPE_CHECK (waypoint.position[1] >=
                 fixture.map.interpolated_height (waypoint.position[0],
                                                  waypoint.position[2]) +
                   8.9f);
}

MOPPE_TEST (cinematic_planner_reads_across_a_toroidal_seam) {
  constexpr int storage_side = 17;
  constexpr int unique_side = storage_side - 1;
  constexpr std::size_t count = unique_side * unique_side;
  map::RandomHeightMap map (storage_side,
                            storage_side,
                            Vec3 (1600, 240, 1600),
                            9,
                            terrain::Topology::Torus);
  for (int z = 0; z < storage_side; ++z)
    for (int x = 0; x < storage_side; ++x) {
      const float dx =
        std::min (x % unique_side, unique_side - x % unique_side);
      const float dz =
        std::min (z % unique_side, unique_side - z % unique_side);
      map.set (x, z, 0.2f + 0.02f * (dx + dz));
    }
  map.recompute_normals ();

  const terrain::TerrainGrid grid = map.terrain_view ().grid ();
  const terrain::FieldSamplingGrid2D domain { .width = unique_side,
                                              .height = unique_side };
  std::vector<terrain::CellIndex> receiver (count);
  for (std::uint32_t cell = 0; cell < count; ++cell)
    receiver[cell] = terrain::CellIndex (cell);
  const terrain::FloodField flood {
    .source_grid = grid,
    .sea_level = 0.0f,
    .has_ocean = false,
    .water_level =
      terrain::ScalarRaster (domain, std::vector<float> (count, 0.0f)),
    .water_depth =
      terrain::ScalarRaster (domain, std::vector<float> (count, 0.0f)),
    .ocean = std::vector<std::uint8_t> (count, 0),
    .spill_receiver = receiver,
    .outlets = { terrain::CellIndex (0) }
  };
  const terrain::LakeCensus census { .body = std::vector<terrain::WaterBodyId> (
                                       count, terrain::LakeCensus::dry) };
  const terrain::DrainageGraph drainage {
    .source_grid = grid,
    .receiver = receiver,
    .slope = terrain::SlopeRaster (
      terrain::ScalarRaster (domain, std::vector<float> (count, 0.0f))),
    .contributing_area = terrain::ContributingAreaRaster (
      terrain::ScalarRaster (domain, std::vector<float> (count, 10000.0f))),
    .basin = std::vector<terrain::CellIndex> (count, 0),
    .sinks = { terrain::CellIndex (0) }
  };
  const terrain::RiverNetwork rivers {
    .minimum_area = 10000.0f * mp_units::si::metre * mp_units::si::metre,
    .reach_by_cell =
      std::vector<terrain::RiverReachId> (count, terrain::RiverReach::no_id),
    .waterfall_by_cell =
      std::vector<terrain::WaterfallId> (count, terrain::Waterfall::no_id)
  };

  const game::CinematicFlightPlan plan = game::plan_cinematic_flight (
    map, flood, census, drainage, rivers, Vec3 (100, 60, 100));
  MOPPE_CHECK (!plan.empty ());
  MOPPE_CHECK (plan.landmarks.back ().kind ==
               game::CinematicLandmarkKind::Arrival);
}

MOPPE_TEST (cinematic_rotorcraft_stays_clear_and_accepts_live_trim) {
  FlightFixture fixture;
  const game::CinematicFlightPlan plan =
    game::plan_cinematic_flight (fixture.map,
                                 fixture.flood,
                                 fixture.census,
                                 fixture.drainage,
                                 fixture.rivers,
                                 Vec3 (200, 80, 200));
  game::CinematicFlight flight;
  flight.start (plan);
  MOPPE_CHECK (flight.active ());

  for (int frame = 0; frame < 12000 && flight.active (); ++frame) {
    const game::CinematicFlightControls controls {
      .lateral = frame < 180 ? 0.6f : 0.0f,
      .lift = frame >= 180 && frame < 360 ? 0.4f : 0.0f,
      .pace = frame < 360 ? 0.25f : 0.0f
    };
    flight.tick (1.0f / 60.0f, fixture.map, controls);
    const Vec3 p = flight.position ();
    MOPPE_CHECK (p[1] >= fixture.map.interpolated_height (p[0], p[2]) + 17.9f);
    MOPPE_CHECK (std::isfinite (flight.forward ()[0]));
  }
  MOPPE_CHECK (flight.elapsed () > 1.0f);
  MOPPE_CHECK (!flight.active ());
}
