#include <moppe/game/game_session.hh>
#include <moppe/mov/glider.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace moppe;

namespace {
  // Level ground at ten metres; the world is periodic, so a long glide
  // simply goes round.
  map::SurfaceGeometry glider_test_ground () {
    map::SurfaceGeometry surface (terrain::TerrainDomain (
      33, 33, spatial_extent_in_metres (Vec3 (512, 0, 512))));
    std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                       terrain::surface_elevation_point (10.0f * u::m));
    map::rebuild_geometry (surface);
    return surface;
  }

  const seconds_t glider_test_step = seconds (1.0f / 60.0f);

  void glider_test_fly (mov::Glider& glider, float duration) {
    const bool trace = std::getenv ("MOPPE_GLIDER_TRACE") != nullptr;
    for (int i = 0; i < static_cast<int> (duration * 60.0f); ++i) {
      glider.update (glider_test_step);
      if (trace && i % 30 == 0)
        std::printf ("%5.1f  h %6.1f  v %5.1f  climb %5.2f  bank %5.2f\n",
                     i / 60.0f,
                     float (glider.position ()[1]),
                     glider.airspeed ().numerical_value_in (u::m / u::s),
                     glider.vertical_speed ().numerical_value_in (u::m / u::s),
                     radians_value (glider.bank ()));
    }
  }

  float glider_test_airspeed (const mov::Glider& glider) {
    return glider.airspeed ().numerical_value_in (u::m / u::s);
  }

  float glider_test_climb (const mov::Glider& glider) {
    return glider.vertical_speed ().numerical_value_in (u::m / u::s);
  }
}

MOPPE_TEST (a_glider_left_alone_settles_into_its_trimmed_glide) {
  const map::SurfaceGeometry ground = glider_test_ground ();
  mov::Glider glider (ground);
  glider.launch (position (Vec3 (100, 600, 100)),
                 velocity (Vec3 (0, 0, 16)),
                 Vec3 (0, 0, 1));
  glider_test_fly (glider, 30.0f);
  MOPPE_CHECK (!glider.landed ());
  MOPPE_CHECK_NEAR (glider_test_airspeed (glider), 16.0f, 1.0f);
  // Sixteen to one at best, a little less at trim.
  MOPPE_CHECK (glider_test_climb (glider) < -0.8f);
  MOPPE_CHECK (glider_test_climb (glider) > -1.3f);
  // And it stays there rather than porpoising.
  const float settled = glider_test_climb (glider);
  glider_test_fly (glider, 3.0f);
  MOPPE_CHECK_NEAR (glider_test_climb (glider), settled, 0.1f);
}

MOPPE_TEST (a_glider_carrying_the_bike_flies_the_same_slope_faster) {
  const map::SurfaceGeometry ground = glider_test_ground ();
  mov::Glider glider (ground);
  glider.launch (position (Vec3 (100, 600, 100)),
                 velocity (Vec3 (0, 0, 25)),
                 Vec3 (0, 0, 1),
                 true);
  glider_test_fly (glider, 30.0f);
  MOPPE_CHECK_NEAR (
    glider_test_airspeed (glider), 16.0f * std::sqrt (2.5f), 2.0f);
  const float ratio =
    glider_test_airspeed (glider) / -glider_test_climb (glider);
  MOPPE_CHECK (ratio > 8.0f);
  MOPPE_CHECK (ratio < 16.0f);
}

MOPPE_TEST (a_dive_buys_speed_that_a_pull_up_turns_back_into_height) {
  const map::SurfaceGeometry ground = glider_test_ground ();
  mov::Glider glider (ground);
  glider.launch (position (Vec3 (100, 600, 100)),
                 velocity (Vec3 (0, 0, 16)),
                 Vec3 (0, 0, 1));
  glider.set_speed_control (1.0f);
  glider_test_fly (glider, 8.0f);
  MOPPE_CHECK (glider_test_airspeed (glider) > 21.0f);
  const float low = glider.position ()[1];
  glider.set_speed_control (-1.0f);
  float top = low;
  for (int i = 0; i < 240; ++i) {
    glider.update (glider_test_step);
    top = std::max<float> (top, glider.position ()[1]);
  }
  MOPPE_CHECK (top > low + 4.0f);
  // Nothing comes free: the zoom gives back less than the speed was worth.
  MOPPE_CHECK (top < low + 25.0f);
}

MOPPE_TEST (a_banked_glider_turns_at_the_rate_its_tilted_lift_gives) {
  const map::SurfaceGeometry ground = glider_test_ground ();
  mov::Glider glider (ground);
  glider.launch (position (Vec3 (100, 600, 100)),
                 velocity (Vec3 (0, 0, 16)),
                 Vec3 (0, 0, 1));
  glider.set_turn (0.5f);
  glider_test_fly (glider, 6.0f);
  const Vec3 before = glider.heading ();
  glider_test_fly (glider, 1.0f);
  const Vec3 after = glider.heading ();
  const float turned =
    std::acos (std::clamp<float> (dot (before, after), -1.0f, 1.0f));
  const float expected = 9.82f * std::tan (radians_value (glider.bank ())) /
                         glider_test_airspeed (glider);
  MOPPE_CHECK_NEAR (turned, expected, 0.25f * expected);
  // A positive turn swings a wing flying along +z toward +x, the way the
  // bike steers.
  MOPPE_CHECK (cross (before, after)[1] > 0.0f);
}

MOPPE_TEST (a_flare_holds_the_wing_off_while_its_speed_lasts_and_then_stalls) {
  const map::SurfaceGeometry ground = glider_test_ground ();
  mov::Glider glider (ground);
  glider.launch (position (Vec3 (100, 600, 100)),
                 velocity (Vec3 (0, 0, 16)),
                 Vec3 (0, 0, 1));
  glider_test_fly (glider, 10.0f);
  const float gliding = glider_test_climb (glider);
  const float speed = glider_test_airspeed (glider);
  glider.set_flare (true);
  glider_test_fly (glider, 0.8f);
  MOPPE_CHECK (glider_test_climb (glider) > gliding + 0.3f);
  glider_test_fly (glider, 6.0f);
  MOPPE_CHECK (glider_test_airspeed (glider) < speed - 3.0f);
  MOPPE_CHECK (glider_test_climb (glider) < gliding - 0.5f);
}

MOPPE_TEST (a_glider_touches_down_still_moving) {
  const map::SurfaceGeometry ground = glider_test_ground ();
  mov::Glider glider (ground);
  glider.launch (
    position (Vec3 (100, 30, 100)), velocity (Vec3 (0, 0, 16)), Vec3 (0, 0, 1));
  glider_test_fly (glider, 60.0f);
  MOPPE_CHECK (glider.landed ());
  MOPPE_CHECK_NEAR (
    float (glider.position ()[1]), 10.0f + mov::Glider::pilot_clearance, 1e-3f);
  const Vec3 v = glider.velocity ();
  MOPPE_CHECK (std::hypot (v[0], v[2]) > 10.0f);
}

namespace {
  struct GlideOutcome {
    game::Mode mode;
    float seconds;
    float speed;
    float upright;
    float health_lost;
    float sink;
  };

  // Throws the bike off into the air at speed, opens the wing, and flies
  // it down onto level ground, flaring over the last metres if asked to.
  GlideOutcome glider_test_land_bike (mov::BikePhysics physics, bool flare) {
    const map::SurfaceGeometry surface = glider_test_ground ();
    game::WorldParams world;
    world.map_size = spatial_extent_in_metres (Vec3 (512, 20, 512));
    world.resolution = static_cast<int> (surface.domain ().width ());
    world.water_level = 0 * u::m;
    game::GameSession session (world, surface, physics);

    mov::Vehicle::State flight = session.bike ().state ();
    flight.position = position (Vec3 (100, 50, 100));
    flight.velocity = velocity (Vec3 (0, 0, 24));
    flight.heading = Vec3 (0, 0, 1);
    flight.thrust_orientation = flight.heading;
    flight.render_heading = flight.heading;
    flight.airborne_time = seconds (1.0f);
    flight.fall_top = 50 * u::m;
    session.bike ().restore (flight);

    const bool trace = std::getenv ("MOPPE_GLIDER_TRACE") != nullptr;
    const float health = session.logic ().m_health;
    game::InputFrame input;
    input.deploy_glider = true;
    GlideOutcome outcome {};
    for (int i = 0; i < 60 * 120; ++i) {
      const float agl = session.bike ().position ()[1] - 10.0f;
      if (flare && agl < 3.0f)
        input.boost = 1.0f * one;
      const float sink = -session.bike ().velocity ()[1];
      game::advance_game_session (
        world, surface, session, input, glider_test_step);
      input.deploy_glider = false;
      if (i == 0)
        MOPPE_CHECK (session.logic ().m_mode == game::M_GLIDER);
      if (trace && i % 30 == 0)
        std::printf ("%5.1f  agl %6.2f  v %5.1f  climb %5.2f  up %4.2f\n",
                     i / 60.0f,
                     agl,
                     length (session.bike ().velocity ()),
                     float (session.bike ().velocity ()[1]),
                     float (session.bike ().render_normal ()[1]));
      if (session.logic ().m_mode != game::M_GLIDER) {
        outcome.seconds = i / 60.0f;
        outcome.sink = sink;
        const Vec3 v = session.bike ().velocity ();
        outcome.speed = std::hypot (v[0], v[2]);
        break;
      }
    }
    // Let the landing play out on the wheels.
    input.boost = 0.0f * one;
    for (int i = 0; i < 30; ++i)
      game::advance_game_session (
        world, surface, session, input, glider_test_step);
    outcome.mode = session.logic ().m_mode;
    outcome.upright = session.bike ().render_normal ()[1];
    outcome.health_lost = health - session.logic ().m_health;
    const Vec3 v = session.bike ().velocity ();
    if (trace)
      std::printf ("landed after %.1f s at %.1f m/s sinking %.2f; "
                   "%.1f m/s half a second on, up %.2f, hurt %.1f\n",
                   outcome.seconds,
                   outcome.speed,
                   outcome.sink,
                   std::hypot (v[0], v[2]),
                   outcome.upright,
                   outcome.health_lost);
    outcome.speed = std::min<float> (outcome.speed, std::hypot (v[0], v[2]));
    return outcome;
  }
}

MOPPE_TEST (a_bike_under_the_wing_lands_on_its_wheels_and_rides_on) {
  for (const bool flare : { false, true }) {
    const GlideOutcome landing =
      glider_test_land_bike (mov::BikePhysics::rigid, flare);
    MOPPE_CHECK (landing.mode == game::M_BIKE);
    // Forty metres up at better than eight to one is a long way.
    MOPPE_CHECK (landing.seconds > 10.0f);
    // The glide's speed is still the bike's half a second after the wheels
    // touch, less what a flare chose to spend.
    MOPPE_CHECK (landing.speed > (flare ? 9.0f : 18.0f));
    MOPPE_CHECK (landing.upright > 0.9f);
    MOPPE_CHECK (landing.health_lost == 0.0f);
  }
}

MOPPE_TEST (a_flare_softens_the_touchdown) {
  const GlideOutcome plain =
    glider_test_land_bike (mov::BikePhysics::rigid, false);
  const GlideOutcome flared =
    glider_test_land_bike (mov::BikePhysics::rigid, true);
  MOPPE_CHECK (flared.sink < plain.sink - 0.5f);
  MOPPE_CHECK (flared.speed < plain.speed - 2.0f);
}

MOPPE_TEST (the_classic_bike_is_set_down_moving_too) {
  const GlideOutcome landing =
    glider_test_land_bike (mov::BikePhysics::classic, false);
  MOPPE_CHECK (landing.mode == game::M_BIKE);
  MOPPE_CHECK (landing.speed > 8.0f);
}

MOPPE_TEST (a_pilot_alone_lands_running) {
  const map::SurfaceGeometry surface = glider_test_ground ();
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (Vec3 (512, 20, 512));
  world.resolution = static_cast<int> (surface.domain ().width ());
  world.water_level = 0 * u::m;
  game::GameSession session (world, surface);
  session.set_mode (game::M_GLIDER);
  session.glider ().launch (
    position (Vec3 (100, 20, 100)), velocity (Vec3 (0, 0, 16)), Vec3 (0, 0, 1));
  int steps = 0;
  while (session.logic ().m_mode == game::M_GLIDER && steps++ < 3600)
    game::advance_game_session (
      world, surface, session, game::InputFrame {}, glider_test_step);
  MOPPE_CHECK (session.logic ().m_mode == game::M_FOOT);
  const Vec3 v = session.walker ().velocity ();
  MOPPE_CHECK (std::hypot (v[0], v[2]) > 6.0f);
  // And the feet bring it to a stand.
  for (int i = 0; i < 120; ++i)
    game::advance_game_session (
      world, surface, session, game::InputFrame {}, glider_test_step);
  MOPPE_CHECK (session.walker ().grounded ());
  MOPPE_CHECK (length (session.walker ().velocity ()) < 0.1f);
  MOPPE_CHECK_NEAR (session.walker ().position ()[1], 10.0f, 0.01f);
}
