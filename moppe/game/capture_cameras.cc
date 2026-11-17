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

  namespace {
    // A displacement as what moves a position_t (docs/typed-quantities.md).
    auto camera_shift (const displacement_t& by) {
      return quantity_cast<isq::position_vector> (by);
    }

    // A quarter turn right of `way`, level.
    direction_t camera_right (const direction_t& way) {
      return unit_direction (
        Vec3 (direction_value (way)[2], 0.0f, -direction_value (way)[0]));
    }

    // Which way `facing` points, seen from above.
    direction_t camera_level (Vec3 facing) {
      facing[1] = 0.0f;
      return unit_direction (normalized (facing));
    }

    void camera_place (ChaseCamera& camera,
                       const position_t& eye,
                       const position_t& target) {
      camera.place (position_value (eye), position_value (target));
    }
  }

  void CaptureCameras::rest (GameSession& session, const WorldGround& ground) {
    const Camp& camp = session.logic ().m_camp;
    const proportion_t load =
      hammock_load (camp, session.logic ().m_total_time);
    if (!camp.hung || load <= 0.001f * proportion[one] ||
        session.logic ().m_doing != Doing::on_foot ())
      return;
    ChaseCamera& camera = session.camera ();
    const position_t from = position (camera.position ());
    const direction_t looking = unit_direction (camera.forward ());
    position_t eye;
    direction_t toward;
    if (session.logic ().m_cam_mode == CAM_HELMET) {
      eye = resting_eye (camp.hammock, camp.swing);
      toward = resting_gaze (camp.hammock, camp.gaze_yaw, camp.gaze_pitch);
    } else {
      const position_t subject = position (
        position_value (hammock_middle (camp.hammock)) - Vec3 (0, 0.45f, 0));
      const radians_t around = (radians_value (camp.gaze_yaw) + 0.45f) * u::rad;
      eye = position (
        position_value (subject) +
        (direction_value (hammock_across (camp.hammock)) *
           std::cos (radians_value (around)) +
         direction_value (hammock_along (camp.hammock)) *
           std::sin (radians_value (around))) *
          3.7f +
        Vec3 (0,
              0.75f +
                1.2f * (1.1f - camp.gaze_pitch.numerical_value_in (u::rad)),
              0));
      position_value (eye)[1] = std::max (
        static_cast<float> (position_value (eye)[1]),
        terrain::surface_elevation_value (ground.elevation (plan (eye))) +
          0.4f);
      toward = direction_of (subject - eye);
    }
    const position_t at =
      from + camera_shift ((eye - from) * load.numerical_value_in (one));
    const direction_t gaze = unit_direction (
      normalized (direction_value (looking) +
                  (direction_value (toward) - direction_value (looking)) *
                    load.numerical_value_in (one)));
    camera_place (camera, at, at + camera_shift (along (gaze, 10.0f * u::m)));
  }

  void CaptureCameras::walk_side (GameSession& session) {
    static const bool side = [] {
      const char* view = moppe::environment ("MOPPE_WALK_CAMERA");
      return view && std::string_view (view) == "side";
    }();
    if (!side || session.logic ().m_doing != Doing::on_foot ())
      return;
    const Walker& walker = session.walker ();
    const direction_t heading = unit_direction (walker.heading ());
    const direction_t right = camera_right (heading);
    const position_t at = position (walker.position () + Vec3 (0, 0.9f, 0));
    camera_place (session.camera (),
                  at + camera_shift (along (right, 4.0f * u::m)) +
                    camera_shift (displacement (Vec3 (0, 0.25f, 0))),
                  at);
  }

  void CaptureCameras::follow_animal (GameSession& session,
                                      const WorldGround& ground,
                                      seconds_t interval) {
    const seconds_t dt = interval;
    static const meters_t distance = [] {
      const char* value = moppe::environment ("MOPPE_ANIMAL_CAMERA");
      return (value ? static_cast<float> (::atof (value)) : 0.0f) * u::m;
    }();
    if (distance <= 0.0f * u::m || !watched ||
        *watched >= session.animals ().animals ().size ())
      return;
    const Animal& a = session.animals ().animals ()[*watched];
    const meters_t height = 0.7f * traits (a.species).length;
    const position_t target =
      a.position +
      camera_shift (displacement (Vec3 (0.0f, meters_value (height), 0.0f)));
    // Eased, so that the view glides after it.
    const blend_weight_t ease =
      (1.0f - std::exp (-1.5f * seconds_value (dt))) * blend_weight[one];
    watch_target = watch_target ? *watch_target +
                                    camera_shift ((target - *watch_target) *
                                                  ease.numerical_value_in (one))
                                : target;
    position_t eye =
      *watch_target + camera_shift (along (watch_side, distance)) +
      camera_shift (
        displacement (Vec3 (0.0f, meters_value (0.25f * distance), 0.0f)));
    position_value (eye)[1] = std::max<float> (
      position_value (eye)[1],
      terrain::surface_elevation_value (ground.elevation (plan (eye))) + 1.2f);
    camera_place (session.camera (), eye, *watch_target);
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
      const direction_t heading =
        length2 (Vec3 (load.forward[0], 0.0f, load.forward[2])) > 1e-6f
          ? camera_level (load.forward)
          : unit_direction (Vec3 (0, 0, 1));
      const direction_t right = camera_right (heading);
      const position_t at = position (load.position + Vec3 (0, 0.5f, 0));
      const displacement_t from =
        view == "front"
          ? along (heading, 7.5f * u::m) + along (right, 2.0f * u::m)
          : along (right, 7.5f * u::m) + along (heading, 0.6f * u::m);
      camera_place (session.camera (),
                    at + camera_shift (from) +
                      camera_shift (displacement (Vec3 (0, 0.5f, 0))),
                    at);
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
    const position_t car =
      position (tractor ? (session.tractor ().position () +
                           session.tractor ().trailer_position ()) *
                            0.5f
                : caddy ? session.caddy ().position ()
                : boat  ? session.boat ().position ()
                : ski   ? session.jet_ski ().position ()
                        : session.jeep ().position ());
    const direction_t heading =
      camera_level (gliding   ? session.glider ().heading ()
                    : tractor ? session.tractor ().forward ()
                    : caddy   ? session.caddy ().forward ()
                    : boat    ? session.boat ().forward ()
                    : ski     ? session.jet_ski ().forward ()
                    : driving ? session.jeep ().forward ()
                              : bike.render_orientation ());
    const direction_t right = camera_right (heading);
    const position_t at = position (
      gliding    ? session.glider ().position () - Vec3 (0, 0.9f, 0)
      : driving  ? position_value (car) + Vec3 (0, tractor ? 0.9f : 0.5f, 0)
      : pedalled ? bike.render_position () + Vec3 (0, 0.55f, 0)
                 : bike.render_position () + Vec3 (0, 0.4f, 0));
    const meters_t away = (gliding    ? 7.0f
                           : caddy    ? 7.5f
                           : boat     ? 7.0f
                           : ski      ? 5.5f
                           : tractor  ? 9.0f
                           : driving  ? 6.0f
                           : pedalled ? 3.4f
                                      : 4.5f) *
                          u::m;
    const displacement_t from =
      view == "front" ? along (heading, away) + along (right, away * 0.27f)
                      : along (right, away) + along (heading, 0.6f * u::m);
    camera_place (session.camera (),
                  at + camera_shift (from) +
                    camera_shift (displacement (Vec3 (0, 0.5f, 0))),
                  at);
  }

  bool CaptureCameras::film_boat (GameSession& session,
                                  const WorldGround& ground,
                                  std::string_view view,
                                  seconds_t interval) {
    const seconds_t dt = interval;
    static const radians_t angle = [] {
      const char* value = moppe::environment ("MOPPE_RIDE_CAMERA_ANGLE");
      return (value ? std::strtof (value, nullptr) * PI / 180.0f : 0.0f) *
             u::rad;
    }();
    const mov::Boat& boat = session.boat ();
    const position_t at = position (boat.position () + Vec3 (0, 0.6f, 0));
    const direction_t heading = camera_level (boat.forward ());
    // The subject eased, so the waves' heave and the hull's yawing
    // do not shake a camera that is meant to be flown.
    const bool fresh = film_time <= 0.0f * u::s;
    const auto ease = [dt] (per_second_t rate) {
      return 1.0f - std::exp ((-rate * dt).numerical_value_in (one));
    };
    if (fresh) {
      film_at = at;
      film_heading = heading;
    }
    film_at += (at - film_at) * ease (5.0f / u::s);
    film_heading = unit_direction (normalized (direction_value (
      film_heading + (heading - film_heading) * ease (0.9f / u::s))));
    film_time += dt;
    const direction_t h = film_heading;
    const direction_t r = camera_right (h);
    const auto around = [&] (radians_t a) {
      return unit_direction (
        -direction_value (h) * std::cos (radians_value (a)) -
        direction_value (r) * std::sin (radians_value (a)));
    };
    const direction_t up = unit_direction (Vec3 (0, 1, 0));
    const auto off = [] (const direction_t& way, float metres) {
      return camera_shift (along (way, metres * u::m));
    };
    position_t eye;
    position_t look;
    degrees_t fov = 55.0f * u::deg;
    bool clear = true;
    if (view == "drone") {
      eye = film_at + off (around (angle), 26.0f) + off (up, 13.0f);
      look = film_at + off (h, 8.0f);
      fov = 48.0f * u::deg;
    } else if (view == "aerial") {
      eye = film_at + off (around (angle), 75.0f) + off (up, 48.0f);
      look = film_at + off (h, 6.0f);
      fov = 30.0f * u::deg;
    } else if (view == "orbit") {
      eye = film_at +
            off (around (angle + 0.25f * u::rad / u::s * film_time), 15.0f) +
            off (up, 3.5f);
      look = film_at;
      fov = 50.0f * u::deg;
    } else if (view == "top") {
      eye = film_at + off (up, 30.0f) - off (h, 3.0f);
      look = film_at + off (h, 2.0f);
      fov = 40.0f * u::deg;
    } else if (view == "fpv") {
      const radians_t sway =
        0.35f * std::sin (0.45f * seconds_value (film_time)) * u::rad;
      eye = film_at + off (around (angle + sway), 5.0f) + off (up, 1.0f);
      look = film_at + off (h, 14.0f) + off (up, 0.3f);
      fov = 84.0f * u::deg;
    } else if (view == "hull") {
      eye = position (boat.point (Vec3 (-1.35f, 0.45f, 1.3f)));
      look = position (boat.point (Vec3 (0.0f, 0.85f, -1.0f)));
      fov = 70.0f * u::deg;
      clear = false;
    } else if (view == "stern") {
      eye = position (boat.point (Vec3 (0.4f, 1.45f, -2.9f)));
      look = position (boat.point (Vec3 (0.0f, 0.5f, 9.0f)));
      fov = 72.0f * u::deg;
      clear = false;
    } else if (view == "flyby") {
      // A new stand whenever it has gone by and well off.
      const displacement_t gone = film_stand - at;
      if (fresh ||
          (length (displacement_value (gone)) > 65.0f &&
           dot (displacement_value (gone), direction_value (h)) < 0.0f)) {
        const bool right_side = std::sin (radians_value (angle)) >= 0.0f;
        film_stand = at + off (heading, 45.0f) +
                     off (r, (right_side ? 1.0f : -1.0f) * 8.0f);
        const plan_position_t stand = plan (film_stand);
        const terrain::SurfaceElevation floor = ground.elevation (stand);
        position_value (film_stand)[1] =
          terrain::surface_elevation_value (
            std::max (floor + ground.water_depth (stand), floor)) +
          1.1f;
      }
      // A long lens that keeps the boat the same size as it nears.
      eye = film_stand;
      look = at;
      fov = std::clamp (
              2.0f * std::atan (5.0f / length (displacement_value (at - eye))) *
                180.0f / PI,
              8.0f,
              60.0f) *
            u::deg;
    } else if (view == "helm") {
      // The helmsman's eyes, through play's own camera.
      session.logic ().m_cam_mode = CAM_HELMET;
      film_fov = 75.0f * u::deg;
      return true;
    } else {
      return false;
    }
    camera_place (session.camera (), eye, look);
    if (clear)
      session.camera ().keep_above (ground.surface ());
    film_fov = fov;
    return true;
  }
}
