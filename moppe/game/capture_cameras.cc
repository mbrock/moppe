// The capture cameras and the rest camera (capture_scripts.hh).

#include <moppe/environment.hh>
#include <moppe/game/camp.hh>
#include <moppe/game/capture_scripts.hh>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace moppe::game {
  void CaptureCameras::override_camera (GameSession& session,
                                        const WorldGround& ground,
                                        seconds_t interval) {
    // Each places play's camera when it applies, in this order, so that
    // a later one wins where two do: lying in the hammock gives way to
    // the walk's side view, that to the ride camera, and that to the
    // animal camera.
    rest (session, ground);
    walk_side (session);
    ride (session, ground, interval);
    follow_animal (session, ground, interval);
  }

  void CaptureCameras::rest (GameSession& session, const WorldGround& ground) {
    const Camp& camp = session.logic ().m_camp;
    const float load = hammock_load (camp, session.logic ().m_total_time);
    if (!camp.hung || load <= 0.001f ||
        session.logic ().m_doing != Doing::on_foot ())
      return;
    ChaseCamera& camera = session.camera ();
    const Vec3 from = camera.position ();
    const Vec3 looking = camera.forward ();
    Vec3 eye, toward;
    if (session.logic ().m_cam_mode == CAM_HELMET) {
      eye = resting_eye (camp.hammock, camp.swing);
      toward = resting_gaze (camp.hammock, camp.gaze_yaw, camp.gaze_pitch);
    } else {
      const Vec3 subject = hammock_middle (camp.hammock) - Vec3 (0, 0.45f, 0);
      const float around = camp.gaze_yaw + 0.45f;
      eye = subject +
            (hammock_across (camp.hammock) * std::cos (around) +
             hammock_along (camp.hammock) * std::sin (around)) *
              3.7f +
            Vec3 (0, 0.75f + 1.2f * (1.1f - camp.gaze_pitch), 0);
      eye[1] =
        std::max (static_cast<float> (eye[1]), ground.height (eye) + 0.4f);
      toward = normalized (subject - eye);
    }
    const Vec3 at = from + (eye - from) * load;
    const Vec3 gaze = normalized (looking + (toward - looking) * load);
    camera.place (at, at + gaze * 10.0f);
  }

  void CaptureCameras::walk_side (GameSession& session) {
    static const bool side = [] {
      const char* view = moppe::environment ("MOPPE_WALK_CAMERA");
      return view && std::string_view (view) == "side";
    }();
    if (!side || session.logic ().m_doing != Doing::on_foot ())
      return;
    const Walker& walker = session.walker ();
    const Vec3 heading = walker.heading ();
    const Vec3 right (heading[2], 0.0f, -heading[0]);
    const Vec3 at = walker.position () + Vec3 (0, 0.9f, 0);
    session.camera ().place (at + right * 4.0f + Vec3 (0, 0.25f, 0), at);
  }

  void CaptureCameras::follow_animal (GameSession& session,
                                      const WorldGround& ground,
                                      seconds_t interval) {
    const float dt = seconds_value (interval);
    static const float distance = [] {
      const char* value = moppe::environment ("MOPPE_ANIMAL_CAMERA");
      return value ? static_cast<float> (::atof (value)) : 0.0f;
    }();
    if (distance <= 0.0f || !watched ||
        *watched >= session.animals ().animals ().size ())
      return;
    const Animal& a = session.animals ().animals ()[*watched];
    const float height = 0.7f * meters_value (traits (a.species).length);
    const Vec3 target = a.position + Vec3 (0.0f, height, 0.0f);
    // Eased, so that the view glides after it.
    const float ease = 1.0f - std::exp (-1.5f * dt);
    watch_target =
      watch_target ? *watch_target + (target - *watch_target) * ease : target;
    Vec3 eye = *watch_target + watch_side * distance +
               Vec3 (0.0f, 0.25f * distance, 0.0f);
    eye[1] = std::max<float> (eye[1], ground.height (eye) + 1.2f);
    session.camera ().place (eye, *watch_target);
  }

  void CaptureCameras::ride (GameSession& session,
                             const WorldGround& ground,
                             seconds_t interval) {
    static const std::string_view view = [] {
      const char* name = moppe::environment ("MOPPE_RIDE_CAMERA");
      return std::string_view (name ? name : "");
    }();
    film_fov.reset ();
    if (session.logic ().m_doing == Doing::driving (Machine::boat) &&
        film_boat (session, ground, view, interval))
      return;
    // Flying, it is the machine on the line it stands beside.
    const bool slung =
      session.logic ().m_doing == Doing::driving (Machine::helicopter) &&
      session.logic ().m_sling.hooked;
    if (view.empty () || session.logic ().m_doing == Doing::on_foot () ||
        (session.logic ().m_doing == Doing::driving (Machine::helicopter) &&
         !slung))
      return;
    if (slung) {
      const MachineBody load =
        session.machine_body (session.logic ().m_sling.load);
      Vec3 heading = direction_value (load.forward);
      heading[1] = 0.0f;
      heading =
        length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
      const Vec3 right (heading[2], 0.0f, -heading[0]);
      const Vec3 at = position_value (load.position) + Vec3 (0, 0.5f, 0);
      const Vec3 from = view == "front" ? heading * 7.5f + right * 2.0f
                                        : right * 7.5f + heading * 0.6f;
      session.camera ().place (at + from + Vec3 (0, 0.5f, 0), at);
      return;
    }
    const bool gliding = session.logic ().m_doing == Doing::gliding ();
    const bool tractor =
      session.logic ().m_doing == Doing::driving (Machine::tractor);
    const bool caddy =
      session.logic ().m_doing == Doing::driving (Machine::caddy);
    const bool boat =
      session.logic ().m_doing == Doing::driving (Machine::boat);
    const bool ski =
      session.logic ().m_doing == Doing::driving (Machine::jet_ski);
    const bool driving =
      session.logic ().m_doing == Doing::driving (Machine::jeep) || tractor ||
      caddy || boat || ski;
    const bool pedalled = game::pedalled (session.logic ().m_doing);
    const auto& bike = session.ridden_bike ();
    // The tractor with its trailer, from their middle.
    const Vec3 car = tractor
                       ? (position_value (session.tractor ().position ()) +
                          session.tractor ().trailer_position ()) *
                           0.5f
                     : caddy ? position_value (session.caddy ().position ())
                     : boat  ? position_value (session.boat ().position ())
                     : ski   ? position_value (session.jet_ski ().position ())
                             : position_value (session.jeep ().position ());
    Vec3 heading = gliding   ? session.glider ().heading ()
                   : tractor ? direction_value (session.tractor ().forward ())
                   : caddy   ? direction_value (session.caddy ().forward ())
                   : boat    ? direction_value (session.boat ().forward ())
                   : ski     ? direction_value (session.jet_ski ().forward ())
                   : driving ? direction_value (session.jeep ().forward ())
                             : direction_value (bike.render_orientation ());
    heading[1] = 0.0f;
    heading = normalized (heading);
    const Vec3 right (heading[2], 0.0f, -heading[0]);
    const Vec3 at =
      gliding
        ? position_value (session.glider ().position ()) - Vec3 (0, 0.9f, 0)
      : driving  ? car + Vec3 (0, tractor ? 0.9f : 0.5f, 0)
      : pedalled ? position_value (bike.render_position ()) + Vec3 (0, 0.55f, 0)
                 : position_value (bike.render_position ()) + Vec3 (0, 0.4f, 0);
    const float away = gliding    ? 7.0f
                       : caddy    ? 7.5f
                       : boat     ? 7.0f
                       : ski      ? 5.5f
                       : tractor  ? 9.0f
                       : driving  ? 6.0f
                       : pedalled ? 3.4f
                                  : 4.5f;
    const Vec3 from = view == "front" ? heading * away + right * (away * 0.27f)
                                      : right * away + heading * 0.6f;
    session.camera ().place (at + from + Vec3 (0, 0.5f, 0), at);
  }

  bool CaptureCameras::film_boat (GameSession& session,
                                  const WorldGround& ground,
                                  std::string_view view,
                                  seconds_t interval) {
    const float dt = seconds_value (interval);
    static const float angle = [] {
      const char* value = moppe::environment ("MOPPE_RIDE_CAMERA_ANGLE");
      return value ? std::strtof (value, nullptr) * PI / 180.0f : 0.0f;
    }();
    const mov::Boat& boat = session.boat ();
    const Vec3 at = position_value (boat.position ()) + Vec3 (0, 0.6f, 0);
    Vec3 heading = direction_value (boat.forward ());
    heading[1] = 0.0f;
    heading = normalized (heading);
    // The subject eased, so the waves' heave and the hull's yawing
    // do not shake a camera that is meant to be flown.
    const bool fresh = film_time <= 0.0f;
    const auto ease = [dt] (float rate) {
      return 1.0f - std::exp (-rate * dt);
    };
    if (fresh) {
      film_at = at;
      film_heading = heading;
    }
    film_at += (at - film_at) * ease (5.0f);
    film_heading =
      normalized (film_heading + (heading - film_heading) * ease (0.9f));
    film_time += dt;
    const Vec3 h = film_heading;
    const Vec3 r (h[2], 0.0f, -h[0]);
    const auto around = [&] (float a) {
      return -h * std::cos (a) - r * std::sin (a);
    };
    const Vec3 up (0, 1, 0);
    Vec3 eye, look;
    float fov = 55.0f;
    bool clear = true;
    if (view == "drone") {
      eye = film_at + around (angle) * 26.0f + up * 13.0f;
      look = film_at + h * 8.0f;
      fov = 48.0f;
    } else if (view == "aerial") {
      eye = film_at + around (angle) * 75.0f + up * 48.0f;
      look = film_at + h * 6.0f;
      fov = 30.0f;
    } else if (view == "orbit") {
      eye = film_at + around (angle + 0.25f * film_time) * 15.0f + up * 3.5f;
      look = film_at;
      fov = 50.0f;
    } else if (view == "top") {
      eye = film_at + up * 30.0f - h * 3.0f;
      look = film_at + h * 2.0f;
      fov = 40.0f;
    } else if (view == "fpv") {
      const float sway = 0.35f * std::sin (0.45f * film_time);
      eye = film_at + around (angle + sway) * 5.0f + up * 1.0f;
      look = film_at + h * 14.0f + up * 0.3f;
      fov = 84.0f;
    } else if (view == "hull") {
      eye =
        position_value (boat.point (displacement (Vec3 (-1.35f, 0.45f, 1.3f))));
      look =
        position_value (boat.point (displacement (Vec3 (0.0f, 0.85f, -1.0f))));
      fov = 70.0f;
      clear = false;
    } else if (view == "stern") {
      eye =
        position_value (boat.point (displacement (Vec3 (0.4f, 1.45f, -2.9f))));
      look =
        position_value (boat.point (displacement (Vec3 (0.0f, 0.5f, 9.0f))));
      fov = 72.0f;
      clear = false;
    } else if (view == "flyby") {
      // A new stand whenever it has gone by and well off.
      const Vec3 off = film_stand - at;
      if (fresh || (length (off) > 65.0f && dot (off, h) < 0.0f)) {
        const float side = std::sin (angle) >= 0.0f ? 1.0f : -1.0f;
        film_stand = at + heading * 45.0f + r * (side * 8.0f);
        film_stand[1] =
          std::max (ground.depth (film_stand) + ground.height (film_stand),
                    ground.height (film_stand)) +
          1.1f;
      }
      // A long lens that keeps the boat the same size as it nears.
      eye = film_stand;
      look = at;
      fov = std::clamp (
        2.0f * std::atan (5.0f / length (at - eye)) * 180.0f / PI, 8.0f, 60.0f);
    } else if (view == "helm") {
      // The helmsman's eyes, through play's own camera.
      session.logic ().m_cam_mode = CAM_HELMET;
      film_fov = 75.0f;
      return true;
    } else {
      return false;
    }
    session.camera ().place (eye, look);
    if (clear)
      session.camera ().keep_above (ground.surface ());
    film_fov = fov;
    return true;
  }
}
