#include <moppe/game/game_session.hh>
#include <moppe/game/graphics_benchmark.hh>
#include <moppe/map/surface.hh>

#include <tests/test.hh>

#include <algorithm>
#include <optional>
#include <vector>

namespace {
  void check_input (const moppe::game::InputFrame& actual,
                    const moppe::game::InputFrame& expected) {
    MOPPE_CHECK_NEAR (moppe::game::input_value (actual.turn),
                      moppe::game::input_value (expected.turn),
                      0.0f);
    MOPPE_CHECK_NEAR (moppe::game::input_value (actual.drive),
                      moppe::game::input_value (expected.drive),
                      0.0f);
    MOPPE_CHECK_NEAR (moppe::game::input_value (actual.boost),
                      moppe::game::input_value (expected.boost),
                      0.0f);
    MOPPE_CHECK (actual.deploy_glider == expected.deploy_glider);
    MOPPE_CHECK (actual.toggle_mount == expected.toggle_mount);
    MOPPE_CHECK (actual.cycle_camera == expected.cycle_camera);
    MOPPE_CHECK (actual.leave_cinematic == expected.leave_cinematic);
  }

  void check_benchmark_vector (const moppe::Vec3& actual,
                               const moppe::Vec3& expected) {
    MOPPE_CHECK_NEAR (actual[0], expected[0], 1e-6f);
    MOPPE_CHECK_NEAR (actual[1], expected[1], 1e-6f);
    MOPPE_CHECK_NEAR (actual[2], expected[2], 1e-6f);
  }

  void check_state (const moppe::game::GameState& actual,
                    const moppe::game::GameState& expected) {
    MOPPE_CHECK (actual.logic.m_mode == expected.logic.m_mode);
    MOPPE_CHECK (actual.logic.m_cam_mode == expected.logic.m_cam_mode);
    MOPPE_CHECK_NEAR (static_cast<float> (actual.logic.m_odometer),
                      static_cast<float> (expected.logic.m_odometer),
                      1e-6f);
    check_benchmark_vector (moppe::position_value (actual.vehicle.position),
                            moppe::position_value (expected.vehicle.position));
    check_benchmark_vector (moppe::velocity_value (actual.vehicle.velocity),
                            moppe::velocity_value (expected.vehicle.velocity));
    check_benchmark_vector (actual.vehicle.heading, expected.vehicle.heading);
    check_benchmark_vector (moppe::position_value (actual.camera.position),
                            moppe::position_value (expected.camera.position));
    check_benchmark_vector (moppe::position_value (actual.camera.target),
                            moppe::position_value (expected.camera.target));
    MOPPE_CHECK (actual.dust.next_id == expected.dust.next_id);
    MOPPE_CHECK (actual.dust.emissions.size () ==
                 expected.dust.emissions.size ());
  }
}

MOPPE_TEST (graphics_benchmark_replay_reuses_the_public_session_tape) {
  using namespace moppe;

  const game::GraphicsBenchmarkReplay::Config config {
    .prelude_frames = 2,
    .settle_frames = 1,
    .measured_frames = 2,
  };
  game::GraphicsBenchmarkReplay replay (config);
  MOPPE_CHECK (replay.prelude_tape ().size () == 2);
  MOPPE_CHECK (replay.replay_tape ().size () == 3);
  for (int frame = 0; frame < config.prelude_frames; ++frame)
    check_input (replay.prelude_tape ()[frame], game::benchmark_input (frame));
  for (int frame = 0; frame < config.settle_frames + config.measured_frames;
       ++frame)
    check_input (replay.replay_tape ()[frame], game::benchmark_input (frame));

  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (200, 0, 200))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.5f) * 20.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (Vec3 (200, 20, 200));
  world.resolution = static_cast<int> (surface.domain ().width ());
  world.water_level = 0 * u::m;
  std::vector<mov::Box> obstacles;
  game::GameSession session (world, surface);

  std::optional<game::GameState> checkpoint;
  std::vector<game::GameState> epoch_ends;
  std::vector<int> replay_frames (replay.configuration_count (), 0);
  int prelude_frames = 0;
  int measured_output_count = 0;
  int prelude_complete_count = 0;
  int epoch_complete_count = 0;
  int complete_count = 0;

  while (const std::optional<game::GraphicsBenchmarkReplay::Frame> frame =
           replay.current_frame ()) {
    if (frame->prelude) {
      MOPPE_CHECK (frame->epoch == 0);
      MOPPE_CHECK (frame->partition_mask == 0);
      MOPPE_CHECK (frame->logical_frame == prelude_frames);
      MOPPE_CHECK (!frame->measured);
      check_input (frame->input, replay.prelude_tape ()[prelude_frames]);
      ++prelude_frames;
    } else {
      MOPPE_CHECK (checkpoint.has_value ());
      MOPPE_CHECK (frame->epoch >= 0);
      MOPPE_CHECK (frame->epoch < replay.configuration_count ());
      MOPPE_CHECK (frame->partition_mask == game::gray_code (frame->epoch));
      MOPPE_CHECK (frame->logical_frame == replay_frames[frame->epoch]);
      MOPPE_CHECK (frame->measured ==
                   (frame->logical_frame >= config.settle_frames));
      check_input (frame->input, replay.replay_tape ()[frame->logical_frame]);
      ++replay_frames[frame->epoch];
      if (frame->measured)
        ++measured_output_count;
    }

    const game::GameSessionAdvanceResult result =
      advance_game_session (world,
                            surface,
                            obstacles,
                            session,
                            frame->input,
                            seconds (game::GRAPHICS_BENCHMARK_DT));
    MOPPE_CHECK (!result.say_ouchies);

    switch (replay.finish_frame ()) {
    case game::GraphicsBenchmarkReplay::Boundary::none:
      break;
    case game::GraphicsBenchmarkReplay::Boundary::prelude_complete:
      checkpoint = session.state ();
      ++prelude_complete_count;
      break;
    case game::GraphicsBenchmarkReplay::Boundary::epoch_complete:
      epoch_ends.push_back (session.state ());
      session.restore (*checkpoint);
      ++epoch_complete_count;
      break;
    case game::GraphicsBenchmarkReplay::Boundary::complete:
      epoch_ends.push_back (session.state ());
      ++complete_count;
      break;
    }
  }

  MOPPE_CHECK (prelude_frames == config.prelude_frames);
  MOPPE_CHECK (prelude_complete_count == 1);
  MOPPE_CHECK (epoch_complete_count == replay.configuration_count () - 1);
  MOPPE_CHECK (complete_count == 1);
  MOPPE_CHECK (epoch_ends.size () ==
               static_cast<std::size_t> (replay.configuration_count ()));
  MOPPE_CHECK (measured_output_count ==
               config.measured_frames * replay.configuration_count ());
  for (const int frame_count : replay_frames)
    MOPPE_CHECK (frame_count == config.settle_frames + config.measured_frames);

  MOPPE_CHECK (checkpoint.has_value ());
  MOPPE_CHECK (epoch_ends.front ().logic.m_odometer >
               checkpoint->logic.m_odometer);
  for (const game::GameState& end : epoch_ends)
    check_state (end, epoch_ends.front ());
}

// A block is a claim about what the cube can separate. Undergrowth is a
// whole geometry pass, so lumping it in with the small effects would make
// its cost unmeasurable -- which is the only reason the cube exists.
MOPPE_TEST (undergrowth_is_its_own_benchmark_dimension) {
  using namespace moppe::game;
  const auto block =
    graphics_benchmark_partition (GraphicsFeatureId::undergrowth);
  MOPPE_CHECK (RidingGraphicsPartition::name (block) == "undergrowth");
  for (const GraphicsFeature* feature : graphics_features) {
    if (!graphics_benchmark_includes (*feature))
      continue;
    if (feature->id == GraphicsFeatureId::undergrowth)
      continue;
    MOPPE_CHECK (graphics_benchmark_partition (feature->id) != block);
  }

  // Every hot riding feature still lands in some declared block, and the
  // cube covers every combination of them.
  MOPPE_CHECK (graphics_benchmark_dimension_count () ==
               static_cast<int> (RidingGraphicsPartition::blocks.size ()));
}
