#include <moppe/game/companion.hh>
#include <moppe/game/game_session.hh>
#include <moppe/mov/parachute.hh>
#include <moppe/mov/pogo.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>

using namespace moppe;

namespace {
  map::SurfaceGeometry pogo_test_ground () {
    map::SurfaceGeometry surface (terrain::TerrainDomain (
      33, 33, spatial_extent_in_metres (Vec3 (512, 0, 512))));
    std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                       terrain::surface_elevation_point (10.0f * u::m));
    map::rebuild_geometry (surface);
    return surface;
  }

  game::WorldParams pogo_test_world (const map::SurfaceGeometry& surface) {
    game::WorldParams world;
    world.map_size = spatial_extent_in_metres (Vec3 (512, 20, 512));
    world.resolution = static_cast<int> (surface.domain ().width ());
    world.water_level = 0.0f * u::m;
    return world;
  }

  const seconds_t pogo_test_step = seconds (1.0f / 60.0f);
}

MOPPE_TEST (pogo_stick_bounces_faster_than_running_and_pumps_higher) {
  const map::SurfaceGeometry surface = pogo_test_ground ();
  const game::WorldParams world = pogo_test_world (surface);
  game::GameSession session (world, surface);
  session.start_on_foot ();
  session.walker ().spawn (position (Vec3 (120, 10.0f, 120)),
                           unit_direction (Vec3 (0, 0, 1)));

  // Press B to hop onto the pogo stick.
  game::advance_game_session (world,
                              surface,
                              session,
                              game::InputFrame { .toggle_pogo = true },
                              pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::pogoing ());

  // Travel forward without pumping: should bounce briskly and outpace running.
  meters_t normal_peak = 0.0f * u::m;
  speed_t max_cruise = 0.0f * (u::m / u::s);
  for (int i = 0; i < 60 * 5; ++i) {
    game::advance_game_session (
      world,
      surface,
      session,
      game::InputFrame { .drive = 1.0f * control_signal[one] },
      pogo_test_step);
    const meters_t agl =
      (position_value (session.pogo ().position ())[1] - 10.0f) * u::m;
    normal_peak = std::max (normal_peak, agl);
    const Vec3& v = velocity_value (session.pogo ().velocity ());
    max_cruise = std::max (max_cruise, std::hypot (v[0], v[2]) * (u::m / u::s));
  }
  MOPPE_CHECK (normal_peak > 1.5f * u::m);
  MOPPE_CHECK (max_cruise > game::Walker::run_speed);

  // Hold Space while moving forward: consecutive bounces build comically high!
  meters_t pumped_peak = 0.0f * u::m;
  speed_t max_super = 0.0f * (u::m / u::s);
  for (int i = 0; i < 60 * 6; ++i) {
    game::advance_game_session (
      world,
      surface,
      session,
      game::InputFrame { .drive = 1.0f * control_signal[one],
                         .boost = 1.0f * proportion[one] },
      pogo_test_step);
    const meters_t agl =
      (position_value (session.pogo ().position ())[1] - 10.0f) * u::m;
    pumped_peak = std::max (pumped_peak, agl);
    const Vec3& v = velocity_value (session.pogo ().velocity ());
    max_super = std::max (max_super, std::hypot (v[0], v[2]) * (u::m / u::s));
  }
  MOPPE_CHECK (pumped_peak > normal_peak + 3.0f * u::m);
  MOPPE_CHECK (pumped_peak > 6.0f * u::m);
  MOPPE_CHECK (max_super > max_cruise);

  // Press F (or B) to step off and stow the pogo stick.
  game::advance_game_session (world,
                              surface,
                              session,
                              game::InputFrame { .toggle_mount = true },
                              pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::on_foot ());
}

MOPPE_TEST (parachute_deploys_from_freefall_flares_and_lands_on_foot) {
  const map::SurfaceGeometry surface = pogo_test_ground ();
  const game::WorldParams world = pogo_test_world (surface);
  game::GameSession session (world, surface);
  session.start_on_foot ();
  // Drop the walker from high up, falling briskly.
  session.walker ().spawn (position (Vec3 (120, 60.0f, 120)),
                           unit_direction (Vec3 (0, 0, 1)),
                           velocity (Vec3 (0, -18.0f, 4.0f)));
  MOPPE_CHECK (session.can_deploy_parachute (surface));

  // Press E to pop the parachute.
  game::advance_game_session (
    world, surface, session, game::InputFrame { .use = true }, pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::parachuting ());

  // Let it bloom and settle into a calm descent.
  for (int i = 0; i < 60 * 3; ++i)
    game::advance_game_session (
      world, surface, session, game::InputFrame {}, pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::parachuting ());
  const float cruise_sink =
    session.parachute ().vertical_speed ().numerical_value_in (u::m / u::s);
  MOPPE_CHECK (cruise_sink < -1.5f);
  MOPPE_CHECK (cruise_sink > -5.0f);

  // Flaring with Space slows the descent further.
  for (int i = 0; i < 60 * 2; ++i)
    game::advance_game_session (
      world,
      surface,
      session,
      game::InputFrame { .boost = 1.0f * proportion[one] },
      pogo_test_step);
  const float flared_sink =
    session.parachute ().vertical_speed ().numerical_value_in (u::m / u::s);
  MOPPE_CHECK (flared_sink > cruise_sink + 0.6f);

  // Cutting away returns to freefall, and Space mid-air re-deploys!
  game::advance_game_session (world,
                              surface,
                              session,
                              game::InputFrame { .toggle_mount = true },
                              pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::on_foot ());
  // Release boost first, then press Space mid-air to re-open.
  game::advance_game_session (
    world, surface, session, game::InputFrame {}, pogo_test_step);
  game::advance_game_session (
    world,
    surface,
    session,
    game::InputFrame { .boost = 1.0f * proportion[one] },
    pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::parachuting ());

  // Fly down until touchdown onto feet.
  int steps = 0;
  while (session.logic ().m_doing == game::Doing::parachuting () &&
         steps++ < 60 * 40)
    game::advance_game_session (
      world, surface, session, game::InputFrame {}, pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::on_foot ());
  MOPPE_CHECK (position_value (session.walker ().position ())[1] < 12.0f);
}

MOPPE_TEST (jumping_out_of_helicopter_or_airborne_bike_lets_you_parachute) {
  const map::SurfaceGeometry surface = pogo_test_ground ();
  const game::WorldParams world = pogo_test_world (surface);
  game::GameSession session (world, surface);
  session.start_on_foot ();

  // 1. Climb high in the helicopter, bail out with F, then open parachute with
  // E.
  session.helicopter ().place (position (Vec3 (180, 10, 180)),
                               unit_direction (Vec3 (0, 0, 1)));
  session.walker ().spawn (position (Vec3 (178, 10.1f, 180)),
                           unit_direction (Vec3 (0, 0, 1)));
  game::advance_game_session (world,
                              surface,
                              session,
                              game::InputFrame { .toggle_mount = true },
                              pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing ==
               game::Doing::driving (game::Machine::helicopter));
  for (int i = 0; i < 60 * 4; ++i)
    game::advance_game_session (
      world, surface, session, game::InputFrame {}, pogo_test_step);
  for (int i = 0; i < 60 * 3; ++i)
    game::advance_game_session (
      world,
      surface,
      session,
      game::InputFrame { .boost = 1.0f * proportion[one] },
      pogo_test_step);
  MOPPE_CHECK (session.helicopter ().altitude () > 10.0f * u::m);

  game::advance_game_session (world,
                              surface,
                              session,
                              game::InputFrame { .toggle_mount = true },
                              pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::on_foot ());
  MOPPE_CHECK (session.can_deploy_parachute (surface));

  game::advance_game_session (
    world, surface, session, game::InputFrame { .use = true }, pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::parachuting ());

  // 2. Airborne on the Trail 90: F jumps off the bike, then E opens parachute.
  game::take_over (session, game::Machine::bike);
  mov::Vehicle::State airborne = session.bike ().state ();
  airborne.position = position (Vec3 (140, 45, 140));
  airborne.velocity = velocity (Vec3 (0, 2, 18));
  airborne.airborne_time = seconds (0.3f);
  session.bike ().restore (airborne);
  game::advance_game_session (world,
                              surface,
                              session,
                              game::InputFrame { .toggle_mount = true },
                              pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::on_foot ());
  MOPPE_CHECK (session.can_deploy_parachute (surface));
  game::advance_game_session (
    world, surface, session, game::InputFrame { .use = true }, pogo_test_step);
  MOPPE_CHECK (session.logic ().m_doing == game::Doing::parachuting ());
}

MOPPE_TEST (pogo_and_parachute_roundtrip_state_and_companion_wire) {
  const map::SurfaceGeometry surface = pogo_test_ground ();
  const game::WorldParams world = pogo_test_world (surface);
  game::GameSession session (world, surface);
  session.start_on_foot ();
  session.walker ().spawn (position (Vec3 (120, 10.0f, 120)),
                           unit_direction (Vec3 (0, 0, 1)));

  game::advance_game_session (world,
                              surface,
                              session,
                              game::InputFrame { .toggle_pogo = true },
                              pogo_test_step);
  for (int i = 0; i < 30; ++i)
    game::advance_game_session (
      world,
      surface,
      session,
      game::InputFrame { .drive = 1.0f * control_signal[one],
                         .boost = 1.0f * proportion[one] },
      pogo_test_step);

  const game::GameState saved = session.state ();
  game::GameSession restored (world, surface);
  restored.restore (saved);
  MOPPE_CHECK (restored.logic ().m_doing == game::Doing::pogoing ());
  MOPPE_CHECK_NEAR (position_value (restored.pogo ().position ())[1],
                    position_value (session.pogo ().position ())[1],
                    1e-4f);

  // Companion wire round-trip for both pogo and parachute poses.
  game::CompanionSnapshot snap;
  snap.mode = game::Doing::pogoing ();
  snap.pogo = game::pogo_pose (session.pogo ());
  const auto pogo_wire = game::encode (snap);
  const auto pogo_decoded = game::decode (pogo_wire);
  MOPPE_CHECK (pogo_decoded.has_value ());
  MOPPE_CHECK (pogo_decoded->mode == game::Doing::pogoing ());
  MOPPE_CHECK (pogo_decoded->pogo.has_value ());

  session.parachute ().deploy (position (Vec3 (120, 50.0f, 120)),
                               velocity (Vec3 (0, -5, 6)),
                               unit_direction (Vec3 (0, 0, 1)));
  session.set_mode (game::Doing::parachuting ());
  game::CompanionSnapshot chute_snap;
  chute_snap.mode = game::Doing::parachuting ();
  chute_snap.parachute = game::parachute_pose (session.parachute ());
  const auto chute_wire = game::encode (chute_snap);
  const auto chute_decoded = game::decode (chute_wire);
  MOPPE_CHECK (chute_decoded.has_value ());
  MOPPE_CHECK (chute_decoded->mode == game::Doing::parachuting ());
  MOPPE_CHECK (chute_decoded->parachute.has_value ());
}
