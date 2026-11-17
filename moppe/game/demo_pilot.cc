// The demo autopilot (demo_pilot.hh).

#include <moppe/environment.hh>
#include <moppe/game/demo_pilot.hh>

#include <cmath>
#include <string_view>

namespace moppe::game {
  bool DemoPilot::active () {
    static const bool demo = moppe::environment ("MOPPE_DEMO") != 0;
    return demo;
  }

  InputFrame DemoPilot::steer (InputFrame input,
                               GameSession& session,
                               Parking& parking,
                               const WorldGround& ground,
                               seconds_t total_time,
                               seconds_t dt) {
    static const bool demo_glides = [] {
      const char* name = moppe::environment ("MOPPE_DEMO");
      return name && std::string_view (name) == "glide";
    }();
    static const bool demo_wheelies = [] {
      const char* name = moppe::environment ("MOPPE_DEMO");
      return name && std::string_view (name) == "wheelie";
    }();
    // How fast the bike's and the cruising machines' weave swings.
    constexpr radians_per_second_t weave = 0.25f * u::rad / u::s;
    // The autopilot rides; the player may still look around.
    const radians_t look_yaw = input.look_yaw;
    const radians_t look_pitch = input.look_pitch;
    if (session.logic ().m_doing == Doing::driving (Machine::helicopter)) {
      // Spool up, climb out, hook what is under it, and cruise in
      // long easy curves.
      const seconds_t t = total_time;
      constexpr radians_per_second_t swing = 0.22f * u::rad / u::s;
      input = {
        .turn = (t > 9.0f * u::s ? 0.45f * libm::sin ((t - 9.0f * u::s) * swing)
                                 : 0.0f) *
                control_signal[one],
        .drive = (t > 8.0f * u::s ? 1.0f : 0.0f) * control_signal[one],
        .boost =
          (t > 4.0f * u::s && t < 9.0f * u::s ? 1.0f : 0.0f) * proportion[one],
        .toggle_mount = !session.logic ().m_sling.hooked &&
                        session.hook_within_reach ().has_value (),
        .look_yaw = look_yaw,
        .look_pitch = look_pitch,
      };
    } else if (demo_wheelies &&
               session.logic ().m_doing == Doing::driving (Machine::bike)) {
      // MOPPE_DEMO=wheelie: rolling for a few seconds of every
      // twelve, then up on the back wheel and held there as a rider
      // at the keyboard holds it, the throttle all or nothing --
      // open while the nose is low or falling, shut as it rises, and
      // a dab of the rear brake if it gets too high -- judged by
      // where the pitch is heading a moment on.
      const mov::Vehicle& bike = session.bike ();
      const radians_t pitch = bike.pitch ();
      const radians_per_second_t rate =
        dt > 0.0f * u::s ? (pitch - m_pitch) / dt : 0.0f * u::rad / u::s;
      m_pitch = pitch;
      const radians_t ahead = pitch + (0.4f * u::s) * rate;
      const bool up = fmod (total_time, 12.0f * u::s) > 3.0f * u::s;
      input = {
        .turn = 0.2f * libm::sin (total_time * weave) * control_signal[one],
        .drive = (!up                     ? 0.6f
                  : ahead < 0.7f * u::rad ? 1.0f
                                          : 0.0f) *
                 control_signal[one],
        .lean_back = up,
        .look_yaw = look_yaw,
        .look_pitch = look_pitch,
        .run = up && ahead > 1.1f * u::rad,
      };
    } else if (session.logic ().m_doing == Doing::driving (Machine::boat) ||
               session.logic ().m_doing == Doing::driving (Machine::jet_ski)) {
      input = session.logic ().m_doing == Doing::driving (Machine::boat)
                ? water (session, parking, ground, total_time, true)
                : water (session, parking, ground, total_time, false);
      input.look_yaw = look_yaw;
      input.look_pitch = look_pitch;
    } else {
      input = {
        .turn = 0.35f * libm::sin (total_time * weave) * control_signal[one],
        .drive = 1.0f * control_signal[one],
        .boost =
          session.logic ().m_doing != Doing::driving (Machine::tractor) &&
              session.logic ().m_doing != Doing::driving (Machine::caddy) &&
              session.logic ().m_doing != Doing::driving (Machine::boat) &&
              fmod (total_time, 11.0f * u::s) <
                (demo_glides ? 3.5f : 1.35f) * u::s
            ? 1.0f * proportion[one]
            : 0.0f * proportion[one],
        // MOPPE_DEMO=glide opens the wing on the first leap high
        // enough to allow it.
        .use_held = demo_glides,
        .look_yaw = look_yaw,
        .look_pitch = look_pitch,
      };
      // Look a few metres ahead; where a trunk stands in the way, steer
      // hard toward the side its contact pushes, so a ride through the
      // woods weaves between trees instead of stopping at the first.
      const Vec3 at = position_value (session.subject_position ());
      const Vec3 heading = direction_value (session.subject_heading ());
      const Vec3 right = normalized (cross (heading, Vec3 (0, 1, 0)));
      for (const meters_t ahead : { 4.0f * u::m, 9.0f * u::m }) {
        const Vec3 probe = at + heading * meters_value (ahead);
        const mov::TrunkContact contact = ground.trunks.collide (
          position (probe), position (probe + Vec3 (0, 1.5f, 0)), 1.8f * u::m);
        if (contact.hit) {
          input.turn = dot (direction_value (contact.normal), right) >= 0.0f
                         ? 1.0f * control_signal[one]
                         : -1.0f * control_signal[one];
          break;
        }
      }
    }

    return input;
  }

  InputFrame DemoPilot::water (GameSession& session,
                               Parking& parking,
                               const WorldGround& ground,
                               seconds_t t,
                               bool boat) {
    if (boat) {
      const mov::Boat& hull = session.boat ();
      return parking.water_autopilot (
        ground, t, hull.position (), hull.forward (), { .follow_route = true });
    }
    const mov::JetSki& ski = session.jet_ski ();
    return parking.water_autopilot (
      ground,
      t,
      ski.position (),
      ski.forward (),
      { .speed = length (velocity_value (ski.velocity ())) * u::m / u::s,
        .sight = 240.0f * u::m,
        .room = 200.0f * u::m });
  }
}
