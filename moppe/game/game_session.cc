#include <moppe/game/game_session.hh>
#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace moppe::game {
  namespace {
    // The classic bike is a payload the glider carries about.
    void sync_attached_bike (GameSession& session) {
      const mov::Glider& glider = session.glider ();
      const Vec3 up = glider.up ();
      session.bike ().carry (glider.physical_position () -
                               up * mov::Glider::bike_hang * u::m,
                             glider.physical_velocity (),
                             glider.heading (),
                             up);
    }

    // The rigid bike flies: its own world moves it, wheels and all, with
    // the wing's pull on it, and the wing rides where the bike takes it.
    bool flies_bike (const GameSession& session) {
      return session.can_drop_bike () &&
             session.bike ().physics () == mov::BikePhysics::rigid;
    }

    position_t wing_over_bike (const mov::Vehicle& bike) {
      return moppe::position (bike.position () +
                              bike.render_normal () * mov::Glider::bike_hang);
    }

    // True once a wheel is down.
    bool fly_bike (GameSession& session, seconds_t dt) {
      mov::Vehicle& bike = session.bike ();
      mov::Glider& glider = session.glider ();
      const Vec3 pull =
        glider.fly (dt, wing_over_bike (bike), bike.physical_velocity ());
      // The wing crabs into the wind; the wheels should meet the ground
      // the way they are going over it.
      Vec3 track = bike.velocity ();
      track[1] = 0.0f;
      const Vec3 ahead =
        length2 (track) > 4.0f ? normalized (track) : glider.heading ();
      bike.hang ({ pull, ahead, glider.up () });
      bike.update (dt);
      bike.unhang ();
      glider.follow (wing_over_bike (bike), bike.physical_velocity ());
      return bike.grounded ();
    }

    void set_turn (GameSession& session, float value) {
      GameLogicState& logic = session.logic ();
      logic.m_turn_input = value;
      // On foot the mouse turns; the steering keys step sideways.
      if (logic.m_mode == M_FOOT)
        session.walker ().set_strafe (value);
      else if (logic.m_mode == M_GLIDER)
        session.glider ().set_turn (value);
      else if (logic.m_mode == M_JEEP)
        session.jeep ().set_turn (value);
      else if (logic.m_mode == M_HELICOPTER)
        session.helicopter ().set_turn (value);
      else if (logic.m_mode == M_TRACTOR)
        session.tractor ().set_turn (value);
      else if (logic.m_mode == M_CADDY)
        session.caddy ().set_turn (value);
      else if (logic.m_mode == M_BIKE)
        session.bike ().set_yaw ((90 * value) * u::deg);
      else if (logic.m_mode == M_MOUNTAIN_BIKE)
        session.mountain_bike ().set_yaw ((90 * value) * u::deg);
      else if (logic.m_mode == M_BOAT)
        session.boat ().set_turn (value);
    }

    void set_go (GameSession& session, float value) {
      GameLogicState& logic = session.logic ();
      logic.m_go_input = value;
      if (logic.m_mode == M_FOOT) {
        // Hauling on a rope is slow work.
        const float burden = logic.m_haul.active ? 0.6f : 1.0f;
        session.walker ().set_walk (burden *
                                    (value > 0 ? value : value * 0.6f));
      } else if (logic.m_mode == M_GLIDER)
        session.glider ().set_speed_control (value);
      else if (logic.m_mode == M_JEEP)
        session.jeep ().set_drive (value);
      else if (logic.m_mode == M_HELICOPTER)
        session.helicopter ().set_drive (value);
      else if (logic.m_mode == M_TRACTOR)
        session.tractor ().set_drive (value);
      else if (logic.m_mode == M_CADDY)
        session.caddy ().set_drive (value);
      else if (logic.m_mode == M_BIKE) {
        session.bike ().set_thrust (value);
        session.bike ().set_boost (logic.m_boost_input, logic.m_go_input);
      } else if (logic.m_mode == M_MOUNTAIN_BIKE)
        session.mountain_bike ().set_thrust (value);
      else if (logic.m_mode == M_BOAT)
        session.boat ().set_drive (value);
    }

    void set_boost (GameSession& session, float value) {
      GameLogicState& logic = session.logic ();
      const float previous = logic.m_boost_input;
      logic.m_boost_input = std::max (0.0f, std::min (1.0f, value));
      if (logic.m_mode == M_FOOT) {
        if (logic.m_boost_input > 0.1f && previous <= 0.1f)
          session.walker ().jump ();
      } else if (logic.m_mode == M_GLIDER) {
        session.glider ().set_flare (logic.m_boost_input > 0.1f);
      } else if (logic.m_mode == M_JEEP) {
        // What boosts the bike brakes the jeep.
        session.jeep ().set_brake (logic.m_boost_input > 0.1f);
      } else if (logic.m_mode == M_TRACTOR) {
        session.tractor ().set_brake (logic.m_boost_input > 0.1f);
      } else if (logic.m_mode == M_CADDY) {
        session.caddy ().set_brake (logic.m_boost_input > 0.1f);
      } else if (logic.m_mode == M_BIKE) {
        session.bike ().set_boost (logic.m_boost_input, logic.m_go_input);
      } else if (logic.m_mode == M_MOUNTAIN_BIKE) {
        // No jets on a mountainbike: the rider springs it up off the
        // ground instead.
        if (logic.m_boost_input > 0.1f && previous <= 0.1f)
          session.mountain_bike ().hop (3.0f);
      }
    }

    void deploy_glider (GameSession& session,
                        const map::SurfaceGeometry& terrain) {
      if (!session.can_deploy_glider (terrain))
        return;
      const Vec3 position = session.bike ().position ();
      const Vec3 heading = session.bike ().orientation ();
      const velocity_t inherited = session.bike ().physical_velocity ();
      session.bike ().set_thrust (0);
      session.bike ().set_yaw (0 * u::deg);
      session.bike ().set_boost (0, 0);
      session.glider ().launch (
        moppe::position (position + Vec3 (0, mov::Glider::bike_hang, 0)),
        inherited,
        heading,
        true);
      GameLogicState& logic = session.logic ();
      session.set_mode (M_GLIDER);
      session.glider ().set_turn (logic.m_turn_input);
      session.glider ().set_speed_control (logic.m_go_input);
      session.glider ().set_flare (logic.m_boost_input > 0.1f);
      if (!flies_bike (session))
        sync_attached_bike (session);
    }

    void use_glider_control (GameSession& session,
                             const map::SurfaceGeometry& terrain) {
      if (session.logic ().m_mode == M_GLIDER) {
        session.glider ().drop_bike ();
        return;
      }
      deploy_glider (session, terrain);
    }

    void finish_glide (GameSession& session) {
      GameLogicState& logic = session.logic ();
      if (session.glider ().bike_attached ()) {
        // The bike rides on with everything the glide gave it.
        if (!flies_bike (session))
          sync_attached_bike (session);
        session.glider ().drop_bike ();
        session.set_mode (M_BIKE);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        set_boost (session, 0);
        return;
      }

      // The pilot's feet swing down out of the harness and run the
      // landing out.
      const mov::Glider& glider = session.glider ();
      const Vec3 feet =
        glider.position () - Vec3 (0, mov::Glider::pilot_clearance - 0.1f, 0);
      session.walker ().spawn (
        moppe::position (feet), glider.heading (), glider.physical_velocity ());
      session.set_mode (M_FOOT);
      set_turn (session, logic.m_turn_input);
      set_go (session, logic.m_go_input);
      set_boost (session, 0);
    }

    // The helicopter climbs on what boosts the bike and descends on what
    // runs on foot (or the held deploy key, a controller's A).
    void set_lift (GameSession& session, const InputFrame& input) {
      if (session.logic ().m_mode != M_HELICOPTER)
        return;
      const float climb = input_value (input.boost);
      const bool descend = input.run || input.deploy_glider_held;
      session.helicopter ().set_lift (climb > 0.1f ? climb
                                      : descend    ? -1.0f
                                                   : 0.0f);
    }

    // The machine a mode drives, if any.
    std::optional<Machine> driven_machine (Mode mode) {
      switch (mode) {
      case M_BIKE:
        return Machine::bike;
      case M_JEEP:
        return Machine::jeep;
      case M_TRACTOR:
        return Machine::tractor;
      case M_CADDY:
        return Machine::caddy;
      case M_HELICOPTER:
        return Machine::helicopter;
      case M_MOUNTAIN_BIKE:
        return Machine::mountain_bike;
      default:
        return std::nullopt;
      }
    }

    std::optional<Machine> mounted_machine (GameSession::Mount mount) {
      switch (mount) {
      case GameSession::Mount::bike:
        return Machine::bike;
      case GameSession::Mount::jeep:
        return Machine::jeep;
      case GameSession::Mount::tractor:
        return Machine::tractor;
      case GameSession::Mount::caddy:
        return Machine::caddy;
      case GameSession::Mount::helicopter:
        return Machine::helicopter;
      case GameSession::Mount::mountain_bike:
        return Machine::mountain_bike;
      case GameSession::Mount::boat:
      case GameSession::Mount::none:
        break;
      }
      return std::nullopt;
    }

    Trouble& trouble_of (GameLogicState& logic, Machine machine) {
      return logic.m_trouble[static_cast<std::size_t> (machine)];
    }

    // Takes up a rope tied to the nearer end of a stuck machine, as long
    // as it reaches from there to the walker's feet.
    void take_rope (GameSession& session, Machine machine) {
      const MachineBody body = session.machine_body (machine);
      const Vec3 feet = session.walker ().position ();
      const bool front = dot (feet - body.position, body.forward) >= 0.0f;
      Haul& haul = session.logic ().m_haul;
      haul = { .active = true,
               .machine = machine,
               .tie = tie_point (machine, front) };
      Vec3 out = feet - body.point (haul.tie);
      out[1] = 0.0f;
      haul.rope = std::clamp (length (out), 1.0f, 1.8f);
    }

    // Lets the rope go; a machine let go of on purpose is taken to be
    // out of its trouble, so that the key gets into it again.
    void let_go (GameSession& session, bool freed) {
      GameLogicState& logic = session.logic ();
      Haul& haul = logic.m_haul;
      if (freed)
        trouble_of (logic, haul.machine) = {
          .anchor = session.machine_body (haul.machine).position
        };
      haul.active = false;
    }

    Vec3 level_heading (Vec3 heading) {
      heading[1] = 0.0f;
      return length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
    }

    float ground_at (const map::SurfaceGeometry& surface, const Vec3& at) {
      return terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (at[0], 0.0f, at[2]))));
    }

    // Unloads the bike and the jeep from the Caddy where it stands: the
    // bike down the open tailgate, the jeep beside the passenger's door.
    void unload_sling (GameSession& session,
                       const map::SurfaceGeometry& surface) {
      const mov::Caddy& caddy = session.caddy ();
      const Vec3 heading = level_heading (caddy.forward ());
      const Vec3 left = cross (Vec3 (0, 1, 0), heading);
      Vec3 bike = caddy.position () - heading * 4.8f;
      bike[1] = ground_at (surface, bike) + 1.2f;
      session.bike ().reset (bike);
      session.bike ().set_heading (heading);
      session.jeep ().place (caddy.position () - left * 3.4f, heading);
      session.logic ().m_sling.laden = false;
    }

    // Hangs the Caddy, the bike and the jeep in it, on the line under
    // the helicopter; set down beside it first when it stands further
    // off than the line reaches.
    void rig_sling (GameSession& session) {
      Sling& sling = session.logic ().m_sling;
      if (sling.hooked)
        return;
      const mov::Helicopter& heli = session.helicopter ();
      mov::Caddy& caddy = session.caddy ();
      const Vec3 hook = heli.point (sling_hook ());
      if (length (caddy.point (sling_ring ()) - hook) > sling_line + 3.0f)
        caddy.place (heli.point (Vec3 (7.0f, 0.0f, -0.5f)),
                     level_heading (heli.forward ()));
      sling = { .hooked = true, .laden = true };
    }

    // The line's pull between the helicopter and the Caddy, each taking
    // its share; and once the Caddy is let go of and stands still, it is
    // unloaded.
    void tend_sling (GameSession& session,
                     const map::SurfaceGeometry& surface,
                     float elapsed) {
      GameLogicState& logic = session.logic ();
      Sling& sling = logic.m_sling;
      mov::Helicopter& heli = session.helicopter ();
      mov::Caddy& caddy = session.caddy ();
      if (sling.hooked && logic.m_mode != M_HELICOPTER)
        sling.hooked = false;
      if (!sling.hooked) {
        heli.set_load (Vec3 (), 0.0f);
        sling.stretch = 0.0f;
        if (sling.laden && caddy.grounded () &&
            length2 (caddy.velocity ()) < 0.5f * 0.5f)
          unload_sling (session, surface);
        return;
      }
      const Vec3 hook = heli.point (sling_hook ());
      const Vec3 ring = caddy.point (sling_ring ());
      const Vec3 pull = sling_pull (sling, hook, ring, elapsed);
      if (length2 (pull) > 0.0f) {
        caddy.push (ring, pull * elapsed);
        heli.push (hook, -pull * elapsed);
      }
      heli.set_load (-pull, sling_hang);
    }

    void toggle_mount (GameSession& session) {
      GameLogicState& logic = session.logic ();
      // A passenger's seat is the game's to give and take away.
      if (logic.m_mode == M_GLIDER || logic.m_mode == M_PASSENGER)
        return;

      if (logic.m_mode == M_HELICOPTER) {
        // With the Caddy on the line, the key lets it go first.
        if (logic.m_sling.hooked) {
          logic.m_sling.hooked = false;
          return;
        }
        // Out on the pilot's side, the right, once down near the ground.
        if (!session.can_step_out_of_helicopter ())
          return;
        mov::Helicopter& heli = session.helicopter ();
        heli.set_drive (0);
        heli.set_turn (0);
        heli.set_lift (0);
        Vec3 heading = heli.forward ();
        heading[1] = 0.0f;
        session.walker ().spawn (
          moppe::position (heli.point (Vec3 (-2.0f, 0.3f, 0.4f))),
          length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1),
          moppe::velocity (heli.velocity ()));
        session.set_mode (M_FOOT);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        return;
      }

      if (logic.m_mode == M_TRACTOR) {
        // Down off the left side, ahead of the big wheel.
        mov::Tractor& tractor = session.tractor ();
        tractor.set_drive (0);
        tractor.set_turn (0);
        tractor.set_brake (false);
        session.walker ().spawn (
          moppe::position (tractor.point (Vec3 (1.2f, 0.1f, 0.35f))),
          tractor.forward (),
          moppe::velocity (tractor.velocity ()));
        session.set_mode (M_FOOT);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        return;
      }

      if (logic.m_mode == M_CADDY) {
        // Out of the driver's door, on the left.
        mov::Caddy& caddy = session.caddy ();
        caddy.set_drive (0);
        caddy.set_turn (0);
        caddy.set_brake (false);
        session.walker ().spawn (
          moppe::position (caddy.point (Vec3 (1.35f, 0.2f, 0.25f))),
          caddy.forward (),
          moppe::velocity (caddy.velocity ()));
        session.set_mode (M_FOOT);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        return;
      }

      if (logic.m_mode == M_MOUNTAIN_BIKE) {
        // Off on the left, as a cyclist gets off.
        mov::Vehicle& bike = session.mountain_bike ();
        const Vec3 heading = level_heading (bike.orientation ());
        session.walker ().spawn (
          moppe::position (bike.position () +
                           cross (Vec3 (0, 1, 0), heading) * 0.8f),
          heading,
          bike.physical_velocity ());
        bike.set_thrust (0);
        bike.set_yaw (0 * u::deg);
        bike.set_skid (0);
        session.set_mode (M_FOOT);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        return;
      }

      if (logic.m_mode == M_BOAT) {
        // Over whichever side, or the bow, has the shallowest water.
        mov::Boat& boat = session.boat ();
        boat.set_drive (0);
        boat.set_turn (0);
        session.walker ().spawn (moppe::position (session.boat_landing ()),
                                 level_heading (boat.forward ()),
                                 moppe::velocity (boat.velocity () * 0.5f));
        session.set_mode (M_FOOT);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        return;
      }

      if (logic.m_mode == M_JEEP) {
        // Step out on the driver's side.
        mov::Jeep& jeep = session.jeep ();
        jeep.set_drive (0);
        jeep.set_turn (0);
        jeep.set_brake (false);
        session.walker ().spawn (
          moppe::position (jeep.point (Vec3 (1.75f, 0.3f, -0.1f))),
          jeep.forward (),
          moppe::velocity (jeep.velocity ()));
        session.set_mode (M_FOOT);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        return;
      }

      // On foot, holding a rope: let go of it. Beside a stuck machine:
      // take up a rope to haul it out.
      if (logic.m_mode == M_FOOT && logic.m_haul.active) {
        let_go (session, true);
        return;
      }
      if (const std::optional<Machine> stuck = session.haul_within_reach ()) {
        take_rope (session, *stuck);
        return;
      }

      // On foot: take whatever stands nearest within reach.
      const GameSession::Mount reach = session.mount_within_reach ();
      const Mode into = reach == GameSession::Mount::jeep    ? M_JEEP
                        : reach == GameSession::Mount::tractor ? M_TRACTOR
                        : reach == GameSession::Mount::caddy   ? M_CADDY
                        : reach == GameSession::Mount::mountain_bike
                          ? M_MOUNTAIN_BIKE
                        : reach == GameSession::Mount::helicopter
                          ? M_HELICOPTER
                        : reach == GameSession::Mount::boat ? M_BOAT
                                                            : M_FOOT;
      if (into != M_FOOT) {
        session.set_mode (into);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        if (into != M_HELICOPTER && into != M_BOAT)
          set_boost (session, logic.m_boost_input);
        return;
      }

      if (logic.m_mode != M_FOOT) {
        // Step off to the side of the bike.
        mov::Vehicle& vehicle = session.bike ();
        const Vec3 heading = vehicle.orientation ();
        const Vec3 side (heading[2], 0, -heading[0]);
        session.walker ().spawn (
          moppe::position (vehicle.position () + side * 1.8f),
          heading,
          vehicle.physical_velocity ());
        vehicle.set_thrust (0);
        vehicle.set_yaw (0 * u::deg);
        vehicle.set_boost (0, 0);
        vehicle.set_skid (0);
        session.set_mode (M_FOOT);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        return;
      }

      // On foot: remount the bike when standing next to it.
      if (reach == GameSession::Mount::bike) {
        session.bike ().set_thrust (0);
        session.bike ().set_yaw (0 * u::deg);
        session.set_mode (M_BIKE);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        set_boost (session, logic.m_boost_input);
      }
    }

    // Riding, driving, or gliding, a look the player has let go of finds
    // its way back to the way ahead: soon after a glance, later after a
    // long look aside, and gently, gathering pace as it comes round. A
    // machine at rest leaves the view where it was put, there being no
    // way ahead to watch. On foot the walker turns with the look, so it
    // is always ahead.
    void settle_look (GameLogicState& logic, float speed, float elapsed) {
      logic.m_look_idle += elapsed;
      if (logic.m_mode == M_FOOT)
        return;
      const float aside =
        std::max (std::abs (logic.m_look_yaw), std::abs (logic.m_look_pitch));
      const float wait = 0.6f + 1.4f * smoothstep (0.15f, 1.2f, aside);
      const float rate = 1.3f *
                         smoothstep (wait, wait + 1.0f, logic.m_look_idle) *
                         smoothstep (0.5f, 6.0f, speed);
      const float settle = smoothing_alpha (rate / u::s, elapsed * u::s);
      logic.m_look_yaw -= logic.m_look_yaw * settle;
      logic.m_look_pitch -= logic.m_look_pitch * settle;
    }

    void apply_input_frame (GameSession& session,
                            const map::SurfaceGeometry& terrain,
                            const InputFrame& input) {
      set_turn (session, input_value (input.turn));
      set_go (session, input_value (input.drive));
      set_boost (session, input_value (input.boost));
      set_lift (session, input);
      GameLogicState& logic = session.logic ();
      // Riding, what runs on foot stamps on the rear brake.
      session.bike ().set_skid (logic.m_mode == M_BIKE && input.run ? 1 : 0);
      session.mountain_bike ().set_skid (
        logic.m_mode == M_MOUNTAIN_BIKE && input.run ? 1 : 0);
      logic.m_look_pitch =
        std::clamp (logic.m_look_pitch + input.look_pitch, -1.35f, 1.35f);
      if (input.look_yaw != 0.0f || input.look_pitch != 0.0f)
        logic.m_look_idle = 0.0f;
      if (logic.m_mode == M_FOOT) {
        // The walker turns with the look, and a following camera turns
        // with them at once rather than catching up.
        session.walker ().turn_by (input.look_yaw);
        session.walker ().set_run (input.run && !logic.m_haul.active);
        if (logic.m_cam_mode != CAM_HELMET)
          session.camera ().turn (input.look_yaw, 0.0f);
        logic.m_look_yaw = 0.0f;
      } else {
        // Riding, the look swings about the way ahead, all the way round.
        logic.m_look_yaw =
          std::remainder (logic.m_look_yaw + input.look_yaw, 6.2831853f);
      }

      if (input.deploy_glider)
        use_glider_control (session, terrain);
      else if (input.deploy_glider_held && session.logic ().m_mode != M_GLIDER)
        deploy_glider (session, terrain);
      if (input.toggle_mount) {
        if (session.can_deploy_glider (terrain) || session.can_drop_bike ())
          use_glider_control (session, terrain);
        else
          toggle_mount (session);
      }
      if (input.cycle_camera) {
        GameLogicState& logic = session.logic ();
        logic.m_cam_mode = (CamMode)((logic.m_cam_mode + 1) % 3);
        if (logic.m_cam_mode == CAM_HELMET)
          logic.m_fp_eye = session.camera ().position ();
      }
    }
  }

  namespace {
    // A puff of the ground thrown up where a machine heaves or two
    // machines knock together.
    void puff (GameSession& session,
               const Vec3& at,
               const Vec3& moving,
               float strength,
               const GroundCover& ground) {
      Dust::Style burst;
      burst.size = 0.35f * u::m;
      burst.lifetime = 0.9f * u::s;
      burst.downward_acceleration =
        3.0f * isq::acceleration[u::m / pow<2> (u::s)];
      burst.spread = 1.4f * one;
      session.dust ().emit (
        moppe::position (at),
        velocity (moving * 0.2f + Vec3 (0, 1.5f, 0)),
        std::max (2, static_cast<int> (std::round (strength * ground.dust))),
        ground.dust_color,
        burst);
    }

    // Pulls the machine on the rope after the walker, and holds the
    // walker to the rope's length.
    void tend_haul (GameSession& session, float elapsed) {
      GameLogicState& logic = session.logic ();
      const Haul& haul = logic.m_haul;
      if (!haul.active)
        return;
      if (logic.m_mode != M_FOOT) {
        let_go (session, false);
        return;
      }
      const MachineBody body = session.machine_body (haul.machine);
      const Vec3 tie = body.point (haul.tie);
      Walker& walker = session.walker ();
      Vec3 out = walker.position () - tie;
      const float drop = std::abs (out[1]);
      out[1] = 0.0f;
      // Pulled out of the hand: the machine fell away, or the walker did.
      if (length (out) > haul.rope + 4.0f || drop > 4.0f) {
        let_go (session, false);
        return;
      }
      const HaulForce force =
        haul_force (haul, body, walker.position (), walker.velocity ());
      session.push_machine (haul.machine, tie, force.pull * elapsed);
      session.push_machine (haul.machine,
                            body.point (machine_centre (haul.machine)),
                            force.lift * elapsed);
      walker.tether (
        tie, std::max (0.6f, haul.rope - rope_give), haul.rope + rope_give);
    }

    // Notes how each machine is getting on, and heaves the one being
    // driven if it is stuck.
    void strain_machines (GameSession& session,
                          float elapsed,
                          const GroundCover& ground) {
      GameLogicState& logic = session.logic ();
      const std::optional<Machine> driven = driven_machine (logic.m_mode);
      for (int i = 0; i < machine_count; ++i) {
        const Machine machine = static_cast<Machine> (i);
        const MachineBody body = session.machine_body (machine);
        Trouble& trouble = trouble_of (logic, machine);
        // The helicopter flies out of any hole.
        const float throttle =
          driven == machine && machine != Machine::helicopter ? logic.m_go_input
                                                              : 0.0f;
        if (!strain (trouble, body.position, throttle, elapsed))
          continue;
        const Vec3 kick = heave (trouble, body.forward, throttle);
        ++trouble.heaves;
        session.push_machine (
          machine, body.point (machine_centre (machine)), kick * body.mass);
        puff (session, body.position - body.up * 0.3f, kick, 10.0f, ground);
        logic.m_shake = std::max (logic.m_shake, 0.05f);
        logic.m_shake_time = 0.0f;
      }
    }

    // Machines that meet knock each other apart, ours and the other
    // player's alike.
    void bump_machines (GameSession& session,
                        std::span<const MachineBody> strangers,
                        const GroundCover& ground) {
      GameLogicState& logic = session.logic ();
      std::vector<MachineBody> bodies;
      bodies.reserve (machine_count + strangers.size ());
      // The bike and the jeep stowed in the Caddy meet nothing.
      for (int i = 0; i < machine_count; ++i) {
        const Machine machine = static_cast<Machine> (i);
        if (!session.stowed (machine))
          bodies.push_back (session.machine_body (machine));
      }
      bodies.insert (bodies.end (), strangers.begin (), strangers.end ());
      const std::optional<Machine> driven = driven_machine (logic.m_mode);
      for (const Bump& bump : find_bumps (bodies)) {
        const MachineBody& a = bodies[bump.a];
        const MachineBody& b = bodies[bump.b];
        if (a.ours)
          session.push_machine (a.machine, bump.at, bump.impulse);
        if (b.ours)
          session.push_machine (b.machine, bump.at, -bump.impulse);
        if (bump.speed < 2.5f)
          continue;
        puff (session,
              bump.at,
              a.velocity * 0.5f + b.velocity * 0.5f,
              std::min (8.0f, bump.speed),
              ground);
        const bool felt =
          (a.ours && a.machine == driven) || (b.ours && b.machine == driven);
        if (felt) {
          logic.m_shake =
            std::max (logic.m_shake, std::min (0.2f, 0.02f * bump.speed));
          logic.m_shake_time = 0.0f;
        }
      }
    }
  }

  // Where the mountainbike first stands: on the far side of the motocross,
  // both facing 45 degrees round from +z, from where its rider gets off,
  // a little behind it (game.cc parks it there again on the ground).
  namespace {
    position_t mountain_bike_stand (const WorldParams& world) {
      const Vec3 heading (0.70710678f, 0.0f, 0.70710678f);
      const Vec3 side (heading[2], 0.0f, -heading[0]);
      return moppe::position (position_value (world.spawn_position ()) -
                              side * 2.6f - heading * 1.2f);
    }
  }

  MachineBody GameSession::machine_body (Machine machine) const {
    MachineBody body;
    body.machine = machine;
    switch (machine) {
    case Machine::bike:
      body.position = m_bike.position ();
      body.forward = m_bike.orientation ();
      body.up = m_bike.render_normal ();
      body.velocity = m_bike.velocity ();
      body.mass = m_bike.mass ();
      break;
    case Machine::jeep:
      body.position = m_jeep.position ();
      body.forward = m_jeep.forward ();
      body.up = m_jeep.up ();
      body.velocity = m_jeep.velocity ();
      body.mass = m_jeep.mass ();
      break;
    case Machine::tractor:
      body.position = m_tractor.position ();
      body.forward = m_tractor.forward ();
      body.up = m_tractor.up ();
      body.velocity = m_tractor.velocity ();
      body.mass = m_tractor.mass ();
      break;
    case Machine::caddy:
      body.position = m_caddy.position ();
      body.forward = m_caddy.forward ();
      body.up = m_caddy.up ();
      body.velocity = m_caddy.velocity ();
      body.mass = m_caddy.mass ();
      break;
    case Machine::helicopter:
      body.position = m_helicopter.position ();
      body.forward = m_helicopter.forward ();
      body.up = m_helicopter.up ();
      body.velocity = m_helicopter.velocity ();
      body.mass = m_helicopter.mass ();
      break;
    case Machine::mountain_bike:
      body.position = m_mountain_bike.position ();
      body.forward = m_mountain_bike.orientation ();
      body.up = m_mountain_bike.render_normal ();
      body.velocity = m_mountain_bike.velocity ();
      body.mass = m_mountain_bike.mass ();
      break;
    }
    return body;
  }

  void GameSession::push_machine (Machine machine,
                                  const Vec3& at,
                                  const Vec3& impulse) {
    switch (machine) {
    case Machine::bike:
      m_bike.push (at, impulse);
      break;
    case Machine::jeep:
      m_jeep.push (at, impulse);
      break;
    case Machine::tractor:
      m_tractor.push (at, impulse);
      break;
    case Machine::caddy:
      m_caddy.push (at, impulse);
      break;
    case Machine::helicopter:
      m_helicopter.push (at, impulse);
      break;
    case Machine::mountain_bike:
      m_mountain_bike.push (at, impulse);
      break;
    }
  }

  std::optional<Machine> GameSession::haul_within_reach () const {
    if (m_logic.m_mode != M_FOOT || m_logic.m_haul.active)
      return std::nullopt;
    const std::optional<Machine> machine =
      mounted_machine (mount_within_reach ());
    if (!machine ||
        !m_logic.m_trouble[static_cast<std::size_t> (*machine)].stuck)
      return std::nullopt;
    return machine;
  }

  std::optional<Machine> GameSession::hauling () const {
    if (m_logic.m_mode != M_FOOT || !m_logic.m_haul.active)
      return std::nullopt;
    return m_logic.m_haul.machine;
  }

  Vec3 GameSession::rope_end () const {
    const Haul& haul = m_logic.m_haul;
    return machine_body (haul.machine).point (haul.tie);
  }

  bool GameSession::stuck () const {
    const std::optional<Machine> driven = driven_machine (m_logic.m_mode);
    return driven &&
           m_logic.m_trouble[static_cast<std::size_t> (*driven)].stuck;
  }

  GameSession::GameSession (const WorldParams& world,
                            const map::SurfaceGeometry& surface,
                            mov::BikePhysics physics)
      : m_bike (world.spawn_position (),
                45 * u::deg,
                surface,
                2600 * u::N,
                30 * u::kW,
                150 * u::kg,
                physics),
        m_glider (surface), m_jeep (surface), m_helicopter (surface),
        m_tractor (surface), m_caddy (surface),
        // A rider of 80 kg on 15 kg of bike, putting down some 750 W at
        // most and 320 N at the tyre.
        m_mountain_bike (mountain_bike_stand (world),
                         45 * u::deg,
                         surface,
                         320 * u::N,
                         750 * u::W,
                         95 * u::kg,
                         mov::BikePhysics::rigid,
                         mov::mountain_bike_rig),
        m_boat (surface),
        m_camera (18 * u::deg, 6.5f * u::m) {}

  Vec3 GameSession::subject_position () const {
    if (m_logic.m_mode == M_FOOT)
      return m_walker.position ();
    if (m_logic.m_mode == M_GLIDER)
      return m_glider.position ();
    if (m_logic.m_mode == M_JEEP)
      return m_jeep.position ();
    if (m_logic.m_mode == M_HELICOPTER)
      return m_helicopter.position ();
    if (m_logic.m_mode == M_TRACTOR)
      return m_tractor.position ();
    if (m_logic.m_mode == M_CADDY)
      return m_caddy.position ();
    if (m_logic.m_mode == M_PASSENGER)
      return m_logic.m_seat_position;
    if (m_logic.m_mode == M_MOUNTAIN_BIKE)
      return m_mountain_bike.position ();
    if (m_logic.m_mode == M_BOAT)
      return m_boat.position ();
    return m_bike.position ();
  }

  Vec3 GameSession::subject_heading () const {
    if (m_logic.m_mode == M_FOOT)
      return m_walker.heading ();
    if (m_logic.m_mode == M_GLIDER)
      return m_glider.heading ();
    if (m_logic.m_mode == M_JEEP)
      return m_jeep.forward ();
    if (m_logic.m_mode == M_HELICOPTER)
      return m_helicopter.forward ();
    if (m_logic.m_mode == M_TRACTOR)
      return m_tractor.forward ();
    if (m_logic.m_mode == M_CADDY)
      return m_caddy.forward ();
    if (m_logic.m_mode == M_PASSENGER)
      return m_logic.m_seat_heading;
    if (m_logic.m_mode == M_MOUNTAIN_BIKE)
      return m_mountain_bike.orientation ();
    if (m_logic.m_mode == M_BOAT)
      return m_boat.forward ();
    return m_bike.orientation ();
  }

  float GameSession::subject_speed_kmh () const {
    if (m_logic.m_mode == M_FOOT)
      return 0.0f;
    if (m_logic.m_mode == M_GLIDER)
      return m_glider.airspeed ().numerical_value_in (u::m / u::s) * 3.6f;
    if (m_logic.m_mode == M_JEEP)
      return length (m_jeep.velocity ()) * 3.6f;
    if (m_logic.m_mode == M_HELICOPTER)
      return length (m_helicopter.velocity ()) * 3.6f;
    if (m_logic.m_mode == M_TRACTOR)
      return length (m_tractor.velocity ()) * 3.6f;
    if (m_logic.m_mode == M_CADDY)
      return length (m_caddy.velocity ()) * 3.6f;
    if (m_logic.m_mode == M_PASSENGER)
      return length (m_logic.m_seat_velocity) * 3.6f;
    if (m_logic.m_mode == M_MOUNTAIN_BIKE)
      return length (m_mountain_bike.velocity ()) * 3.6f;
    if (m_logic.m_mode == M_BOAT)
      return length (m_boat.velocity ()) * 3.6f;
    return length (m_bike.velocity ()) * 3.6f;
  }

  GameSession::Mount GameSession::mount_within_reach () const {
    if (m_logic.m_mode != M_FOOT)
      return Mount::none;
    const Vec3 feet = m_walker.position ();
    // Each measured from where one gets on or in, with how far off that
    // may be: the bike's saddle, the jeep's middle, the helicopter's
    // cabin a little ahead of its mast, the tractor between its axles,
    // the Caddy's cab, and the mountainbike between its wheels.
    const struct {
      Mount mount;
      Vec3 at;
      float reach;
    } mounts[] = {
      { Mount::bike, m_bike.position (), 5.0f },
      { Mount::jeep, m_jeep.position (), 4.5f },
      { Mount::helicopter, m_helicopter.point (Vec3 (0, 0, 0.4f)), 4.0f },
      { Mount::tractor, m_tractor.position (), 3.6f },
      { Mount::caddy, m_caddy.point (Vec3 (0, 0, 0.25f)), 3.4f },
      { Mount::mountain_bike, m_mountain_bike.position (), 2.4f },
      // The boat from its middle: it may lie a step out in the water.
      { Mount::boat, m_boat.point (Vec3 (0, 0, 0.6f)), 4.0f },
    };
    Mount nearest = Mount::none;
    float best = 1e30f;
    for (const auto& m : mounts) {
      if (m_logic.m_sling.laden &&
          (m.mount == Mount::bike || m.mount == Mount::jeep))
        continue;
      const float d = length2 (feet - m.at);
      if (d < m.reach * m.reach && d < best) {
        best = d;
        nearest = m.mount;
      }
    }
    return nearest;
  }

  bool GameSession::can_drive () const {
    return mount_within_reach () == Mount::jeep;
  }

  bool GameSession::can_fly () const {
    return mount_within_reach () == Mount::helicopter;
  }

  bool GameSession::can_drive_tractor () const {
    return mount_within_reach () == Mount::tractor;
  }

  bool GameSession::can_drive_caddy () const {
    return mount_within_reach () == Mount::caddy;
  }

  bool GameSession::can_ride_mountain_bike () const {
    return mount_within_reach () == Mount::mountain_bike;
  }

  bool GameSession::can_take_boat () const {
    return mount_within_reach () == Mount::boat;
  }

  bool GameSession::stowed (Machine machine) const {
    return m_logic.m_sling.laden &&
           (machine == Machine::bike || machine == Machine::jeep);
  }

  bool GameSession::can_step_out_of_helicopter () const {
    return m_logic.m_mode == M_HELICOPTER && m_helicopter.altitude () < 1.2f &&
           length (m_helicopter.velocity ()) < 4.0f;
  }

  bool
  GameSession::can_deploy_glider (const map::SurfaceGeometry& terrain) const {
    if (m_logic.m_mode != M_BIKE || !m_bike.airborne ())
      return false;
    const Vec3 position = m_bike.position ();
    const float ground = terrain::surface_elevation_value (
      spatial::sample<terrain::surface_elevation> (
        terrain, moppe::position (Vec3 (position[0], 0.0f, position[2]))));
    return position[1] - ground > 3.0f;
  }

  bool GameSession::can_drop_bike () const {
    return m_logic.m_mode == M_GLIDER && m_glider.bike_attached ();
  }

  void GameSession::set_mode (Mode mode) {
    if (mode == M_HELICOPTER && m_logic.m_mode != M_HELICOPTER)
      rig_sling (*this);
    const bool was_on_foot = m_logic.m_mode == M_FOOT;
    const bool on_foot = mode == M_FOOT;
    m_logic.m_mode = mode;
    if (was_on_foot == on_foot)
      return;
    (was_on_foot ? m_logic.m_foot_cam : m_logic.m_ride_cam) =
      m_logic.m_cam_mode;
    m_logic.m_cam_mode = on_foot ? m_logic.m_foot_cam : m_logic.m_ride_cam;
    // Eyes open where the camera was and settle into the head, as when
    // Tab chooses them.
    if (m_logic.m_cam_mode == CAM_HELMET)
      m_logic.m_fp_eye = m_camera.position ();
  }

  void GameSession::start_on_foot () {
    if (m_logic.m_mode == M_BIKE)
      toggle_mount (*this);
    m_logic.m_cam_mode = CAM_CHASE;
    m_logic.m_look_pitch = 0.0f;
    m_logic.m_fp_eye = m_walker.eye_position ();
    m_camera.reset ();
  }

  CameraRig GameSession::camera_rig () const {
    CameraRig rig;
    switch (m_logic.m_mode) {
    case M_FOOT:
      rig.eye = m_walker.eye_position ();
      rig.ahead = m_walker.heading ();
      rig.pivot = m_walker.position () + Vec3 (0, 1.35f, 0);
      rig.velocity = m_walker.velocity ();
      rig.distance = 3.8f * u::m;
      rig.nearest = 0.9f * u::m;
      rig.elevation = 10.0f * u::deg;
      break;
    case M_GLIDER:
      rig.eye = m_glider.position () - Vec3 (0, 0.75f, 0);
      rig.ahead = m_glider.heading ();
      rig.pivot = position_value (m_glider.physical_position ());
      rig.velocity = velocity_value (m_glider.physical_velocity ());
      break;
    case M_JEEP:
      rig.eye = m_jeep.driver_seat () + m_jeep.up () * 0.78f +
                m_jeep.forward () * 0.08f;
      rig.ahead = m_jeep.forward ();
      rig.pivot = m_jeep.position () + Vec3 (0, 0.9f, 0);
      rig.velocity = m_jeep.velocity ();
      rig.distance = 8.0f * u::m;
      rig.nearest = 3.4f * u::m;
      break;
    case M_PASSENGER:
      rig.eye = m_logic.m_seat_position + m_logic.m_seat_up * 0.78f +
                m_logic.m_seat_heading * 0.08f;
      rig.ahead = m_logic.m_seat_heading;
      rig.pivot = m_logic.m_seat_position + Vec3 (0, 0.5f, 0);
      rig.velocity = m_logic.m_seat_velocity;
      rig.distance = 8.0f * u::m;
      rig.nearest = 3.4f * u::m;
      break;
    case M_CADDY:
      rig.eye = m_caddy.driver_seat () + m_caddy.up () * 0.70f +
                m_caddy.forward () * 0.06f;
      rig.ahead = m_caddy.forward ();
      rig.pivot = m_caddy.position () + Vec3 (0, 1.0f, 0);
      rig.velocity = m_caddy.velocity ();
      rig.distance = 8.5f * u::m;
      rig.nearest = 3.6f * u::m;
      break;
    case M_TRACTOR:
      rig.eye = m_tractor.driver_seat () + m_tractor.up () * 0.80f +
                m_tractor.forward () * 0.1f;
      rig.ahead = m_tractor.forward ();
      rig.pivot = m_tractor.position () + Vec3 (0, 1.5f, 0);
      rig.velocity = m_tractor.velocity ();
      rig.distance = 6.0f * u::m;
      rig.nearest = 3.0f * u::m;
      break;
    case M_HELICOPTER: {
      rig.eye = m_helicopter.pilot_seat () + m_helicopter.up () * 0.78f +
                m_helicopter.forward () * 0.1f;
      // Behind the way it heads, level, whatever its nose is doing.
      Vec3 ahead = m_helicopter.forward ();
      ahead[1] = 0.0f;
      rig.ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
      rig.pivot = m_helicopter.position () + Vec3 (0, 1.6f, 0);
      rig.velocity = m_helicopter.velocity ();
      rig.distance = 12.0f * u::m;
      rig.nearest = 5.0f * u::m;
      rig.elevation = 21.0f * u::deg;
      // Further off and lower, to see the Caddy hanging under it too.
      if (m_logic.m_sling.hooked) {
        rig.pivot = rig.pivot * 0.65f + m_caddy.position () * 0.35f;
        rig.distance = 24.0f * u::m;
        rig.nearest = 8.0f * u::m;
        rig.elevation = 14.0f * u::deg;
      }
      break;
    }
    case M_BOAT:
      rig.eye = m_boat.helm_seat () + m_boat.up () * 0.85f +
                m_boat.forward () * 0.1f;
      rig.ahead = m_boat.forward ();
      rig.pivot = m_boat.position () + Vec3 (0, 1.0f, 0);
      rig.velocity = m_boat.velocity ();
      rig.distance = 7.5f * u::m;
      rig.nearest = 3.2f * u::m;
      rig.elevation = 16.0f * u::deg;
      break;
    case M_MOUNTAIN_BIKE: {
      // The bike is life size, its reference point between the axles at
      // their height; the rider's eyes are some 1.3 m over that.
      const mov::Vehicle& bike = m_mountain_bike;
      rig.eye = bike.position () + bike.render_normal () * 1.3f +
                bike.orientation () * 0.1f;
      rig.ahead = bike.orientation ();
      rig.pivot = bike.position () + Vec3 (0, 0.75f, 0);
      rig.velocity = bike.velocity ();
      rig.distance = 4.6f * u::m;
      rig.nearest = 1.6f * u::m;
      rig.elevation = 15.0f * u::deg;
      break;
    }
    case M_BIKE:
      rig.eye =
        m_bike.position () + Vec3 (0, 0.95f, 0) + m_bike.orientation () * 0.4f;
      rig.ahead = m_bike.orientation ();
      rig.pivot = position_value (m_bike.physical_position ());
      rig.velocity = velocity_value (m_bike.physical_velocity ());
      break;
    }
    return rig;
  }

  void GameSession::follow_camera (const map::SurfaceGeometry& surface,
                                   seconds_t dt) {
    const CameraRig rig = camera_rig ();
    if (m_logic.m_cam_mode == CAM_HELMET) {
      // Ride inside the head, lightly smoothed so terrain bumps do not
      // rattle the eyeballs. The head turns about the vertical and then
      // nods about its own right, from the way ahead.
      const Vec3 up (0, 1, 0);
      Vec3 look =
        Quaternion::rotate (rig.ahead, up, -m_logic.m_look_yaw * u::rad);
      Vec3 right = cross (look, up);
      if (length2 (right) > 1e-6f) {
        normalize (right);
        look = Quaternion::rotate (look, right, m_logic.m_look_pitch * u::rad);
      }
      // A helicopter flies too fast for the eye to chase: it is carried
      // along, and only what the cabin does about it is smoothed.
      if (m_logic.m_mode == M_HELICOPTER)
        m_logic.m_fp_eye +=
          rig.velocity * static_cast<float> (dt.numerical_value_in (u::s));
      m_logic.m_fp_eye +=
        (rig.eye - m_logic.m_fp_eye) * smoothing_alpha (25.0f / u::s, dt);
      m_camera.place (m_logic.m_fp_eye, m_logic.m_fp_eye + look * 10.0f);
      return;
    }
    // Looking up swings the camera down behind the subject, below its
    // level and in close over the ground; looking down lifts it nearly
    // overhead. The look is held to what the camera can show, so turning
    // back answers at once.
    const float base = rig.elevation.numerical_value_in (u::rad);
    const float raised =
      std::clamp (base - m_logic.m_look_pitch, -0.75f, 1.35f);
    m_logic.m_look_pitch = base - raised;
    const float flip = m_logic.m_cam_mode == CAM_FRONT ? -1.0f : 1.0f;
    m_camera.frame (rig.elevation, rig.distance, rig.nearest);
    m_camera.aim (m_logic.m_look_yaw, raised - base);
    m_camera.update (moppe::position (rig.pivot),
                     rig.ahead * flip,
                     moppe::velocity (rig.velocity),
                     dt);
    m_camera.limit (surface, dt);
  }

  void GameSession::settle_camera (const map::SurfaceGeometry& surface) {
    m_logic.m_fp_eye = camera_rig ().eye;
    m_camera.reset ();
    follow_camera (surface, 0 * u::s);
  }

  void GameSession::clear_controls () {
    set_turn (*this, 0.0f);
    set_go (*this, 0.0f);
    set_boost (*this, 0.0f);
  }

  GameSession::State GameSession::state () const {
    return { m_logic,           m_bike.state (),   m_glider.state (),
             m_walker.state (), m_camera.state (), m_stars.state (),
             m_dust.state (),   m_jeep.state (),   m_helicopter.state (),
             m_tractor.state (), m_caddy.state (),
             m_mountain_bike.state (), m_boat.state () };
  }

  void GameSession::restore (const State& state) {
    m_logic = state.logic;
    m_bike.restore (state.vehicle);
    m_glider.restore (state.glider);
    m_walker.restore (state.walker);
    m_camera.restore (state.camera);
    m_stars.restore (state.stars);
    m_dust.restore (state.dust);
    m_jeep.restore (state.jeep);
    m_helicopter.restore (state.helicopter);
    m_tractor.restore (state.tractor);
    m_caddy.restore (state.caddy);
    m_mountain_bike.restore (state.mountain_bike);
    m_boat.restore (state.boat);
  }

  namespace {
    // The boat floats on whatever water lies under it and is carried by
    // its current; driven, its long-tail smokes, throws its rooster tail
    // of spray up behind, and the bow throws water aside at speed.
    void advance_boat (const WorldParams& world,
                       const map::SurfaceGeometry& surface,
                       GameSession& session,
                       const terrain::WaterSheets* water,
                       float elapsed) {
      GameLogicState& logic = session.logic ();
      mov::Boat& boat = session.boat ();
      const float sea = world.water_level.numerical_value_in (u::m);
      const mov::BoatWater floating {
        [&] (float x, float z) {
          return water_surface_at (world, surface, water, Vec3 (x, 0.0f, z))
            .numerical_value_in (u::m);
        },
        [&] (float x, float z) {
          if (!water)
            return Vec3 ();
          const position_t where = moppe::position (Vec3 (x, 0.0f, z));
          const float sheet = terrain::surface_elevation_value (
            spatial::sample<terrain::surface_elevation> (*water, where));
          if (sheet <= ground_at (surface, Vec3 (x, 0.0f, z)) ||
              sheet <= sea + 0.05f)
            return Vec3 ();
          Vec3 current = spatial::sample<terrain::water_velocity> (*water,
                                                                   where)
                           .numerical_value_in (u::m / u::s);
          current[1] = 0.0f;
          return current;
        }
      };
      boat.set_parked (logic.m_mode != M_BOAT);
      boat.update (elapsed, floating);
      if (logic.m_mode != M_BOAT)
        return;
      logic.m_odometer += length (boat.velocity ()) * elapsed;

      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const DisplayColor spray_color (0.86f, 0.92f, 0.97f);
      const Vec3 pivot (
        mov::Boat::pivot[0], mov::Boat::pivot[1], mov::Boat::pivot[2]);
      // Blue-grey two-stroke-ish smoke off the muffler, more under load.
      const Vec3 exhaust = boat.point (mov::Boat::tail_point (
        pivot + Vec3 (-0.25f, 0.30f, 0.05f), boat.swing (), boat.tilt ()));
      if (chance (logic.m_fx_rng) < 0.10f + 0.35f * boat.rpm ()) {
        const float shade = 0.55f + 0.2f * chance (logic.m_fx_rng);
        Dust::Style smoke;
        smoke.size = 0.28f * u::m;
        smoke.lifetime = 1.2f * u::s;
        smoke.downward_acceleration =
          -1.2f * isq::acceleration[u::m / pow<2> (u::s)];
        smoke.spread = 0.3f * one;
        session.dust ().emit (
          moppe::position (exhaust),
          velocity (boat.velocity () * 0.5f + Vec3 (0, 0.5f, 0)),
          1,
          DisplayColor (shade * 0.95f, shade, shade * 1.06f),
          smoke);
      }

      // The rooster tail: the propeller, running at the surface, throws a
      // plume of water up and back along the shaft.
      const Vec3 prop = boat.propeller ();
      const float bite = boat.propeller_depth ();
      if (boat.thrust () > 30.0f && bite > -0.05f && bite < 0.35f) {
        const Vec3 aft = normalized (prop - boat.point (pivot));
        Vec3 back = aft;
        back[1] = 0.0f;
        back = length2 (back) > 1e-6f ? normalized (back) : -boat.forward ();
        const float power = boat.thrust () / 720.0f;
        Dust::Style plume;
        plume.size = 0.12f * u::m;
        plume.lifetime = 1.0f * u::s;
        plume.downward_acceleration =
          9.0f * isq::acceleration[u::m / pow<2> (u::s)];
        plume.spread = 0.35f * one;
        plume.flake = true;
        const int drops = static_cast<int> (std::round (4.0f * power + 0.5f));
        session.dust ().emit (
          moppe::position (prop + Vec3 (0, 0.05f, 0)),
          velocity (boat.velocity () * 0.6f +
                    back * (3.0f + 4.0f * power) +
                    Vec3 (0, 2.5f + 3.5f * power, 0)),
          drops,
          spray_color,
          plume);
        if (chance (logic.m_fx_rng) < 0.5f * power) {
          Dust::Style mist;
          mist.size = 0.6f * u::m;
          mist.lifetime = 1.1f * u::s;
          mist.downward_acceleration =
            1.0f * isq::acceleration[u::m / pow<2> (u::s)];
          mist.spread = 0.9f * one;
          session.dust ().emit (
            moppe::position (prop + back * 0.8f + Vec3 (0, 0.6f, 0)),
            velocity (boat.velocity () * 0.5f + Vec3 (0, 0.8f, 0)),
            1,
            spray_color,
            mist);
        }
      }

      // The bow throws the water aside, a sheet either side of the stem
      // that runs aft as the boat lifts onto the plane.
      Vec3 way = boat.velocity ();
      way[1] = 0.0f;
      const float speed = length (way);
      if (boat.afloat () && speed > 2.5f) {
        const float planing = std::clamp ((speed - 4.0f) / 5.0f, 0.0f, 1.0f);
        Dust::Style sheet;
        sheet.size = 0.10f * u::m;
        sheet.lifetime = 0.7f * u::s;
        sheet.downward_acceleration =
          9.0f * isq::acceleration[u::m / pow<2> (u::s)];
        sheet.spread = 0.3f * one;
        sheet.flake = true;
        const int drops =
          static_cast<int> (std::round (std::min (3.0f, speed * 0.25f)));
        for (const float side : { 1.0f, -1.0f }) {
          const Vec3 at = boat.point (
            Vec3 (side * (0.45f + 0.25f * planing), 0.10f,
                  2.4f - 1.6f * planing));
          session.dust ().emit (
            moppe::position (at),
            velocity (boat.velocity () * 0.75f +
                      boat.left () * (side * (1.0f + 0.15f * speed)) +
                      Vec3 (0, 1.2f + 0.1f * speed, 0)),
            drops,
            spray_color,
            sheet);
        }
      }
      // Run up on the shore, the hull grinds over what is there.
      if (boat.grounded () && speed > 1.0f &&
          chance (logic.m_fx_rng) < 0.3f) {
        Vec3 under = boat.point (Vec3 (0, 0, 1.5f));
        under[1] = ground_at (surface, under) + 0.1f;
        session.dust ().emit (moppe::position (under),
                              velocity (boat.velocity () * 0.2f),
                              1,
                              DisplayColor (0.60f, 0.52f, 0.40f));
      }
    }
  }

  GameSessionAdvanceResult
  advance_game_session (const WorldParams& world,
                        const map::SurfaceGeometry& surface,
                        GameSession& session,
                        const InputFrame& input,
                        seconds_t dt,
                        const mov::TrunkField* trunks,
                        const GroundCover& ground,
                        const terrain::WaterSheets* water,
                        std::span<const MachineBody> strangers) {
    const float elapsed = dt.numerical_value_in (u::s);
    GameLogicState& logic = session.logic ();
    // Rivers and lakes hold back what goes through them as the sea does.
    const auto water_at = [&] (const Vec3& at) {
      return water_surface_at (world, surface, water, at);
    };
    session.bike ().set_water_level (water_at (session.bike ().position ()));
    session.bike ().set_trunks (trunks);
    mov::Vehicle& mountain_bike = session.mountain_bike ();
    mountain_bike.set_water_level (water_at (mountain_bike.position ()));
    mountain_bike.set_trunks (trunks);

    // Where one would step out of the boat.
    if (logic.m_mode == M_BOAT) {
      const mov::Boat& boat = session.boat ();
      Vec3 best;
      float shallowest = 1e30f;
      for (const Vec3& side : { Vec3 (1.4f, 0.3f, 0.4f),
                                Vec3 (-1.4f, 0.3f, 0.4f),
                                Vec3 (0.0f, 0.3f, mov::Boat::stem + 0.7f) }) {
        Vec3 at = boat.point (side);
        const float ground = ground_at (surface, at);
        const float depth =
          water_at (at).numerical_value_in (u::m) - ground;
        if (depth < shallowest) {
          shallowest = depth;
          at[1] = ground + 0.1f;
          best = at;
        }
      }
      session.set_boat_landing (best);
    }

    apply_input_frame (session, surface, input);
    tend_haul (session, elapsed);
    tend_sling (session, surface, elapsed);
    // What is on the rope rolls free after the walker.
    const auto hauled = [&] (Machine machine) {
      return session.hauling () == machine;
    };
    // Only a ridden bike moves under its own physics; one left standing is
    // parked until someone mounts it again.
    session.bike ().set_parked (logic.m_mode != M_BIKE &&
                                !session.can_drop_bike () &&
                                !hauled (Machine::bike));

    if (flies_bike (session)) {
      if (fly_bike (session, dt)) {
        session.glider ().land ();
        finish_glide (session);
      }
    } else {
      if (!session.can_drop_bike ())
        session.bike ().update (dt);
      if (logic.m_mode == M_GLIDER) {
        const bool landed = session.glider ().update (dt);
        if (session.glider ().bike_attached ())
          sync_attached_bike (session);
        if (landed)
          finish_glide (session);
      }
    }
    // The mountainbike stands where it was left too.
    mountain_bike.set_parked (logic.m_mode != M_MOUNTAIN_BIKE &&
                              !hauled (Machine::mountain_bike));
    mountain_bike.update (dt);
    if (logic.m_mode == M_FOOT)
      session.walker ().update (dt, surface, world, trunks, water);
    // The jeep stands braked wherever its driver left it.
    mov::Jeep& jeep = session.jeep ();
    jeep.set_trunks (trunks);
    jeep.set_parked (logic.m_mode != M_JEEP && !hauled (Machine::jeep));
    jeep.update (elapsed,
                 water_at (jeep.position ()).numerical_value_in (u::m));
    // The helicopter's rotor runs down once its pilot has stepped out.
    mov::Helicopter& heli = session.helicopter ();
    heli.set_trunks (trunks);
    heli.set_parked (logic.m_mode != M_HELICOPTER);
    heli.update (elapsed,
                 water_at (heli.position ()).numerical_value_in (u::m));
    if (logic.m_mode == M_HELICOPTER) {
      logic.m_odometer += length (heli.velocity ()) * elapsed;
      // Low over the ground, the downwash blows up what lies there.
      const float height = heli.altitude ();
      if (heli.rotor () > 0.6f && height < 9.0f) {
        std::uniform_real_distribution<float> chance (0.0f, 1.0f);
        const float strength = heli.rotor () * (1.0f - height / 9.0f);
        if (chance (logic.m_fx_rng) < strength * 0.8f) {
          const float around = 6.2831853f * chance (logic.m_fx_rng);
          const Vec3 out (std::cos (around), 0, std::sin (around));
          Vec3 under =
            heli.position () + out * (2.0f + 2.0f * chance (logic.m_fx_rng));
          under[1] -= height - 0.2f;
          const bool wet =
            under[1] < water_at (under).numerical_value_in (u::m) + 0.3f;
          session.dust ().emit (
            moppe::position (under),
            velocity (out * (7.0f + 5.0f * strength) + Vec3 (0, 1.2f, 0)),
            wet ? 2
                : std::max (1,
                            static_cast<int> (std::round (2.0f * ground.dust))),
            wet ? DisplayColor (0.85f, 0.92f, 1.0f) : ground.dust_color);
        }
      }
    }
    // The tractor too, and it smokes while someone sits at its wheel:
    // a puff from the stack now and then at idle, a stream under load.
    mov::Tractor& tractor = session.tractor ();
    tractor.set_trunks (trunks);
    tractor.set_parked (logic.m_mode != M_TRACTOR &&
                        !hauled (Machine::tractor));
    tractor.update (elapsed,
                    water_at (tractor.position ()).numerical_value_in (u::m));
    if (logic.m_mode == M_TRACTOR) {
      logic.m_odometer += length (tractor.velocity ()) * elapsed;
      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const float throttle = tractor.throttle ();
      if (chance (logic.m_fx_rng) < 0.12f + 0.5f * throttle) {
        const float shade = 0.30f + 0.25f * chance (logic.m_fx_rng);
        session.dust ().emit (
          moppe::position (tractor.exhaust ()),
          velocity (tractor.velocity () * 0.6f +
                    tractor.up () * (1.2f + 2.0f * throttle)),
          1,
          DisplayColor (shade, shade, shade * 1.02f));
      }
      // Spinning or sliding wheels throw up what they turn on.
      const float slide =
        std::fabs (dot (tractor.velocity (), tractor.left ()));
      if (tractor.grounded () && slide > 2.5f)
        session.dust ().emit (
          moppe::position (tractor.point (Vec3 (0, 0.1f, -0.8f))),
          velocity (tractor.velocity () * 0.15f),
          static_cast<int> (
            std::round (std::min (3.0f, slide * 0.3f) * ground.dust)),
          ground.dust_color);
    }
    // The Caddy as the jeep, with a breath of exhaust from under its
    // tail while it is driven: more as it pulls.
    mov::Caddy& caddy = session.caddy ();
    caddy.set_trunks (trunks);
    caddy.set_parked (logic.m_mode != M_CADDY && !hauled (Machine::caddy) &&
                      !logic.m_sling.hooked);
    caddy.set_slung (logic.m_sling.hooked);
    caddy.update (elapsed,
                  water_at (caddy.position ()).numerical_value_in (u::m));
    if (logic.m_mode == M_CADDY) {
      logic.m_odometer += length (caddy.velocity ()) * elapsed;
      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const float throttle = caddy.throttle ();
      if (chance (logic.m_fx_rng) < 0.05f + 0.25f * throttle) {
        const float shade = 0.62f + 0.2f * chance (logic.m_fx_rng);
        session.dust ().emit (
          moppe::position (caddy.point (Vec3 (-0.45f, 0.05f, -2.33f))),
          velocity (caddy.velocity () * 0.5f - caddy.forward () * 0.8f +
                    Vec3 (0, 0.4f, 0)),
          1,
          DisplayColor (shade, shade, shade * 1.03f));
      }
      const float slide = std::fabs (dot (caddy.velocity (), caddy.left ()));
      if (caddy.grounded () && slide > 3.0f)
        session.dust ().emit (
          moppe::position (caddy.point (Vec3 (0, -0.2f, -1.3f))),
          velocity (caddy.velocity () * 0.15f),
          static_cast<int> (
            std::round (std::min (3.0f, slide * 0.25f) * ground.dust)),
          ground.dust_color);
    }
    session.boat ().set_trunks (trunks);
    advance_boat (world, surface, session, water, elapsed);
    if (logic.m_mode == M_JEEP) {
      logic.m_odometer += length (jeep.velocity ()) * elapsed;
      // Sliding wheels throw up what they slide on.
      const float slide = std::fabs (dot (jeep.velocity (), jeep.left ()));
      if (jeep.grounded () && slide > 3.0f)
        session.dust ().emit (
          moppe::position (jeep.point (Vec3 (0, -0.3f, -1.2f))),
          velocity (jeep.velocity () * 0.15f),
          static_cast<int> (
            std::round (std::min (3.0f, slide * 0.25f) * ground.dust)),
          ground.dust_color);
    }

    strain_machines (session, elapsed, ground);
    bump_machines (session, strangers, ground);

    const Vec3 vehicle_position = session.subject_position ();
    // What follows is the ridden bike's: the motocross, or the pedalled
    // mountainbike, which has no engine to roost, smoke, or burn.
    mov::Vehicle& vehicle = session.ridden_bike ();
    const bool pedalled = logic.m_mode == M_MOUNTAIN_BIKE;

    // A parked bike's impacts shouldn't linger until remount.
    for (mov::Vehicle* bike : { &session.bike (), &mountain_bike })
      if (bike != &vehicle || !(logic.m_mode == M_BIKE || pedalled)) {
        bike->pop_impact ();
        bike->pop_fall_drop ();
      }

    const bool in_water =
      vehicle_position[1] <
      water_at (vehicle_position).numerical_value_in (moppe::u::m) + 1.0f;
    const bool driving = logic.m_mode == M_BIKE || pedalled;
    const float impact = driving ? vehicle.pop_impact () : 0.0f;

    // Air steering is a visible motocross whip and a scoring mechanic. The
    // shortest signed angle between successive headings survives wraparound.
    // Returning the bike to line unwinds the live angle, but the largest
    // committed angle stays banked so a proper whip-and-recover earns credit.
    bool clean_stunt_landing = false;
    int landed_trick_quarters = 0;
    if (driving && vehicle.airtime () > 0.0f) {
      Vec3 heading = vehicle.orientation ();
      heading[1] = 0.0f;
      if (length2 (heading) > 0.0001f)
        normalize (heading);
      else
        heading = logic.m_jump_last_heading;

      if (logic.m_jump_airtime > 0.0f) {
        Vec3 previous = logic.m_jump_last_heading;
        previous[1] = 0.0f;
        if (length2 (previous) > 0.0001f) {
          normalize (previous);
          const float sine = cross (previous, heading)[1];
          const float cosine =
            std::clamp (dot (previous, heading), -1.0f, 1.0f);
          logic.m_jump_spin_radians += std::atan2 (sine, cosine);
          logic.m_jump_peak_spin_radians =
            std::max (logic.m_jump_peak_spin_radians,
                      std::abs (logic.m_jump_spin_radians));
        }
      } else {
        logic.m_jump_spin_radians = 0.0f;
        logic.m_jump_peak_spin_radians = 0.0f;
      }
      logic.m_jump_last_heading = heading;
      logic.m_jump_airtime = vehicle.airtime ();
      logic.m_landed_age += elapsed;
    } else {
      if (driving && logic.m_jump_airtime >= 0.75f) {
        constexpr float pi = 3.14159265f;
        const float spin_degrees = logic.m_jump_peak_spin_radians * 180.0f / pi;
        landed_trick_quarters =
          (int)std::floor ((spin_degrees + 10.0f) / 90.0f);

        Vec3 horizontal_velocity = vehicle.velocity ();
        horizontal_velocity[1] = 0.0f;
        float alignment = 1.0f;
        if (length2 (horizontal_velocity) > 1.0f) {
          normalize (horizontal_velocity);
          alignment =
            std::abs (dot (horizontal_velocity, vehicle.orientation ()));
        }
        clean_stunt_landing = impact < 7.0f && alignment > 0.82f;

        const float air_points =
          60.0f * logic.m_jump_airtime * logic.m_jump_airtime;
        const float trick_points =
          125.0f * landed_trick_quarters * landed_trick_quarters;
        const float landing_multiplier = clean_stunt_landing ? 1.5f : 1.0f;
        logic.m_landed_airtime = logic.m_jump_airtime;
        logic.m_landed_spin_degrees = spin_degrees;
        logic.m_landed_clean = clean_stunt_landing;
        logic.m_landed_points =
          (int)std::round ((air_points + trick_points) * landing_multiplier);
        logic.m_score += logic.m_landed_points;
        logic.m_landed_age = 0.0f;

        // A clean stunt keeps the ride flowing: the better the whip, the more
        // jump-jet reserve comes back for the next launch.
        if (clean_stunt_landing)
          vehicle.replenish_boost (
            std::min (0.35f, 0.10f + 0.04f * landed_trick_quarters));
      }
      logic.m_jump_airtime = 0.0f;
      logic.m_jump_spin_radians = 0.0f;
      logic.m_jump_peak_spin_radians = 0.0f;
      logic.m_landed_age += elapsed;
    }
    const DisplayColor dust_color = ground.dust_color;
    const DisplayColor clod_color = ground.clod_color;
    // Loose ground gives up its dust; turf and litter hold most of theirs.
    const auto loose = [&ground] (float count) {
      return static_cast<int> (std::round (count * ground.dust));
    };
    const DisplayColor spray_color (0.85f, 0.92f, 1.0f);
    const Vec3 forward = session.subject_heading ();
    const Vec3 rear_wheel =
      pedalled ? vehicle_position - forward * 0.55f + Vec3 (0, -0.25f, 0)
               : vehicle_position - forward * 1.4f + Vec3 (0, -0.7f, 0);

    if (clean_stunt_landing && landed_trick_quarters > 0) {
      Dust::Style trick_flash;
      trick_flash.size = 0.22f * u::m;
      trick_flash.lifetime = 0.65f * u::s;
      trick_flash.downward_acceleration =
        5.0f * isq::acceleration[u::m / pow<2> (u::s)];
      trick_flash.spread = 1.2f * one;
      trick_flash.additive = true;
      session.dust ().emit (
        moppe::position (vehicle_position + Vec3 (0, -0.4f, 0)),
        velocity (vehicle.velocity () * 0.15f + Vec3 (0, 3, 0)),
        std::min (24, 8 + landed_trick_quarters * 4),
        DisplayColor (0.25f, 0.78f, 1.0f),
        trick_flash);
    }

    // Drift kicks up dirt from the rear wheel (or spray), and so does a
    // tyre sliding under the locked rear brake, in a low fan swept out
    // sideways as the tail comes round.
    if (driving && vehicle.grounded () && vehicle.drift_speed () > 6.0f) {
      const float count = std::min (4.0f, vehicle.drift_speed () * 0.2f);
      session.dust ().emit (moppe::position (rear_wheel),
                            velocity (vehicle.velocity () * 0.15f),
                            in_water ? (int)count : loose (count),
                            in_water ? spray_color : dust_color);
    }
    const float sliding = length (vehicle.velocity ());
    if (driving && vehicle.grounded () && vehicle.skid () > 0.3f &&
        sliding > 1.5f) {
      const float count =
        std::min (3.0f, sliding * (pedalled ? 0.25f : 0.15f)) * vehicle.skid ();
      Dust::Style spray;
      spray.size = (pedalled ? 0.22f : 0.35f) * u::m;
      spray.lifetime = 0.8f * u::s;
      spray.downward_acceleration =
        2.0f * isq::acceleration[u::m / pow<2> (u::s)];
      spray.spread = 0.8f * one;
      session.dust ().emit (
        moppe::position (rear_wheel),
        velocity (vehicle.velocity () * 0.4f + Vec3 (0, 0.6f, 0)),
        std::max (1, in_water ? (int)std::round (count) : loose (count)),
        in_water ? spray_color : dust_color,
        spray);
    }

    // Fallen leaves fly up behind the rear wheel and flutter back down.
    if (driving && vehicle.grounded () && !in_water && ground.leaves > 0.15f) {
      const float speed = length (vehicle.velocity ());
      const int count =
        static_cast<int> (std::min (3.0f, ground.leaves * speed * 0.08f));
      if (count > 0) {
        Dust::Style leaf;
        leaf.size = 0.08f * u::m;
        leaf.lifetime = 1.8f * u::s;
        leaf.downward_acceleration =
          1.6f * isq::acceleration[u::m / pow<2> (u::s)];
        leaf.spread = 1.1f * one;
        leaf.flake = true;
        const Vec3 lift = vehicle.velocity () * 0.25f + Vec3 (0, 2.4f, 0);
        session.dust ().emit (moppe::position (rear_wheel),
                              velocity (lift),
                              (count + 1) / 2,
                              DisplayColor (0.93f, 0.67f, 0.20f),
                              leaf);
        session.dust ().emit (moppe::position (rear_wheel),
                              velocity (lift),
                              count / 2,
                              DisplayColor (0.86f, 0.44f, 0.14f),
                              leaf);
      }
    }

    // Roost: hard throttle sprays an arc of dirt clods backward off the rear
    // knobby, heaviest when the engine wins against the ground.
    if (driving && !pedalled && vehicle.grounded () && !in_water &&
        vehicle.thrust () > 0.6f) {
      const float speed = length (vehicle.velocity ());
      const float slip = scalar_value (vehicle.thrust ()) *
                         (1.0f - std::min (1.0f, speed / 30.0f));
      if (slip > 0.15f) {
        Dust::Style roost;
        roost.size = 0.16f * u::m;
        roost.flake = true;
        roost.lifetime = 0.9f * u::s;
        roost.downward_acceleration =
          12.0f * isq::acceleration[u::m / pow<2> (u::s)];
        roost.spread = 0.5f * one;
        session.dust ().emit (moppe::position (rear_wheel),
                              velocity (forward * (-6.0f - 14.0f * slip) +
                                        Vec3 (0, 3.5f + 3.0f * slip, 0)),
                              1 + (int)(slip * 3.0f),
                              clod_color,
                              roost);
      }
    }

    // While the jets burn: hot additive embers shot out along them, a
    // thin smoke trailing off their ends, and, close over the ground, a
    // blast of whatever the ground is made of. The nozzles swing from
    // straight down at rest to back and down under full drive.
    if (driving && vehicle.boost_level () > 0.05f) {
      const float level = vehicle.boost_level ();
      const float swing = 1.047f * vehicle.boost_drive ();
      const Vec3 jet = normalized (Vec3 (0, -std::cos (swing), 0) -
                                   forward * std::sin (swing));
      const Vec3 nozzles =
        vehicle_position - forward * 0.3f + Vec3 (0, -0.45f, 0);
      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const auto happens = [&] (float per_second) {
        const probability_t odds (per_second / u::s * (elapsed * u::s));
        return chance (logic.m_fx_rng) < scalar_value (odds);
      };
      if (happens (90.0f * level)) {
        Dust::Style ember;
        ember.size = 0.12f * u::m;
        ember.lifetime = 0.40f * u::s;
        ember.downward_acceleration =
          6.0f * isq::acceleration[u::m / pow<2> (u::s)];
        ember.spread = 0.35f * one;
        ember.additive = true;
        session.dust ().emit (
          moppe::position (nozzles + jet * 0.4f),
          velocity (vehicle.velocity () * 0.6f + jet * 16.0f),
          2,
          chance (logic.m_fx_rng) < 0.5f ? DisplayColor (1.0f, 0.62f, 0.20f)
                                         : DisplayColor (1.0f, 0.86f, 0.50f),
          ember);
      }
      if (happens (22.0f * level)) {
        Dust::Style smoke;
        smoke.size = 0.55f * u::m;
        smoke.lifetime = 1.1f * u::s;
        smoke.downward_acceleration =
          -1.5f * isq::acceleration[u::m / pow<2> (u::s)];
        smoke.spread = 0.45f * one;
        session.dust ().emit (
          moppe::position (nozzles + jet * (2.0f + 2.5f * level)),
          velocity (vehicle.velocity () * 0.3f + jet * 3.0f),
          1,
          DisplayColor (0.62f, 0.60f, 0.58f),
          smoke);
      }
      if (vehicle.grounded () && jet[1] < -0.5f && happens (40.0f * level)) {
        Dust::Style blast;
        blast.size = 0.30f * u::m;
        blast.lifetime = 0.9f * u::s;
        blast.downward_acceleration =
          3.0f * isq::acceleration[u::m / pow<2> (u::s)];
        blast.spread = 1.4f * one;
        const float around = 6.2831853f * chance (logic.m_fx_rng);
        const Vec3 out (std::cos (around), 0, std::sin (around));
        session.dust ().emit (moppe::position (nozzles + Vec3 (0, -0.4f, 0)),
                              velocity (out * 7.0f + Vec3 (0, 1.5f, 0)),
                              in_water ? 3 : std::max (1, loose (3.0f)),
                              in_water ? spray_color : dust_color);
      }
    }

    // Exhaust smoke: faint gray puffs rise off the muffler while the
    // throttle is open.
    if (driving && !pedalled && abs (vehicle.thrust ()) > 0.3f) {
      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const probability_t puff (14.0f / u::s * (elapsed * u::s));
      if (chance (logic.m_fx_rng) < scalar_value (puff)) {
        Dust::Style smoke;
        smoke.size = 0.35f * u::m;
        smoke.lifetime = 0.8f * u::s;
        smoke.downward_acceleration =
          -2.5f * isq::acceleration[u::m / pow<2> (u::s)];
        smoke.spread = 0.25f * one;
        session.dust ().emit (moppe::position (vehicle_position -
                                               forward * 1.2f +
                                               Vec3 (0, -0.4f, 0)),
                              velocity (vehicle.velocity () * 0.25f),
                              1,
                              DisplayColor (0.45f, 0.45f, 0.48f),
                              smoke);
      }
    }

    // Wading fast throws up a bow wave.
    if (driving && in_water && length (vehicle.velocity ()) > 15.0f)
      session.dust ().emit (
        moppe::position (vehicle_position + Vec3 (0, -0.5f, 0)),
        velocity (vehicle.velocity () * 0.3f),
        3,
        spray_color);

    // Hard landings shake the camera and burst dirt outward: a low pancake of
    // dust plus a ring of ballistic clods.
    if (impact > 8.0f) {
      logic.m_shake = std::min (0.28f, 0.010f * impact);
      logic.m_shake_time = 0.0f;
      session.dust ().emit (
        moppe::position (vehicle_position + Vec3 (0, -0.7f, 0)),
        velocity (vehicle.velocity () * 0.2f),
        in_water ? 12 : loose (12.0f),
        in_water ? spray_color : dust_color);
      if (!in_water) {
        Dust::Style burst;
        burst.size = 0.2f * u::m;
        burst.flake = true;
        burst.lifetime = 1.1f * u::s;
        burst.downward_acceleration =
          10.0f * isq::acceleration[u::m / pow<2> (u::s)];
        burst.spread = 1.4f * one;
        session.dust ().emit (
          moppe::position (vehicle_position + Vec3 (0, -0.6f, 0)),
          velocity (vehicle.velocity () * 0.15f +
                    Vec3 (0, 2.0f + 0.15f * impact, 0)),
          (int)std::min (10.0f, impact * 0.5f),
          clod_color,
          burst);
      }
    }

    GameSessionAdvanceResult result;
    // Crashes hurt; health trickles back slowly. Falls from above a hundred
    // metres are simply fatal -- house rule.
    if (impact > 9.0f)
      logic.m_health -= (impact - 9.0f) * 4.5f;
    if (driving && vehicle.pop_fall_drop () > 100.0f)
      logic.m_health = 0.0f;
    logic.m_health = std::min (100.0f, logic.m_health + 1.5f * elapsed);
    if (logic.m_health <= 0.0f) {
      session.dust ().emit (moppe::position (vehicle_position),
                            velocity (Vec3 (0, 6, 0)),
                            40,
                            DisplayColor (1.0f, 0.5f, 0.1f));
      --logic.m_lives;
      if (logic.m_lives <= 0) {
        logic.m_game_over = true;
      } else {
        // Halfway through the hearts, the game offers its sympathies out
        // loud. The app realizes this platform effect after the advance.
        result.say_ouchies = logic.m_lives == 5;

        // Respawn where you crashed, upright on the ground.
        const float ground = terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            surface,
            moppe::position (
              Vec3 (vehicle_position[0], 0.0f, vehicle_position[2]))));
        vehicle.reset (Vec3 (vehicle_position[0],
                             ground + (pedalled ? 0.5f : 1.2f),
                             vehicle_position[2]));
        logic.m_health = 100.0f;
        logic.m_shake = 1.0f;
        logic.m_shake_time = 0.0f;
      }
    }

    // Star pickups sparkle gold and top up the boost reserve.
    {
      const int picked =
        session.stars ().update (vehicle_position, logic.m_total_time, elapsed);
      if (picked > 0) {
        Dust::Style sparkle;
        sparkle.size = 0.38f * u::m;
        sparkle.lifetime = 0.85f * u::s;
        sparkle.downward_acceleration =
          -1.5f * isq::acceleration[u::m / pow<2> (u::s)];
        sparkle.spread = 1.7f * one;
        sparkle.additive = true;
        session.dust ().emit (moppe::position (session.stars ().last_pos ()),
                              velocity (Vec3 (0, 4, 0)),
                              32,
                              DisplayColor (1.0f, 0.72f, 0.12f),
                              sparkle);
        Dust::Style flash;
        flash.size = 0.9f * u::m;
        flash.lifetime = 0.35f * u::s;
        flash.spread = 0.25f * one;
        flash.additive = true;
        session.dust ().emit (moppe::position (session.stars ().last_pos ()),
                              velocity (Vec3 ()),
                              5,
                              DisplayColor (1.0f, 0.95f, 0.55f),
                              flash);
        if (logic.m_mode != M_GLIDER)
          vehicle.replenish_boost (0.25f * picked);
      }
    }

    if (driving)
      logic.m_odometer += length (vehicle.velocity ()) * elapsed;

    session.dust ().update (dt);
    logic.m_shake_time += elapsed;
    logic.m_shake *= decay (7.0f / u::s, elapsed * u::s);

    settle_look (logic, length (session.camera_rig ().velocity), elapsed);
    session.follow_camera (surface, dt);

    // Speed widens the field of view a touch.
    const float kmh =
      logic.m_mode != M_FOOT ? session.subject_speed_kmh () : 0.0f;
    const float fov_target =
      std::min (1.0f, std::max (0.0f, (kmh - 70.0f) / 180.0f));
    logic.m_fov_k += (fov_target - logic.m_fov_k) *
                     smoothing_alpha (5.0f / u::s, elapsed * u::s);

    return result;
  }
}
