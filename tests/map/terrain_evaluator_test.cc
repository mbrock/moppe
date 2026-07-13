#include <moppe/map/generate.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/mov/vehicle.hh>
#include <moppe/terrain/program.hh>
#include <moppe/terrain/topology.hh>

#include <tests/test.hh>

#include <bit>
#include <cmath>
#include <cstdint>

namespace {
  bool maps_match (const moppe::map::RandomHeightMap& left,
                   const moppe::map::RandomHeightMap& right) {
    if (left.width () != right.width () || left.height () != right.height ())
      return false;
    for (int y = 0; y < left.height (); ++y)
      for (int x = 0; x < left.width (); ++x)
        if (std::bit_cast<std::uint32_t> (left.get (x, y)) !=
            std::bit_cast<std::uint32_t> (right.get (x, y)))
          return false;
    return true;
  }

  void evaluate (moppe::map::RandomHeightMap& target,
                 const moppe::terrain::TerrainProgram& program) {
    moppe::map::TerrainEvaluator (target).evaluate (program);
  }
}

MOPPE_TEST (heightmap_materializes_an_arbitrary_scalar_field) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::RandomHeightMap map (3, 3, Vec3 (1, 1, 1), 0);

  map.materialize (coordinate_x () + 2.0f * coordinate_y ());

  MOPPE_CHECK_NEAR (map.get (0, 0), 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (map.get (1, 1), 1.5f, 1e-6f);
  MOPPE_CHECK_NEAR (map.get (2, 2), 3.0f, 1e-6f);
}

MOPPE_TEST (evaluator_replays_the_direct_generation_sequence) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vec3 size (1, 1, 1);
  map::RandomHeightMap legacy (65, 65, size, 123);
  legacy.randomize_geologically ();
  legacy.exponentiate (1.15f);
  legacy.erode_hydraulically (200);
  legacy.erode_thermally (1, 0.003f);

  TerrainProgram program = make_geological_program (123);
  program.transforms.emplace_back (PowerHeights { 1.15f });
  program.transforms.emplace_back (HydraulicErosion { droplet_count (200) });
  program.transforms.emplace_back (
    ThermalErosion { iteration_count (1), 0.003f });
  map::RandomHeightMap replayed (65, 65, size, 999);
  evaluate (replayed, program);

  MOPPE_CHECK (maps_match (legacy, replayed));
}

MOPPE_TEST (evaluating_a_program_twice_is_deterministic) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vec3 size (1, 1, 1);
  TerrainProgram program = make_geological_program (77);
  program.transforms.emplace_back (HydraulicErosion { droplet_count (100) });
  map::RandomHeightMap first (33, 33, size, 0);
  map::RandomHeightMap second (33, 33, size, 1);

  evaluate (first, program);
  evaluate (second, program);
  MOPPE_CHECK (maps_match (first, second));
}

MOPPE_TEST (evaluator_checkpoint_resume_matches_complete_replay) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vec3 size (100, 20, 100);
  TerrainProgram program = make_geological_program (77);
  program.transforms.emplace_back (PowerHeights { 1.15f });
  program.transforms.emplace_back (HydraulicErosion {
    .droplets = droplet_count (257), .batch_size = batch_size (64) });
  program.transforms.emplace_back (
    ThermalErosion { iteration_count (2), 0.003f });
  map::RandomHeightMap replayed (65, 65, size, 0, Topology::Torus);
  map::RandomHeightMap resumed (65, 65, size, 0, Topology::Torus);

  evaluate (replayed, program);
  map::TerrainEvaluator evaluator (resumed);
  evaluator.begin (program);
  evaluator.apply (program.transforms[0]);
  const map::TerrainCheckpoint checkpoint = evaluator.checkpoint ();
  evaluator.apply (HydraulicErosion { .droplets = droplet_count (3),
                                      .batch_size = batch_size (1) });
  evaluator.restore (checkpoint);
  for (std::size_t i = 1; i < program.transforms.size (); ++i)
    evaluator.apply (program.transforms[i]);

  MOPPE_CHECK (maps_match (replayed, resumed));
}

MOPPE_TEST (periodic_program_preserves_height_and_normal_seams) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vec3 size (5000, 650, 3000);
  map::RandomHeightMap map (65, 33, size, 123, Topology::Torus);
  TerrainProgram program = make_geological_program (123);
  program.transforms.emplace_back (PowerHeights { 1.15f });
  program.transforms.emplace_back (HydraulicErosion { droplet_count (400) });
  program.transforms.emplace_back (
    ThermalErosion { iteration_count (2), 0.003f });
  evaluate (map, program);
  map.recompute_normals ();

  MOPPE_CHECK (map.periodic ());
  MOPPE_CHECK_NEAR (map.size ()[0], size[0], 1e-4f);
  MOPPE_CHECK_NEAR (map.size ()[2], size[2], 1e-4f);
  for (int y = 0; y < map.height (); ++y) {
    MOPPE_CHECK (map.get (0, y) == map.get (map.width () - 1, y));
    const Vec3 a = map.normal (0, y);
    const Vec3 b = map.normal (map.width () - 1, y);
    MOPPE_CHECK_NEAR (a[0], b[0], 1e-6f);
    MOPPE_CHECK_NEAR (a[1], b[1], 1e-6f);
    MOPPE_CHECK_NEAR (a[2], b[2], 1e-6f);
  }
  for (int x = 0; x < map.width (); ++x) {
    MOPPE_CHECK (map.get (x, 0) == map.get (x, map.height () - 1));
    const Vec3 a = map.normal (x, 0);
    const Vec3 b = map.normal (x, map.height () - 1);
    MOPPE_CHECK_NEAR (a[0], b[0], 1e-6f);
    MOPPE_CHECK_NEAR (a[1], b[1], 1e-6f);
    MOPPE_CHECK_NEAR (a[2], b[2], 1e-6f);
  }

  constexpr float x = 1234.5f;
  constexpr float z = 987.25f;
  const float h = map.interpolated_height (x, z);
  MOPPE_CHECK_NEAR (
    map.interpolated_height (x + size[0], z - size[2]), h, 1e-4f);
  MOPPE_CHECK (map.in_bounds (-100000.0f, 100000.0f));
}

MOPPE_TEST (vehicle_coordinates_remain_unwrapped_on_the_torus) {
  using namespace moppe;
  map::RandomHeightMap map (
    9, 9, Vec3 (100, 20, 100), 1, terrain::Topology::Torus);
  map.randomize_geologically ();
  map.recompute_normals ();
  mov::Vehicle vehicle (position (Vec3 (112.5f, 0, -7.5f)),
                        0 * u::deg,
                        map,
                        1000 * u::N,
                        10 * u::kW,
                        100 * u::kg);

  vehicle.update (seconds (0.0f));

  MOPPE_CHECK_NEAR (vehicle.position ()[0], 112.5f, 1e-6f);
  MOPPE_CHECK_NEAR (vehicle.position ()[2], -7.5f, 1e-6f);
  MOPPE_CHECK_NEAR (
    map.interpolated_height (vehicle.position ()[0], vehicle.position ()[2]),
    map.interpolated_height (12.5f, 92.5f),
    1e-5f);
}

MOPPE_TEST (periodic_hydraulic_batches_are_deterministic) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vec3 size (100, 20, 100);
  TerrainProgram program = make_geological_program (987);
  program.transforms.emplace_back (HydraulicErosion {
    .droplets = droplet_count (513), .batch_size = batch_size (64) });
  map::RandomHeightMap first (65, 65, size, 0, Topology::Torus);
  map::RandomHeightMap second (65, 65, size, 1, Topology::Torus);

  evaluate (first, program);
  evaluate (second, program);

  MOPPE_CHECK (maps_match (first, second));
  for (int i = 0; i < first.width (); ++i)
    MOPPE_CHECK (first.get (i, 0) == first.get (i, first.height () - 1));
}

MOPPE_TEST (hydraulic_progress_reports_each_completed_batch) {
  using namespace moppe;
  using namespace moppe::terrain;
  TerrainProgram program = make_geological_program (123);
  program.transforms.emplace_back (HydraulicErosion {
    .droplets = droplet_count (130), .batch_size = batch_size (64) });
  map::RandomHeightMap map (65, 65, Vec3 (100, 20, 100), 0, Topology::Torus);
  std::vector<int> completed;
  map::TerrainEvaluator evaluator (map);
  evaluator.evaluate (
    program,
    {},
    [&completed] (std::size_t, const TerrainTransform&, int done, int total) {
      MOPPE_CHECK (total == 130);
      completed.push_back (done);
    });

  MOPPE_CHECK (completed == std::vector<int> ({ 64, 128, 130 }));
}

MOPPE_TEST (periodic_analytical_erosion_is_deterministic_and_seam_safe) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vec3 size (5000, 650, 5000);
  TerrainProgram program = make_geological_program (2468);
  program.transforms.emplace_back (PowerHeights { 1.15f });
  program.transforms.emplace_back (AnalyticalErosion {
    .duration = 100000.0f * mp_units::astronomy::Julian_year,
    .fixed_point_iterations = iteration_count (2),
    .relaxation = 0.5f });
  map::RandomHeightMap first (65, 65, size, 0, Topology::Torus);
  map::RandomHeightMap second (65, 65, size, 1, Topology::Torus);
  map::TerrainEvaluator first_evaluator (first);
  map::TerrainEvaluator second_evaluator (second);

  first_evaluator.begin (program);
  second_evaluator.begin (program);
  TerrainTransformReport first_report;
  TerrainTransformReport second_report;
  for (const TerrainTransform& transform : program.transforms) {
    first_report = first_evaluator.apply (transform);
    second_report = second_evaluator.apply (transform);
  }

  MOPPE_CHECK (maps_match (first, second));
  const auto& report = std::get<AnalyticalErosionReport> (first_report);
  MOPPE_CHECK (report.lowered_volume > 0.0);
  MOPPE_CHECK (report.fixed_point_iterations == iteration_count (2));
  for (int i = 0; i < first.width (); ++i) {
    MOPPE_CHECK (first.get (i, 0) == first.get (i, first.height () - 1));
    MOPPE_CHECK (first.get (0, i) == first.get (first.width () - 1, i));
  }
  MOPPE_CHECK (
    std::get<AnalyticalErosionReport> (second_report).lowered_volume ==
    report.lowered_volume);
}

MOPPE_TEST (hydraulic_batch_size_is_part_of_the_recipe) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vec3 size (100, 20, 100);
  TerrainProgram serial = make_geological_program (321);
  serial.transforms.emplace_back (HydraulicErosion {
    .droplets = droplet_count (256), .batch_size = batch_size (1) });
  TerrainProgram batched = serial;
  std::get<HydraulicErosion> (batched.transforms.back ()).batch_size =
    batch_size (64);
  map::RandomHeightMap one_at_a_time (65, 65, size, 0, Topology::Torus);
  map::RandomHeightMap sixty_four_at_a_time (65, 65, size, 0, Topology::Torus);

  evaluate (one_at_a_time, serial);
  evaluate (sixty_four_at_a_time, batched);

  MOPPE_CHECK (!maps_match (one_at_a_time, sixty_four_at_a_time));
}

MOPPE_TEST (path_monotone_carving_is_an_explicit_recipe_choice) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vec3 size (100, 20, 100);
  map::RandomHeightMap unconstrained_map (65, 65, size, 0, Topology::Torus);
  map::RandomHeightMap monotone_map (65, 65, size, 0, Topology::Torus);
  map::TerrainEvaluator unconstrained (unconstrained_map);
  map::TerrainEvaluator monotone (monotone_map);
  const TerrainProgram source = make_geological_program (731);
  unconstrained.begin (source);
  monotone.begin (source);
  const HydraulicErosion erosion { .droplets = droplet_count (2000),
                                   .batch_size = batch_size (64),
                                   .max_steps = step_count (128),
                                   .carving_rule = CarvingRule::Unconstrained };
  const auto old_result = unconstrained.apply (erosion);
  HydraulicErosion constrained = erosion;
  constrained.carving_rule = CarvingRule::PathMonotone;
  const auto new_result = monotone.apply (constrained);
  const auto& old_report = std::get<HydraulicErosionReport> (old_result);
  const auto& new_report = std::get<HydraulicErosionReport> (new_result);

  MOPPE_CHECK (!maps_match (unconstrained_map, monotone_map));
  MOPPE_CHECK (new_report.eroded <= old_report.eroded);
}

MOPPE_TEST (hydraulic_report_balances_the_sediment_ledger) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::RandomHeightMap map (65, 65, Vec3 (100, 20, 100), 0, Topology::Torus);
  map::TerrainEvaluator evaluator (map);
  const TerrainProgram source = make_geological_program (912);
  evaluator.begin (source);
  const TerrainTransformReport result =
    evaluator.apply (HydraulicErosion { .droplets = droplet_count (1000),
                                        .batch_size = batch_size (1),
                                        .max_steps = step_count (64) });
  const auto& report = std::get<HydraulicErosionReport> (result);

  MOPPE_CHECK (report.droplets == event_count (1000));
  MOPPE_CHECK (report.stopped_flat + report.stopped_at_boundary +
                 report.stopped_at_step_limit ==
               report.droplets);
  MOPPE_CHECK_NEAR (
    static_cast<float> (report.eroded),
    static_cast<float> (report.deposited + report.discarded_sediment),
    static_cast<float> (report.eroded * 2e-5 + 1e-7));
}

MOPPE_TEST (hillslope_diffusion_spreads_a_gaussian_and_conserves_volume) {
  using namespace moppe;
  using namespace moppe::terrain;
  // 1 m cell spacing: size 64 over 65 periodic samples.
  map::RandomHeightMap map (65, 65, Vec3 (64, 1, 64), 0, Topology::Torus);
  const float sigma2 = 9.0f;
  for (int y = 0; y < map.height (); ++y)
    for (int x = 0; x < map.width (); ++x) {
      const float dx = static_cast<float> (x) - 32.0f;
      const float dy = static_cast<float> (y) - 32.0f;
      map.set (x, y, std::exp (-(dx * dx + dy * dy) / (2.0f * sigma2)));
    }
  map.synchronize_periodic_edges ();

  const auto moments = [&map] {
    double mass = 0.0, var_x = 0.0;
    for (int y = 0; y < map.unique_height (); ++y)
      for (int x = 0; x < map.unique_width (); ++x) {
        const double h = map.get (x, y);
        const double dx = static_cast<double> (x) - 32.0;
        mass += h;
        var_x += h * dx * dx;
      }
    return std::pair { mass, var_x / mass };
  };
  const auto [mass_before, var_before] = moments ();

  const float d = 0.5f, years = 4.0f;
  const HillslopeDiffusionReport report =
    map.diffuse_hillslopes (years * mp_units::astronomy::Julian_year,
                            d * mp_units::si::metre * mp_units::si::metre /
                              mp_units::astronomy::Julian_year);

  const auto [mass_after, var_after] = moments ();
  // Volume conserved to float accumulation error.
  MOPPE_CHECK_NEAR (static_cast<float> (mass_after),
                    static_cast<float> (mass_before),
                    static_cast<float> (mass_before) * 1e-4f);
  // Analytic variance growth of a diffusing Gaussian: 2 D t per axis.
  MOPPE_CHECK_NEAR (
    static_cast<float> (var_after - var_before), 2.0f * d * years, 0.35f);
  MOPPE_CHECK (count_value (report.sweeps) > 0);
  MOPPE_CHECK_NEAR (
    static_cast<float> (cubic_meters_value (report.lowered_volume)),
    static_cast<float> (cubic_meters_value (report.raised_volume)),
    static_cast<float> (cubic_meters_value (report.raised_volume)) * 1e-3f +
      1e-5f);
}

MOPPE_TEST (hydraulic_erosion_accumulates_the_ledger_rasters) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::RandomHeightMap map (65, 65, Vec3 (100, 20, 100), 0, Topology::Torus);
  map::TerrainEvaluator evaluator (map);
  evaluator.begin (make_geological_program (912));
  const TerrainTransformReport result =
    evaluator.apply (HydraulicErosion { .droplets = droplet_count (1000),
                                        .batch_size = batch_size (1),
                                        .max_steps = step_count (64) });
  const auto& report = std::get<HydraulicErosionReport> (result);

  // The per-cell rasters carry exactly what the report sums globally,
  // and both stay nonnegative.  Droplets only touch unique cells, so
  // the unique-cell sums balance against the report totals.
  double eroded = 0.0, deposited = 0.0;
  for (int y = 0; y < map.unique_height (); ++y)
    for (int x = 0; x < map.unique_width (); ++x) {
      const std::size_t i = static_cast<std::size_t> (y) * map.width () + x;
      MOPPE_CHECK (map.raw_eroded ()[i] >= 0.0f);
      MOPPE_CHECK (map.raw_deposited ()[i] >= 0.0f);
      eroded += map.raw_eroded ()[i];
      deposited += map.raw_deposited ()[i];
    }
  MOPPE_CHECK_NEAR (static_cast<float> (eroded),
                    static_cast<float> (report.eroded),
                    static_cast<float> (report.eroded * 1e-4 + 1e-6));
  MOPPE_CHECK_NEAR (static_cast<float> (deposited),
                    static_cast<float> (report.deposited),
                    static_cast<float> (report.deposited * 1e-4 + 1e-6));

  // Seam duplication mirrors the height convention.
  for (int y = 0; y < map.unique_height (); ++y) {
    const std::size_t left = static_cast<std::size_t> (y) * map.width ();
    const std::size_t right = left + map.width () - 1;
    MOPPE_CHECK (map.raw_eroded ()[left] == map.raw_eroded ()[right]);
    MOPPE_CHECK (map.raw_deposited ()[left] == map.raw_deposited ()[right]);
  }

  // Checkpoints carry the ledger through restore.
  const map::TerrainCheckpoint checkpoint = evaluator.checkpoint ();
  map.reset_sediment_ledger ();
  evaluator.restore (checkpoint);
  double restored = 0.0;
  for (int y = 0; y < map.unique_height (); ++y)
    for (int x = 0; x < map.unique_width (); ++x)
      restored +=
        map.raw_eroded ()[static_cast<std::size_t> (y) * map.width () + x];
  MOPPE_CHECK_NEAR (
    static_cast<float> (restored), static_cast<float> (eroded), 1e-6f);

  // A fresh source materialization starts a fresh history.
  evaluator.begin (make_geological_program (913));
  for (int y = 0; y < map.height (); ++y)
    for (int x = 0; x < map.width (); ++x) {
      const std::size_t i = static_cast<std::size_t> (y) * map.width () + x;
      MOPPE_CHECK (map.raw_eroded ()[i] == 0.0f);
      MOPPE_CHECK (map.raw_deposited ()[i] == 0.0f);
    }
}

MOPPE_TEST (hydraulic_maximum_lifetime_is_part_of_the_recipe) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vec3 size (100, 20, 100);
  TerrainProgram short_lived = make_geological_program (654);
  short_lived.transforms.emplace_back (
    HydraulicErosion { .droplets = droplet_count (512),
                       .batch_size = batch_size (1),
                       .max_steps = step_count (1) });
  TerrainProgram long_lived = short_lived;
  std::get<HydraulicErosion> (long_lived.transforms.back ()).max_steps =
    step_count (128);
  map::RandomHeightMap short_map (65, 65, size, 0, Topology::Torus);
  map::RandomHeightMap long_map (65, 65, size, 0, Topology::Torus);

  evaluate (short_map, short_lived);
  evaluate (long_map, long_lived);

  MOPPE_CHECK (!maps_match (short_map, long_map));
}

MOPPE_TEST (water_termination_can_settle_the_remaining_sediment) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::RandomHeightMap map (65, 65, Vec3 (100, 20, 100), 0, Topology::Torus);
  map::TerrainEvaluator evaluator (map);
  evaluator.begin (make_geological_program (444));
  const auto result = evaluator.apply (HydraulicErosion {
    .droplets = droplet_count (1000),
    .batch_size = batch_size (1),
    .max_steps = step_count (512),
    .minimum_water = 0.01f,
    .sediment_at_termination = SedimentDisposition::Deposit });
  const auto& report = std::get<HydraulicErosionReport> (result);

  MOPPE_CHECK (report.stopped_at_water_cutoff + report.stopped_flat ==
               report.droplets);
  MOPPE_CHECK_NEAR (static_cast<float> (report.discarded_sediment), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (static_cast<float> (report.eroded),
                    static_cast<float> (report.deposited),
                    static_cast<float> (report.eroded * 2e-5 + 1e-7));
}

MOPPE_TEST (placed_hydraulic_droplet_traces_and_balances_sediment) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::RandomHeightMap map (33, 33, Vec3 (64, 20, 64), 0, Topology::Bounded);
  for (int y = 0; y < map.height (); ++y)
    for (int x = 0; x < map.width (); ++x)
      map.set (x, y, 0.9f - 0.02f * x + 0.0002f * y);

  const map::HydraulicDropletTrace trace =
    map.trace_hydraulic_droplet (8.5f,
                                 16.5f,
                                 128,
                                 0.01f,
                                 SedimentDisposition::Deposit,
                                 CarvingRule::PathMonotone);

  MOPPE_CHECK (trace.points.size () > 2);
  MOPPE_CHECK (trace.points.back ().x > trace.points.front ().x);
  MOPPE_CHECK (trace.eroded > 0.0f);
  MOPPE_CHECK_NEAR (
    trace.eroded, trace.deposited, trace.eroded * 2e-5f + 1e-7f);
}

MOPPE_TEST (path_monotone_droplet_settles_in_a_local_basin) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::RandomHeightMap map (33, 33, Vec3 (64, 20, 64), 0, Topology::Bounded);
  for (int y = 0; y < map.height (); ++y)
    for (int x = 0; x < map.width (); ++x) {
      const float dx = static_cast<float> (x - 16);
      map.set (x, y, 0.2f + 0.002f * dx * dx);
    }

  const map::HydraulicDropletTrace trace =
    map.trace_hydraulic_droplet (8.5f,
                                 16.5f,
                                 128,
                                 0.01f,
                                 SedimentDisposition::Deposit,
                                 CarvingRule::PathMonotone);

  // With momentum the droplet overshoots the basin floor, sloshes up
  // the far wall, and friction settles it near the bottom instead of
  // stopping dead at the first uphill cell.
  MOPPE_CHECK (trace.termination == map::HydraulicDropletTermination::Settled);
  MOPPE_CHECK (trace.points.back ().x >= 14.0f);
  MOPPE_CHECK (trace.points.back ().x <= 18.0f);
}
