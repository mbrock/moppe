#include <moppe/game/game_session.hh>
#include <moppe/game/rescue.hh>
#include <moppe/map/surface.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {
  using namespace moppe;

  game::MachineBody jeep_at (const Vec3& position, const Vec3& velocity) {
    game::MachineBody body;
    body.machine = game::Machine::jeep;
    body.position = position;
    body.velocity = velocity;
    body.mass = 1120.0f;
    return body;
  }

  // A 256 m square at four-metre spacing, its height given by a function
  // of the metre coordinates.
  struct Ground {
    map::SurfaceGeometry surface;
    game::WorldParams world;
  };

  Ground rescue_ground (const std::function<float (float, float)>& height) {
    Ground g { map::SurfaceGeometry (terrain::TerrainDomain (
                 65, 65, spatial_extent_in_metres (Vec3 (256, 0, 256)))),
               {} };
    for (std::size_t z = 0; z < g.surface.domain ().height (); ++z)
      for (std::size_t x = 0; x < g.surface.domain ().width (); ++x)
        spatial::get<terrain::surface_elevation> (
          g.surface[terrain::TerrainIndex { x, z }]) =
          terrain::surface_elevation_point (
            height (4.0f * static_cast<float> (x),
                    4.0f * static_cast<float> (z)) *
            mp_units::si::metre);
    map::rebuild_geometry (g.surface);
    g.world.map_size = spatial_extent_in_metres (Vec3 (256, 60, 256));
    g.world.resolution = static_cast<int> (g.surface.domain ().width ());
    g.world.water_level = 0 * u::m;
    return g;
  }

  struct Runner {
    const Ground& ground;
    game::GameSession& session;
    void operator() (game::InputFrame input, float seconds_held) {
      for (int i = 0; i < static_cast<int> (seconds_held * 60.0f); ++i) {
        game::advance_game_session (
          ground.world, ground.surface, session, input, seconds (1.0f / 60));
        input.toggle_mount = false;
      }
    }
  };
}

MOPPE_TEST (machines_that_meet_are_knocked_apart_by_their_masses) {
  using namespace moppe;

  // Two jeeps overlapping head on, closing at 4 m/s.
  const std::vector<game::MachineBody> bodies {
    jeep_at (Vec3 (0, 0, 0), Vec3 (0, 0, 2)),
    jeep_at (Vec3 (0, 0, 3.5f), Vec3 (0, 0, -2)),
  };
  const std::vector<game::Bump> bumps = game::find_bumps (bodies);
  MOPPE_CHECK (bumps.size () == 1);
  const game::Bump& bump = bumps[0];
  MOPPE_CHECK_NEAR (bump.speed, 4.0f, 1e-4f);
  // The first is sent back, and more than enough to stop it: they part.
  MOPPE_CHECK (bump.impulse[2] < -2.0f * 1120.0f);
  MOPPE_CHECK (std::abs (bump.impulse[0]) < 1e-3f);

  // Apart, or neither of them ours, nothing.
  const std::vector<game::MachineBody> apart {
    jeep_at (Vec3 (0, 0, 0), Vec3 (0, 0, 2)),
    jeep_at (Vec3 (0, 0, 6.0f), Vec3 (0, 0, -2)),
  };
  MOPPE_CHECK (game::find_bumps (apart).empty ());
  std::vector<game::MachineBody> strangers = bodies;
  strangers[0].ours = strangers[1].ours = false;
  MOPPE_CHECK (game::find_bumps (strangers).empty ());

  // A light bike meeting a heavy tractor: each takes the same blow, so
  // it is the bike whose speed changes.
  game::MachineBody bike = jeep_at (Vec3 (0, 0, 0), Vec3 (3, 0, 0));
  bike.machine = game::Machine::bike;
  bike.position[1] = 0.6f;
  bike.mass = 150.0f;
  game::MachineBody tractor = jeep_at (Vec3 (1.3f, 0, 0), Vec3 ());
  tractor.machine = game::Machine::tractor;
  tractor.position[1] = -0.2f;
  tractor.mass = 1840.0f;
  const std::vector<game::MachineBody> pair { bike, tractor };
  const std::vector<game::Bump> knock = game::find_bumps (pair);
  MOPPE_CHECK (knock.size () == 1);
  const float bike_change = knock[0].impulse[0] / bike.mass;
  const float tractor_change = -knock[0].impulse[0] / tractor.mass;
  MOPPE_CHECK (bike_change < -3.0f);
  MOPPE_CHECK (tractor_change > 0.0f && tractor_change < 0.5f);
}

MOPPE_TEST (a_machine_that_gets_nowhere_on_the_throttle_is_stuck) {
  using namespace moppe;

  game::Trouble trouble;
  const float dt = 1.0f / 60.0f;
  // Moving freely, never.
  for (int i = 0; i < 600; ++i)
    MOPPE_CHECK (!game::strain (trouble, Vec3 (0, 0, i * 0.05f), 1.0f, dt));
  MOPPE_CHECK (!trouble.stuck);

  // Standing still with the throttle open, stuck after three seconds,
  // and heaving at once and then every second and a half.
  trouble = {};
  int heaves = 0;
  for (int i = 0; i < 60 * 6; ++i)
    if (game::strain (trouble, Vec3 (0.1f, 0, 0), 1.0f, dt))
      ++heaves;
  MOPPE_CHECK (trouble.stuck);
  MOPPE_CHECK (heaves == 2 || heaves == 3);

  // Standing still with no one trying, it stays as it was.
  game::Trouble idle;
  for (int i = 0; i < 600; ++i)
    game::strain (idle, Vec3 (), 0.0f, dt);
  MOPPE_CHECK (!idle.stuck);

  // Got three metres away, it is free again.
  game::strain (trouble, Vec3 (4.0f, 0, 0), 0.0f, dt);
  MOPPE_CHECK (!trouble.stuck);

  // A heave goes the way the driver wants, and up.
  const Vec3 back = game::heave (trouble, Vec3 (0, 0.3f, 1), -1.0f);
  MOPPE_CHECK (back[2] < -1.0f && back[1] > 1.0f);
}

MOPPE_TEST (a_tractor_driven_into_a_wall_gets_stuck_and_heaves) {
  using namespace moppe;

  // Level ground to z = 200, then a cliff.
  const Ground ground = rescue_ground (
    [] (float, float z) { return 10.0f + std::max (0.0f, z - 200.0f) * 3.0f; });
  game::GameSession session (ground.world, ground.surface);
  Runner run { ground, session };
  game::GameLogicState& logic = session.logic ();
  session.start_on_foot ();
  session.tractor ().place (Vec3 (60, 0, 194), Vec3 (0, 0, 1));
  session.walker ().spawn (position (Vec3 (62, 10.1f, 194)), Vec3 (0, 0, 1));
  run ({ .toggle_mount = true }, 0.1f);
  MOPPE_CHECK (logic.m_mode == game::M_TRACTOR);
  run ({ .drive = 1.0f }, 6.0f);
  const game::Trouble& trouble =
    logic.m_trouble[static_cast<std::size_t> (game::Machine::tractor)];
  MOPPE_CHECK (trouble.stuck);
  MOPPE_CHECK (trouble.heaves > 0);
  MOPPE_CHECK (session.stuck ());
}

MOPPE_TEST (a_stuck_tractor_is_hauled_out_on_a_rope) {
  using namespace moppe;

  const Ground ground = rescue_ground ([] (float, float) { return 10.0f; });
  game::GameSession session (ground.world, ground.surface);
  Runner run { ground, session };
  game::GameLogicState& logic = session.logic ();
  session.start_on_foot ();
  session.tractor ().place (Vec3 (60, 0, 180), Vec3 (0, 0, 1));
  run ({}, 1.0f);
  const Vec3 start = session.tractor ().position ();
  game::Trouble& trouble =
    logic.m_trouble[static_cast<std::size_t> (game::Machine::tractor)];
  trouble = { .anchor = start, .strain = 4.0f, .stuck = true };

  // Behind it, facing away: F takes up the rope rather than climbing up.
  session.walker ().spawn (position (Vec3 (60, 10.1f, 177.5f)),
                           Vec3 (0, 0, -1));
  MOPPE_CHECK (session.haul_within_reach () == game::Machine::tractor);
  run ({ .toggle_mount = true }, 0.1f);
  MOPPE_CHECK (logic.m_mode == game::M_FOOT);
  MOPPE_CHECK (session.hauling () == game::Machine::tractor);

  // Walking off, the tractor comes after, with the walker never more
  // than the rope's length ahead of it.
  run ({ .drive = 1.0f }, 8.0f);
  const Vec3 moved = session.tractor ().position () - start;
  MOPPE_CHECK (moved[2] < -6.0f);
  MOPPE_CHECK (session.tractor ().up ()[1] > 0.9f);
  Vec3 rope = session.walker ().position () - session.rope_end ();
  rope[1] = 0.0f;
  MOPPE_CHECK (length (rope) < logic.m_haul.rope + game::rope_give + 0.3f);

  // F lets go, and it is no longer stuck: F again climbs up.
  run ({ .toggle_mount = true }, 0.1f);
  MOPPE_CHECK (!session.hauling ());
  MOPPE_CHECK (!trouble.stuck);
  run ({ .toggle_mount = true }, 0.1f);
  MOPPE_CHECK (logic.m_mode == game::M_TRACTOR);
}

MOPPE_TEST (a_boat_run_up_on_the_land_is_dragged_by_its_bow_handle) {
  using namespace moppe;

  const Ground ground = rescue_ground ([] (float, float) { return 10.0f; });
  game::GameSession session (ground.world, ground.surface);
  Runner run { ground, session };
  game::GameLogicState& logic = session.logic ();
  session.start_on_foot ();
  session.boat ().place (Vec3 (60, 10, 180), Vec3 (0, 0, 1), -100.0f);
  run ({}, 2.0f);
  MOPPE_CHECK (session.boat ().grounded () && !session.boat ().afloat ());
  const Vec3 start = session.boat ().position ();

  // Ahead of its bow: F takes up the rope to the handle rather than
  // taking the helm of a boat that cannot go anywhere.
  session.walker ().spawn (position (Vec3 (60, 10.1f, 184.0f)),
                           Vec3 (0, 0, 1));
  MOPPE_CHECK (session.haul_within_reach () == game::Machine::boat);
  run ({ .toggle_mount = true }, 0.1f);
  MOPPE_CHECK (logic.m_mode == game::M_FOOT);
  MOPPE_CHECK (session.hauling () == game::Machine::boat);
  MOPPE_CHECK (logic.m_haul.tie[2] > 2.0f);

  // Walking off, the boat slides after on its bottom, upright.
  run ({ .drive = 1.0f }, 8.0f);
  const Vec3 moved = session.boat ().position () - start;
  MOPPE_CHECK (moved[2] > 6.0f);
  MOPPE_CHECK (session.boat ().up ()[1] > 0.9f);
}

MOPPE_TEST (the_jeep_bumps_the_tractor_out_of_its_way) {
  using namespace moppe;

  const Ground ground = rescue_ground ([] (float, float) { return 10.0f; });
  game::GameSession session (ground.world, ground.surface);
  Runner run { ground, session };
  game::GameLogicState& logic = session.logic ();
  session.start_on_foot ();
  session.jeep ().place (Vec3 (100, 0, 150), Vec3 (0, 0, 1));
  session.tractor ().place (Vec3 (100, 0, 170), Vec3 (1, 0, 0));
  run ({}, 1.0f);
  const Vec3 tractor_was = session.tractor ().position ();
  session.walker ().spawn (position (Vec3 (102, 10.1f, 150)), Vec3 (0, 0, 1));
  run ({ .toggle_mount = true }, 0.1f);
  MOPPE_CHECK (logic.m_mode == game::M_JEEP);

  // Into its side at speed: the tractor is shoved on, neither flies, and
  // the jeep does not pass through it.
  float fastest = 0.0f;
  for (int i = 0; i < 4 * 60; ++i) {
    run ({ .drive = 1.0f }, 1.0f / 60.0f);
    fastest = std::max (fastest, length (session.tractor ().velocity ()));
  }
  const Vec3 shoved = session.tractor ().position () - tractor_was;
  MOPPE_CHECK (shoved[2] > 1.0f);
  MOPPE_CHECK (fastest < 15.0f);
  MOPPE_CHECK (session.tractor ().position ()[1] < tractor_was[1] + 2.0f);
  MOPPE_CHECK (session.jeep ().position ()[2] <
               session.tractor ().position ()[2]);
}
