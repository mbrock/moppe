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
                               float total_time,
                               float dt) {
    static const bool demo_glides = [] {
      const char* name = moppe::environment ("MOPPE_DEMO");
      return name && std::string_view (name) == "glide";
    }();
    static const bool demo_wheelies = [] {
      const char* name = moppe::environment ("MOPPE_DEMO");
      return name && std::string_view (name) == "wheelie";
    }();
    // The autopilot rides; the player may still look around.
    const float look_yaw = input.look_yaw;
    const float look_pitch = input.look_pitch;
    if (session.logic ().m_doing == Doing::driving (Machine::helicopter)) {
      // Spool up, climb out, hook what is under it, and cruise in
      // long easy curves.
      const float t = total_time;
      input = {
        .turn = t > 9.0f ? 0.45f * std::sin ((t - 9.0f) * 0.22f) : 0.0f,
        .drive = t > 8.0f ? 1.0f : 0.0f,
        .boost = t > 4.0f && t < 9.0f ? 1.0f : 0.0f,
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
      const float pitch = radians_value (bike.pitch ());
      const float rate = dt > 0.0f ? (pitch - m_pitch) / dt : 0.0f;
      m_pitch = pitch;
      const float ahead = pitch + 0.4f * rate;
      const bool up = std::fmod (total_time, 12.0f) > 3.0f;
      input = {
        .turn = 0.2f * std::sin (total_time * 0.25f),
        .drive = !up            ? 0.6f
                 : ahead < 0.7f ? 1.0f
                                : 0.0f,
        .lean_back = up,
        .look_yaw = look_yaw,
        .look_pitch = look_pitch,
        .run = up && ahead > 1.1f,
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
        .turn = 0.35f * std::sin (total_time * 0.25f),
        .drive = 1.0f,
        .boost =
          session.logic ().m_doing != Doing::driving (Machine::tractor) &&
              session.logic ().m_doing != Doing::driving (Machine::caddy) &&
              session.logic ().m_doing != Doing::driving (Machine::boat) &&
              std::fmod (total_time, 11.0f) < (demo_glides ? 3.5f : 1.35f)
            ? 1.0f
            : 0.0f,
        // MOPPE_DEMO=glide opens the wing on the first leap high
        // enough to allow it.
        .use_held = demo_glides,
        .look_yaw = look_yaw,
        .look_pitch = look_pitch,
      };
      // Look a few metres ahead; where a trunk stands in the way, steer
      // hard toward the side its contact pushes, so a ride through the
      // woods weaves between trees instead of stopping at the first.
      const Vec3 at = session.subject_position ();
      const Vec3 heading = session.subject_heading ();
      const Vec3 right = normalized (cross (heading, Vec3 (0, 1, 0)));
      for (const float ahead : { 4.0f, 9.0f }) {
        const Vec3 probe = at + heading * ahead;
        const mov::TrunkContact contact =
          ground.trunks.collide (probe, probe + Vec3 (0, 1.5f, 0), 1.8f);
        if (contact.hit) {
          input.turn = dot (contact.normal, right) >= 0.0f ? 1.0f : -1.0f;
          break;
        }
      }
    }

    return input;
  }

  InputFrame DemoPilot::water (GameSession& session,
                               Parking& parking,
                               const WorldGround& ground,
                               float t,
                               bool boat) {
    if (boat) {
      const mov::Boat& hull = session.boat ();
      return parking.water_autopilot (ground,
                                      t * u::s,
                                      hull.position (),
                                      hull.forward (),
                                      { .follow_route = true });
    }
    const mov::JetSki& ski = session.jet_ski ();
    return parking.water_autopilot (
      ground,
      t * u::s,
      ski.position (),
      ski.forward (),
      { .speed = length (velocity_value (ski.velocity ())) * u::m / u::s,
        .sight = 240.0f * u::m,
        .room = 200.0f * u::m });
  }
}
