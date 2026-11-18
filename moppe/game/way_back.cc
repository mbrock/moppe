#include <moppe/game/way_back.hh>

#include <moppe/game/bus.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/world_ground.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <concepts>
#include <limits>
#include <type_traits>

namespace moppe::game {
  namespace {
    // Named apart: the unity build joins this file's helpers with
    // parking's.
    namespace way_back_parts {
      constexpr proportion_t whole = 1.0f * proportion[one];

      position_t moved (const position_t& at, const displacement_t& by) {
        return at + quantity_cast<isq::position_vector> (by);
      }

      meters_t height_of (const position_t& at) {
        return position_value (at)[1] * u::m;
      }

      direction_t upward () {
        return unit_direction (Vec3 (0, 1, 0));
      }

      // The level way a quarter turn to the left of `way`.
      direction_t left_of (const direction_t& way) {
        return unit_direction (
          Vec3 (direction_value (way)[2], 0.0f, -direction_value (way)[0]));
      }

      // `d` on the level, as a way, or `otherwise` where it stands near
      // upright.
      direction_t level_way (const displacement_t& d,
                             const direction_t& otherwise) {
        const displacement_t flat =
          displacement (lift (plan (displacement_value (d))));
        return length (displacement_value (flat)) > 0.01f ? direction_of (flat)
                                                          : otherwise;
      }

      // The cubic Bézier from `a` to `d`, drawn toward `b` and then `c`, at
      // `s` of the way.
      position_t bezier (const position_t& a,
                         const position_t& b,
                         const position_t& c,
                         const position_t& d,
                         proportion_t s) {
        const proportion_t r = whole - s;
        return a + (b - a) * (3.0f * (r * r * s).numerical_value_in (one)) +
               (c - a) * (3.0f * (r * s * s).numerical_value_in (one)) +
               (d - a) * (s * s * s).numerical_value_in (one);
      }
    }
  }
  namespace wb = way_back_parts;

  std::optional<RoadsidePlace> roadside_near (const BusLine& line,
                                              const WorldGround& ground,
                                              const position_t& from) {
    const plan_extent_t period = ground.period ();
    const std::optional<std::size_t> stop = bus_stop_near (
      line, from, std::numeric_limits<float>::infinity () * u::m, period);
    if (!stop)
      return std::nullopt;
    const BusStop& at = line.stops ()[*stop];
    // A bus's length and more short of the stop, and over the line onto
    // the half no bus drives on.
    const meters_t short_of = 14.0f * u::m;
    const meters_t aside = 1.7f * u::m;
    const meters_t where = at.along - short_of;
    const direction_t heading = line.direction (where);
    const position_t road =
      wb::moved (line.point (where), along (wb::left_of (heading), aside));
    RoadsidePlace place;
    place.stop = *stop;
    place.heading = heading;
    place.machine =
      at_height (terrain::nearest_image (plan (road), plan (from), period),
                 wb::height_of (road));
    const plan_position_t feet = terrain::nearest_image (
      plan (at.sign) + plan (along (heading, -6.0f * u::m)),
      plan (from),
      period);
    place.feet =
      terrain::at_elevation (feet, ground.elevation (feet) + 0.1f * u::m);
    return place;
  }

  bool can_take_way_back (const GameSession& session) {
    const GameLogicState& logic = session.logic ();
    return logic.m_doing.way != Doing::Way::bus &&
           logic.m_doing.way != Doing::Way::passenger && !logic.m_camp.resting;
  }

  bool arrives_on_foot (const GameSession& session) {
    const Doing doing = session.logic ().m_doing;
    if (doing == Doing::gliding ())
      return !session.glider ().bike_attached ();
    if (const std::optional<Machine> machine = driven_machine (doing))
      return machine_kind (*machine).medium == Medium::water;
    return true;
  }

  void set_down (GameSession& session, const RoadsidePlace& place) {
    GameLogicState& logic = session.logic ();
    if (logic.m_haul.active)
      let_go_rope (session);
    if (logic.m_sling.hooked)
      let_load_go (session);
    logic.m_fishing.line.phase = Line::Phase::idle;
    // Gliding with the bike under the wing, the bike comes along.
    if (logic.m_doing == Doing::gliding () &&
        session.glider ().bike_attached ()) {
      session.glider ().drop_bike ();
      session.set_mode (Doing::driving (Machine::bike));
    }
    if (arrives_on_foot (session)) {
      if (const std::optional<Machine> afloat = driven_machine (logic.m_doing))
        step_out (session, *afloat);
      session.set_mode (Doing::on_foot ());
      session.walker ().spawn (place.feet, place.heading);
      return;
    }
    const Machine machine = logic.m_doing.machine;
    logic.m_trouble[static_cast<std::size_t> (machine)] =
      Trouble { .anchor = place.machine };
    visit (session.fleet (), machine, [&] (Machine m, auto& body) {
      using Body = std::remove_cvref_t<decltype (body)>;
      if constexpr (std::same_as<Body, mov::Vehicle>) {
        const mov::BikeRig& rig = m == Machine::mountain_bike
                                    ? mov::mountain_bike_rig
                                  : m == Machine::long_john ? mov::long_john_rig
                                                            : mov::trail90_rig;
        body.set_heading (direction_value (place.heading));
        body.reset (position_value (wb::moved (
          place.machine,
          along (wb::upward (), rig.ride_height () + 0.02f * u::m))));
      } else if constexpr (requires {
                             body.place (place.machine, place.heading);
                           }) {
        body.place (place.machine, place.heading);
      }
    });
  }

  void WayBack::start (const FrameCameraReading& from,
                       const position_t& subject,
                       const position_t& to,
                       const RoadsidePlace& place,
                       const WorldGround& ground) {
    m_from = from;
    m_subject = subject;
    m_to = to;
    m_place = place;
    const plan_displacement_t way = plan (to) - plan (subject);
    m_across = displacement (lift (plan_value (way)));
    // Clear of the highest ground on the way, and over both ends' eyes,
    // by more the further it goes.
    meters_t highest =
      std::max (wb::height_of (from.position), wb::height_of (to));
    constexpr int samples = 48;
    for (int i = 0; i <= samples; ++i) {
      const plan_position_t p =
        plan (subject) + way * (static_cast<float> (i) / samples);
      highest = std::max (
        highest,
        terrain::surface_elevation_value (ground.elevation (p)) * u::m);
    }
    const meters_t distance = length (way);
    const meters_t clearance =
      std::clamp (0.3f * distance, 70.0f * u::m, 380.0f * u::m);
    m_top = at_height (plan (subject) + way * 0.5f, highest + clearance);
    m_length =
      std::clamp (seconds_t (5.5f * u::s + distance / (350.0f * u::m / u::s)),
                  5.5f * u::s,
                  11.0f * u::s);
    m_elapsed = 0.0f * u::s;
    m_arrived = false;
    m_active = true;
  }

  bool WayBack::tick (seconds_t dt) {
    if (!m_active)
      return false;
    m_elapsed += dt;
    if (m_elapsed >= m_length)
      m_active = false;
    if (m_arrived || m_elapsed < 0.5f * m_length)
      return false;
    m_arrived = true;
    return true;
  }

  proportion_t WayBack::progress () const {
    return std::clamp ((m_elapsed / m_length).numerical_value_in (one) *
                         proportion[one],
                       0.0f * proportion[one],
                       wb::whole);
  }

  proportion_t WayBack::motion_blur () const {
    const proportion_t t = progress ();
    return quantity_cast<proportion> (t * (wb::whole - t));
  }

  FrameCameraReading WayBack::camera (const FrameCameraReading& play,
                                      const WorldGround& ground) const {
    const proportion_t t = progress ();
    const proportion_t eased = band (0.0f * u::s, m_length, m_elapsed);
    // Most of the way up to the top, and back from what it looked at a
    // little: the zoom out of the view, or the way down into it.
    const auto lifted = [&] (const position_t& eye,
                             const position_t& looked_at) {
      const meters_t climb = wb::height_of (m_top) - wb::height_of (eye);
      const direction_t back = wb::level_way (
        displacement (position_value (eye) - position_value (looked_at)),
        wb::upward ());
      return wb::moved (wb::moved (eye, along (back, 0.25f * climb)),
                        along (wb::upward (), 0.9f * climb));
    };
    // Over the top it goes level, the way across.
    const displacement_t pass = m_across * 0.22f;
    position_t eye =
      !m_arrived
        ? wb::bezier (m_from.position,
                      lifted (m_from.position, m_subject),
                      wb::moved (m_top, pass * -1.0f),
                      m_top,
                      std::min (2.0f * eased, wb::whole))
        : wb::bezier (m_top,
                      wb::moved (m_top, pass),
                      lifted (play.position, m_to),
                      play.position,
                      std::max (2.0f * eased - wb::whole, 0.0f * wb::whole));
    // Well clear of the ground it flies over, and as near it as play's
    // view at the ends.
    const proportion_t mid =
      quantity_cast<proportion> (4.0f * t * (wb::whole - t));
    const meters_t floor =
      terrain::surface_elevation_value (ground.elevation (plan (eye))) * u::m +
      1.2f * u::m * mid.numerical_value_in (one) +
      30.0f * u::m * (mid * mid).numerical_value_in (one);
    if (wb::height_of (eye) < floor)
      eye = at_height (plan (eye), floor);

    // It looks at where the player was, then ahead on the way to where
    // they come down, leaning the way it goes; from play's view at first,
    // and into it at the last.
    const position_t aim =
      m_subject +
      (m_to - m_subject) * band (0.1f * m_length, 0.65f * m_length, m_elapsed)
                             .numerical_value_in (one);
    const direction_t travel = wb::level_way (
      m_across,
      wb::level_way (displacement (direction_value (m_from.forward)),
                     unit_direction (Vec3 (0, 0, 1))));
    const direction_t look = unit_direction (
      normalized (normalized (position_value (aim) - position_value (eye)) +
                  direction_value (travel) * 0.15f));
    const proportion_t leaving =
      wb::whole - band (0.0f * u::s, 0.2f * m_length, m_elapsed);
    const proportion_t landing = band (0.8f * m_length, m_length, m_elapsed);
    const direction_t forward = unit_direction (normalized (
      direction_value (m_from.forward) * leaving.numerical_value_in (one) +
      direction_value (play.forward) * landing.numerical_value_in (one) +
      direction_value (look) *
        (wb::whole - leaving - landing).numerical_value_in (one)));

    FrameCameraReading shot;
    shot.position = eye;
    shot.forward = forward;
    // A little wider over the top, as it zooms out.
    shot.field_of_view =
      lerp (m_from.field_of_view, play.field_of_view, eased) +
      12.0f * u::deg * mid.numerical_value_in (one);
    shot.view =
      Mat4::look_at (position_value (eye),
                     position_value (eye) + direction_value (forward) * 100.0f,
                     Vec3 (0, 1, 0));
    return shot;
  }
}
