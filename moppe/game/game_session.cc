#include <moppe/game/affordances.hh>
#include <moppe/game/game_session.hh>

#include <moppe/game/bus.hh>
#include <moppe/game/surroundings.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/mov/box3d_vec.hh>
#include <moppe/terrain/roads.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace moppe::game {
  namespace {
    // The bike under the wing flies: its own world moves it, wheels and
    // all, with the wing's pull on it, and the wing rides where the bike
    // takes it.
    bool flies_bike (const GameSession& session) {
      return session.can_drop_bike ();
    }

    position_t wing_over_bike (const mov::Vehicle& bike) {
      return moppe::position (bike.position () +
                              bike.render_normal () *
                                meters_value (mov::Glider::bike_hang));
    }

    // True once a wheel is down.
    bool fly_bike (GameSession& session, seconds_t dt) {
      mov::Vehicle& bike = session.bike ();
      mov::Glider& glider = session.glider ();
      const acceleration_t pull =
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

    // Hands the controls to whatever the player is getting about on.
    void steer (GameSession& session, Controls k) {
      GameLogicState& logic = session.logic ();
      k.boost = std::clamp (k.boost, 0.0f, 1.0f);
      const bool pressed = k.boost > 0.1f && logic.m_controls.boost <= 0.1f;
      logic.m_controls = k;
      switch (logic.m_doing.way) {
      case Doing::Way::foot: {
        // On foot the mouse turns; the steering keys step sideways.
        session.walker ().set_strafe (k.turn);
        // Hauling on a rope is slow work.
        const float burden = logic.m_haul.active ? 0.6f : 1.0f;
        session.walker ().set_walk (burden * (k.go > 0 ? k.go : k.go * 0.6f));
        if (pressed)
          session.walker ().jump ();
        break;
      }
      case Doing::Way::glider:
        session.glider ().set_turn (k.turn);
        session.glider ().set_speed_control (k.go);
        session.glider ().set_flare (k.boost > 0.1f);
        break;
      case Doing::Way::passenger:
      case Doing::Way::bus:
        break;
      case Doing::Way::driving:
        drive (session.fleet (), logic.m_doing.machine, k, pressed);
        break;
      }
    }

    void finish_glide (GameSession& session) {
      GameLogicState& logic = session.logic ();
      if (session.glider ().bike_attached ()) {
        // The bike rides on with everything the glide gave it.
        session.glider ().drop_bike ();
        session.set_mode (Doing::driving (Machine::bike));
        Controls k = logic.m_controls;
        k.boost = 0.0f;
        steer (session, k);
        return;
      }

      // The pilot's feet swing down out of the harness and run the
      // landing out.
      const mov::Glider& glider = session.glider ();
      const Vec3 feet =
        glider.position () -
        Vec3 (0, meters_value (mov::Glider::pilot_clearance) - 0.1f, 0);
      session.walker ().spawn (moppe::position (feet),
                               unit_direction (glider.heading ()),
                               glider.physical_velocity ());
      session.set_mode (Doing::on_foot ());
      Controls k = logic.m_controls;
      k.boost = 0.0f;
      steer (session, k);
    }

    Trouble& trouble_of (GameLogicState& logic, Machine machine) {
      return logic.m_trouble[static_cast<std::size_t> (machine)];
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

    // Where the driver sits, in the world: a bike's rider over its
    // reference point.
    Vec3 seat_of (const mov::Vehicle& bike) {
      return bike.position ();
    }
    Vec3 seat_of (const mov::Car& car) {
      return car.driver_seat ();
    }
    Vec3 seat_of (const mov::Tractor& tractor) {
      return tractor.driver_seat ();
    }
    Vec3 seat_of (const mov::Helicopter& heli) {
      return heli.pilot_seat ();
    }
    Vec3 seat_of (const mov::Boat& boat) {
      return boat.helm_seat ();
    }
    Vec3 seat_of (const mov::JetSki& ski) {
      return ski.seat ();
    }

    float ground_at (const map::SurfaceGeometry& surface, const Vec3& at) {
      return terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (at[0], 0.0f, at[2]))));
    }

    // The line's pull between the helicopter and what hangs on it, each
    // taking its share.
    void tend_sling (GameSession& session, seconds_t dt) {
      GameLogicState& logic = session.logic ();
      Sling& sling = logic.m_sling;
      mov::Helicopter& heli = session.helicopter ();
      if (sling.hooked && logic.m_doing != Doing::driving (Machine::helicopter))
        sling.hooked = false;
      if (!sling.hooked) {
        heli.set_load (force (Vec3 ()), meters_t {});
        sling.stretch = {};
        return;
      }
      const Vec3 hook = heli.point (sling_hook ());
      const Vec3 ring = session.machine_body (sling.load)
                          .point (machine_kind (sling.load).ring);
      const force_t pull = sling_pull (sling, hook, ring, dt);
      if (length2 (force_value (pull)) > 0.0f) {
        session.push_machine (
          sling.load, ring, quantity_cast<isq::impulse> (pull * dt));
        heli.push (hook, quantity_cast<isq::impulse> (-pull * dt));
      }
      heli.set_load (-pull, sling_hang);
    }

    // Riding, driving, or gliding, a look the player has let go of finds
    // its way back to the way ahead: soon after a glance, later after a
    // long look aside, and gently, gathering pace as it comes round. A
    // machine at rest leaves the view where it was put, there being no
    // way ahead to watch. On foot the walker turns with the look, so it
    // is always ahead.
    void settle_look (GameLogicState& logic, float speed, float elapsed) {
      logic.m_look_idle += elapsed;
      if (logic.m_doing == Doing::on_foot ())
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

    // A cart road helps whoever drives along it. On its tread the steering
    // is eased toward the middle of the road a little way ahead, so it
    // follows the bends: all the way while the player leaves the bars
    // alone, topping up a turn the player makes the same way, and giving
    // way to one made against it. Wandering off the tread, it steers back
    // a little way past the shoulder before it lets go. And it eases off
    // the throttle before a bend tighter than the machine can take at its
    // speed. Crossing the road, crawling, in the air, or backing up, it
    // lets be.
    struct RoadHelp {
      float turn = 0.0f;
      float drive = 0.0f;
    };

    RoadHelp road_help (const GameSession& session,
                        const map::SurfaceGeometry& terrain,
                        float turn,
                        float drive) {
      const RoadHelp none { turn, drive };
      const Doing mode = session.logic ().m_doing;
      const bool bike =
        mode == Doing::driving (Machine::bike) || pedalled (mode);
      const terrain::RoadSurface* roads = mov::attached_roads (terrain);
      if (!roads || drive < 0.0f ||
          !(bike || mode == Doing::driving (Machine::jeep) ||
            mode == Doing::driving (Machine::tractor) ||
            mode == Doing::driving (Machine::caddy)))
        return none;
      if (bike && !session.ridden_bike ().grounded ())
        return none;
      const Vec3 at = session.subject_position ();
      const terrain::RoadSample road = roads->nearest (at[0], at[2]);
      if (!road.found || road.kind != terrain::Road::Kind::Road)
        return none;
      Vec3 ahead = session.subject_heading ();
      ahead[1] = 0.0f;
      if (length2 (ahead) < 1e-6f)
        return none;
      ahead = normalized (ahead);
      const Vec3 right = normalized (cross (ahead, Vec3 (0.0f, 1.0f, 0.0f)));
      Vec3 way (road.direction_x, 0.0f, road.direction_z);
      if (dot (way, ahead) < 0.0f)
        way = -way;
      const float askew = std::atan2 (dot (way, right), dot (way, ahead));
      const terrain::RoadFormation& formation = roads->formation ();
      const float tread = formation.half_width + formation.shoulder;
      const float reach =
        1.0f - smoothstep (tread + 1.0f, tread + 3.0f, std::abs (road.across));
      const float along = 1.0f - smoothstep (0.4f, 0.7f, std::abs (askew));
      const float kmh = session.subject_speed_kmh ();
      const float moving = smoothstep (8.0f, 18.0f, kmh);
      const float help = reach * along * moving;
      if (help <= 0.0f)
        return none;
      const float speed = kmh / 3.6f;

      // Walks the road's middle on from here, a few metres a step, and
      // calls `visit` with the distance and the way at each step.
      const auto walk = [&] (float far, float step, auto&& visit) {
        Vec3 p =
          at - Vec3 (-road.direction_z, 0.0f, road.direction_x) * road.across;
        Vec3 w = way;
        for (float d = step; d <= far; d += step) {
          const Vec3 q = p + w * step;
          const terrain::RoadSample s = roads->nearest (q[0], q[2]);
          if (!s.found || s.kind != terrain::Road::Kind::Road)
            return;
          Vec3 next (s.direction_x, 0.0f, s.direction_z);
          if (dot (next, w) < 0.0f)
            next = -next;
          p = q - Vec3 (-s.direction_z, 0.0f, s.direction_x) * s.across;
          w = next;
          visit (d, p, w);
        }
      };

      // The middle of the road some way on, steered for.
      const float lookahead = std::max (6.0f, 0.7f * speed);
      Vec3 aim = at + way * lookahead;
      walk (lookahead,
            lookahead / 3.0f,
            [&] (float, const Vec3& p, const Vec3&) { aim = p; });
      Vec3 to = aim - at;
      to[1] = 0.0f;
      const float error = std::atan2 (dot (to, right), dot (to, ahead));
      const float want = std::clamp (2.5f * error, -0.5f, 0.5f) * help;
      float steer = turn;
      if (want * turn >= 0.0f)
        // The same way, or the bars let be: at least as far as it wants.
        steer = std::abs (want) > std::abs (turn) ? want : turn;
      else
        steer = turn + want * (1.0f - std::min (1.0f, std::abs (turn) / 0.4f));

      // How fast the bends ahead may be taken: a machine corners at about
      // `grip` across (m/s²), and slows by about `slowing` with the
      // throttle eased, so each bend asks for a speed some way before it.
      // Over that the throttle eases off, and well over it the brakes go
      // on gently.
      constexpr float grip = 2.8f, slowing = 3.0f, step = 3.0f;
      float allowed = 1e9f;
      Vec3 last_way = way;
      float last_d = 0.0f;
      walk (std::max (24.0f, speed * speed / (2.0f * slowing) + 12.0f),
            step,
            [&] (float d, const Vec3&, const Vec3& w) {
              if (d - last_d >= 9.0f - 1e-3f) {
                const float bend =
                  std::acos (std::clamp (dot (w, last_way), -1.0f, 1.0f));
                const float curvature = bend / (d - last_d);
                if (curvature > 1e-4f) {
                  const float corner = std::sqrt (grip / curvature);
                  const float before = std::max (0.0f, d - 9.0f);
                  allowed = std::min (
                    allowed,
                    std::sqrt (corner * corner + 2.0f * slowing * before));
                }
                last_way = w;
                last_d = d;
              }
            });
      const float over = speed - allowed;
      const float eased =
        drive * (1.0f - smoothstep (0.0f, 3.0f, over) * help) -
        0.4f * smoothstep (3.0f, 8.0f, over) * help;
      return { steer, std::min (drive, eased) };
    }

    void apply_input_frame (GameSession& session,
                            const map::SurfaceGeometry& terrain,
                            const InputFrame& input) {
      const RoadHelp help = road_help (
        session, terrain, input_value (input.turn), input_value (input.drive));
      // The helicopter climbs on what boosts the bike and descends on
      // what runs on foot (or the held deploy key, a controller's A).
      const float climb = input_value (input.boost);
      const bool descend = input.run || input.use_held;
      steer (session,
             { .turn = help.turn,
               .go = help.drive,
               .boost = input_value (input.boost),
               .lift = climb > 0.1f ? climb
                       : descend    ? -1.0f
                                    : 0.0f,
               // Riding, what runs on foot stamps on the rear brake.
               .skid = input.run,
               .wheelie = input.lean_back });
      GameLogicState& logic = session.logic ();
      logic.m_look_pitch =
        std::clamp (logic.m_look_pitch + input.look_pitch, -1.35f, 1.35f);
      if (input.look_yaw != 0.0f || input.look_pitch != 0.0f)
        logic.m_look_idle = 0.0f;
      if (logic.m_doing == Doing::on_foot ()) {
        // The walker turns with the look, and a following camera turns
        // with them at once rather than catching up.
        session.walker ().turn_by (input.look_yaw * u::rad);
        session.walker ().set_run (input.run && !logic.m_haul.active);
        if (logic.m_cam_mode != CAM_HELMET)
          session.camera ().turn (input.look_yaw, 0.0f);
        logic.m_look_yaw = 0.0f;
      } else {
        // Riding, the look swings about the way ahead, all the way round.
        logic.m_look_yaw =
          std::remainder (logic.m_look_yaw + input.look_yaw, 6.2831853f);
      }

      InputFrame presses = input;
      press_buttons (session, terrain, nullptr, presses);
      // Held, the use key opens the wing as soon as the bike leaves the
      // ground.
      if (input.use_held && session.logic ().m_doing != Doing::gliding ())
        deploy_glider (session, terrain);
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
    void tend_haul (GameSession& session, seconds_t step) {
      GameLogicState& logic = session.logic ();
      const Haul& haul = logic.m_haul;
      if (!haul.active)
        return;
      if (logic.m_doing != Doing::on_foot ()) {
        let_go (session, false);
        return;
      }
      const MachineBody body = session.machine_body (haul.machine);
      const Vec3 tie = body.point (haul.tie);
      Walker& walker = session.walker ();
      Vec3 out = position_value (walker.position ()) - tie;
      const float drop = std::abs (out[1]);
      out[1] = 0.0f;
      // Pulled out of the hand: the machine fell away, or the walker did.
      if (length (out) > meters_value (haul.rope + 4.0f * u::m) ||
          drop > 4.0f) {
        let_go (session, false);
        return;
      }
      const HaulForce force = haul_force (haul,
                                          body,
                                          position_value (walker.position ()),
                                          velocity_value (walker.velocity ()));
      session.push_machine (
        haul.machine, tie, quantity_cast<isq::impulse> (force.pull * step));
      session.push_machine (haul.machine,
                            body.point (machine_kind (haul.machine).centre),
                            quantity_cast<isq::impulse> (force.lift * step));
      walker.tether (moppe::position (tie),
                     std::max (0.6f * u::m, haul.rope - rope_give),
                     haul.rope + rope_give);
    }

    // Notes how each machine is getting on, and heaves the one being
    // driven if it is stuck.
    void strain_machines (GameSession& session,
                          seconds_t dt,
                          const GroundCover& ground) {
      GameLogicState& logic = session.logic ();
      const std::optional<Machine> driven = driven_machine (logic.m_doing);
      for (int i = 0; i < machine_count; ++i) {
        const Machine machine = static_cast<Machine> (i);
        const MachineBody body = session.machine_body (machine);
        Trouble& trouble = trouble_of (logic, machine);
        // The helicopter flies out of any hole.
        const float throttle =
          driven == machine && machine != Machine::helicopter
            ? logic.m_controls.go
            : 0.0f;
        if (!strain (trouble, body.position, throttle, dt))
          continue;
        const velocity_t kick = heave (trouble, body.forward, throttle);
        ++trouble.heaves;
        session.push_machine (machine,
                              body.point (machine_kind (machine).centre),
                              quantity_cast<isq::impulse> (kick * body.mass));
        puff (session,
              body.position - body.up * 0.3f,
              velocity_value (kick),
              10.0f,
              ground);
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
      for (int i = 0; i < machine_count; ++i)
        bodies.push_back (session.machine_body (static_cast<Machine> (i)));
      bodies.insert (bodies.end (), strangers.begin (), strangers.end ());
      const std::optional<Machine> driven = driven_machine (logic.m_doing);
      for (const Bump& bump : find_bumps (bodies)) {
        const MachineBody& a = bodies[bump.a];
        const MachineBody& b = bodies[bump.b];
        if (a.ours)
          session.push_machine (a.machine, bump.at, bump.impulse);
        if (b.ours)
          session.push_machine (b.machine, bump.at, -bump.impulse);
        const float speed = speed_value (bump.speed);
        if (speed < 2.5f)
          continue;
        puff (session,
              bump.at,
              a.velocity * 0.5f + b.velocity * 0.5f,
              std::min (8.0f, speed),
              ground);
        const bool felt =
          (a.ours && a.machine == driven) || (b.ours && b.machine == driven);
        if (felt) {
          logic.m_shake =
            std::max (logic.m_shake, std::min (0.2f, 0.02f * speed));
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

    // The Long John back down the trail behind the motocross; game.cc
    // parks it on open ground there.
    position_t long_john_stand (const WorldParams& world) {
      const Vec3 heading (0.70710678f, 0.0f, 0.70710678f);
      return moppe::position (position_value (world.spawn_position ()) -
                              heading * 20.0f);
    }
  }

  // What the hands do with the machines; game/affordances.hh says when.
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
      moppe::position (position +
                       Vec3 (0, meters_value (mov::Glider::bike_hang), 0)),
      inherited,
      heading,
      true);
    GameLogicState& logic = session.logic ();
    session.set_mode (Doing::gliding ());
    steer (session, logic.m_controls);
  }

  void drop_bike (GameSession& session) {
    session.glider ().drop_bike ();
  }

  // Takes up a rope tied to the nearer end of a stuck machine, as long
  // as it reaches from there to the walker's feet.
  void take_rope (GameSession& session, Machine machine) {
    const MachineBody body = session.machine_body (machine);
    const Vec3 feet = position_value (session.walker ().position ());
    const bool front = dot (feet - body.position, body.forward) >= 0.0f;
    Haul& haul = session.logic ().m_haul;
    haul = { .active = true,
             .machine = machine,
             .tie = (front ? machine_kind (machine).front_tie
                           : machine_kind (machine).back_tie) };
    Vec3 out = feet - body.point (haul.tie);
    out[1] = 0.0f;
    haul.rope = std::clamp (length (out), 1.0f, 1.8f) * u::m;
  }

  void let_go_rope (GameSession& session) {
    let_go (session, true);
  }

  void get_in (GameSession& session, Machine machine) {
    session.set_mode (Doing::driving (machine));
    steer (session, session.logic ().m_controls);
  }

  // Out of `machine` and onto the feet, where its kind says
  // (MachineKind::step_out), letting go of everything.
  void step_out (GameSession& session, Machine machine) {
    GameLogicState& logic = session.logic ();
    const MachineKind& kind = machine_kind (machine);
    drive (session.fleet (), machine, Controls {}, false);
    visit (session.fleet (), machine, [&] (Machine, const Drivable auto& b) {
      const Vec3 at =
        kind.step_out ? b.point (*kind.step_out) : session.boat_landing ();
      // Off the water's machines one steps with half their way on.
      const float keep = kind.medium == Medium::water ? 0.5f : 1.0f;
      session.walker ().spawn (moppe::position (at),
                               unit_direction (b.forward ()),
                               moppe::velocity (b.velocity () * keep));
    });
    session.set_mode (Doing::on_foot ());
    steer (session, logic.m_controls);
  }

  // The line goes on as long as it is: no slack, and none too little.
  void hook_load (GameSession& session, Machine under) {
    const Vec3 hook = session.helicopter ().point (sling_hook ());
    const Vec3 ring =
      session.machine_body (under).point (machine_kind (under).ring);
    const meters_t span = length (hook - ring) * u::m;
    session.logic ().m_sling = { .hooked = true,
                                 .load = under,
                                 .stretch =
                                   std::min (span - sling_line, meters_t {}),
                                 .payout =
                                   std::max (span - sling_line, meters_t {}) };
  }

  void let_load_go (GameSession& session) {
    session.logic ().m_sling.hooked = false;
  }

  MachineBody GameSession::machine_body (Machine machine) const {
    return visit (m_fleet, machine, [] (Machine m, const Drivable auto& b) {
      MachineBody body;
      body.machine = m;
      body.position = b.position ();
      body.forward = b.forward ();
      body.up = b.up ();
      body.velocity = b.velocity ();
      body.mass = b.mass ();
      if constexpr (requires { b.trailer_position (); }) {
        body.towing = true;
        body.towed_position = b.trailer_position ();
        body.towed_forward = b.trailer_forward ();
        body.towed_up = b.trailer_up ();
        body.towed_mass = b.trailer_mass ();
      }
      return body;
    });
  }

  void GameSession::push_machine (Machine machine,
                                  const Vec3& at,
                                  const impulse_t& impulse) {
    visit (m_fleet, machine, [&] (Machine, Drivable auto& b) {
      b.push (at, impulse);
    });
  }

  std::optional<Machine> GameSession::haul_within_reach () const {
    if (m_logic.m_doing != Doing::on_foot () || m_logic.m_haul.active)
      return std::nullopt;
    const std::optional<Machine> machine = mount_within_reach ();
    if (!machine)
      return std::nullopt;
    // The boat run up on the land is dragged back to the water by its
    // bow handle; it goes nowhere under its own power there.
    if (*machine == Machine::boat && m_fleet.boat.grounded () &&
        !m_fleet.boat.afloat ())
      return machine;
    // And the jet ski, whose pump has nothing to work on there, by the
    // eye in its stem.
    if (*machine == Machine::jet_ski && m_fleet.jet_ski.grounded () &&
        !m_fleet.jet_ski.afloat ())
      return machine;
    if (!m_logic.m_trouble[static_cast<std::size_t> (*machine)].stuck)
      return std::nullopt;
    return machine;
  }

  std::optional<Machine> GameSession::hauling () const {
    if (m_logic.m_doing != Doing::on_foot () || !m_logic.m_haul.active)
      return std::nullopt;
    return m_logic.m_haul.machine;
  }

  Vec3 GameSession::rope_end () const {
    const Haul& haul = m_logic.m_haul;
    return machine_body (haul.machine).point (haul.tie);
  }

  bool GameSession::stuck () const {
    const std::optional<Machine> driven = driven_machine (m_logic.m_doing);
    return driven &&
           m_logic.m_trouble[static_cast<std::size_t> (*driven)].stuck;
  }

  GameSession::GameSession (const WorldParams& world,
                            const map::SurfaceGeometry& surface)
      : m_fleet {
          .bike = mov::Vehicle (world.spawn_position (),
                                45 * u::deg,
                                surface,
                                1800 * u::N,
                                5.2f * u::kW,
                                175 * u::kg),
          // A rider of 80 kg on 15 kg of bike, putting down some 750 W
          // at most and 320 N at the tyre.
          .mountain_bike = mov::Vehicle (mountain_bike_stand (world),
                                         45 * u::deg,
                                         surface,
                                         320 * u::N,
                                         750 * u::W,
                                         95 * u::kg,
                                         mov::mountain_bike_rig),
          // A rider of 80 kg on 35 kg of bike with its hub motor,
          // pedalling with the motor's help: some 600 W at most and
          // 420 N at the tyre.
          .long_john = mov::Vehicle (long_john_stand (world),
                                     45 * u::deg,
                                     surface,
                                     420 * u::N,
                                     600 * u::W,
                                     115 * u::kg,
                                     mov::long_john_rig),
          .jeep = mov::Car (surface, mov::jeep_rig),
          .helicopter = mov::Helicopter (surface),
          .tractor = mov::Tractor (surface),
          .caddy = mov::Car (surface, mov::caddy_rig),
          .boat = mov::Boat (surface),
          .jet_ski = mov::JetSki (surface),
        },
        m_glider (surface), m_camera (18 * u::deg, 6.5f * u::m) {}

  Vec3 GameSession::subject_position () const {
    switch (m_logic.m_doing.way) {
    case Doing::Way::foot:
      return position_value (m_walker.position ());
    case Doing::Way::glider:
      return m_glider.position ();
    case Doing::Way::passenger:
    case Doing::Way::bus:
      return m_logic.m_seat_position;
    case Doing::Way::driving:
      return visit (
        m_fleet, m_logic.m_doing.machine, [] (Machine, const Drivable auto& b) {
          return b.position ();
        });
    }
    std::unreachable ();
  }

  Vec3 GameSession::subject_heading () const {
    switch (m_logic.m_doing.way) {
    case Doing::Way::foot:
      return direction_value (m_walker.heading ());
    case Doing::Way::glider:
      return m_glider.heading ();
    case Doing::Way::passenger:
    case Doing::Way::bus:
      return m_logic.m_seat_heading;
    case Doing::Way::driving:
      return visit (
        m_fleet, m_logic.m_doing.machine, [] (Machine, const Drivable auto& b) {
          return b.forward ();
        });
    }
    std::unreachable ();
  }

  float GameSession::subject_speed_kmh () const {
    switch (m_logic.m_doing.way) {
    case Doing::Way::foot:
      return 0.0f;
    case Doing::Way::glider:
      return m_glider.airspeed ().numerical_value_in (u::m / u::s) * 3.6f;
    case Doing::Way::passenger:
    case Doing::Way::bus:
      return length (m_logic.m_seat_velocity) * 3.6f;
    case Doing::Way::driving:
      return visit (
        m_fleet, m_logic.m_doing.machine, [] (Machine, const Drivable auto& b) {
          return length (b.velocity ()) * 3.6f;
        });
    }
    std::unreachable ();
  }

  std::optional<Machine> GameSession::mount_within_reach () const {
    if (m_logic.m_doing != Doing::on_foot ())
      return std::nullopt;
    const Vec3 feet = position_value (m_walker.position ());
    std::optional<Machine> nearest;
    float best = 1e30f;
    for_each_machine (m_fleet, [&] (Machine m, const Drivable auto& b) {
      const MachineKind& kind = machine_kind (m);
      const float reach = meters_value (kind.reach);
      const float d = length2 (feet - b.point (kind.reach_from));
      if (d < reach * reach && d < best) {
        best = d;
        nearest = m;
      }
    });
    return nearest;
  }

  std::optional<Machine> GameSession::hook_within_reach () const {
    if (m_logic.m_doing != Doing::driving (Machine::helicopter) ||
        m_logic.m_sling.hooked || m_fleet.helicopter.altitude () < 2.0f * u::m)
      return std::nullopt;
    const Vec3 hook = m_fleet.helicopter.point (sling_hook ());
    std::optional<Machine> nearest;
    float best = 1e30f;
    for (int i = 0; i < machine_count; ++i) {
      const Machine machine = static_cast<Machine> (i);
      if (machine == Machine::helicopter)
        continue;
      const Vec3 ring =
        machine_body (machine).point (machine_kind (machine).ring);
      const Vec3 off = hook - ring;
      const float aside = std::hypot (off[0], off[2]);
      if (off[1] < 1.0f || aside > meters_value (sling_reach) ||
          length (off) > meters_value (sling_line + sling_slack))
        continue;
      if (aside < best) {
        best = aside;
        nearest = machine;
      }
    }
    return nearest;
  }

  bool GameSession::can_step_out_of_helicopter () const {
    return m_logic.m_doing == Doing::driving (Machine::helicopter) &&
           m_fleet.helicopter.altitude () < 1.2f * u::m &&
           length (m_fleet.helicopter.velocity ()) < 4.0f;
  }

  bool
  GameSession::can_deploy_glider (const map::SurfaceGeometry& terrain) const {
    if (m_logic.m_doing != Doing::driving (Machine::bike) ||
        !m_fleet.bike.airborne ())
      return false;
    const Vec3 position = m_fleet.bike.position ();
    const float ground = terrain::surface_elevation_value (
      spatial::sample<terrain::surface_elevation> (
        terrain, moppe::position (Vec3 (position[0], 0.0f, position[2]))));
    return position[1] - ground > 3.0f;
  }

  bool GameSession::can_drop_bike () const {
    return m_logic.m_doing == Doing::gliding () && m_glider.bike_attached ();
  }

  void GameSession::set_mode (Doing mode) {
    const bool was_on_foot = m_logic.m_doing == Doing::on_foot ();
    const bool on_foot = mode == Doing::on_foot ();
    m_logic.m_doing = mode;
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
    if (m_logic.m_doing == Doing::driving (Machine::bike))
      step_out (*this, Machine::bike);
    m_logic.m_cam_mode = CAM_CHASE;
    m_logic.m_look_pitch = 0.0f;
    m_logic.m_fp_eye = position_value (m_walker.eye_position ());
    m_camera.reset ();
  }

  CameraRig GameSession::camera_rig () const {
    CameraRig rig;
    switch (m_logic.m_doing.way) {
    case Doing::Way::foot:
      rig.eye = position_value (m_walker.eye_position ());
      rig.ahead = direction_value (m_walker.heading ());
      rig.pivot = position_value (m_walker.position ()) + Vec3 (0, 1.35f, 0);
      rig.velocity = velocity_value (m_walker.velocity ());
      rig.distance = 3.8f * u::m;
      rig.nearest = 0.9f * u::m;
      rig.elevation = 10.0f * u::deg;
      break;
    case Doing::Way::glider:
      rig.eye = m_glider.position () - Vec3 (0, 0.75f, 0);
      rig.ahead = m_glider.heading ();
      rig.pivot = position_value (m_glider.physical_position ());
      rig.velocity = velocity_value (m_glider.physical_velocity ());
      break;
    case Doing::Way::passenger: {
      // From the seat, or from behind as the machine's driver is seen.
      const SeatView& view = machine_kind (m_logic.m_doing.machine).view;
      rig.eye = m_logic.m_seat_position + m_logic.m_seat_up * 0.78f +
                m_logic.m_seat_heading * 0.08f;
      rig.ahead = m_logic.m_seat_heading;
      rig.pivot = m_logic.m_seat_position + Vec3 (0, 0.5f, 0);
      rig.velocity = m_logic.m_seat_velocity;
      rig.distance = view.distance;
      rig.nearest = view.nearest;
      rig.elevation = view.elevation;
      break;
    }
    case Doing::Way::bus:
      // From the front seat, a sitter's head over it, looking out
      // through the windscreen; or from well behind and above the bus.
      rig.eye = m_logic.m_seat_position + m_logic.m_seat_up * 0.8f +
                m_logic.m_seat_heading * 0.1f;
      rig.ahead = m_logic.m_seat_heading;
      rig.pivot = m_logic.m_seat_position + m_logic.m_seat_up * 1.3f;
      rig.velocity = m_logic.m_seat_velocity;
      rig.distance = 15.0f * u::m;
      rig.nearest = 7.0f * u::m;
      rig.elevation = 14.0f * u::deg;
      break;
    case Doing::Way::driving: {
      const Machine machine = m_logic.m_doing.machine;
      const SeatView& view = machine_kind (machine).view;
      visit (m_fleet, machine, [&] (Machine, const Drivable auto& b) {
        rig.eye =
          seat_of (b) + b.up () * view.eye[1] + b.forward () * view.eye[2];
        rig.ahead = b.forward ();
        rig.pivot =
          b.position () + Vec3 (0, meters_value (view.pivot_height), 0);
        rig.velocity = b.velocity ();
      });
      rig.distance = view.distance;
      rig.nearest = view.nearest;
      rig.elevation = view.elevation;
      if (machine != Machine::helicopter)
        break;
      // Behind the way it heads, level, whatever its nose is doing.
      Vec3 ahead = rig.ahead;
      ahead[1] = 0.0f;
      rig.ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
      // Further off and lower, to see what hangs under it too.
      if (m_logic.m_sling.hooked) {
        rig.pivot = rig.pivot * 0.65f +
                    machine_body (m_logic.m_sling.load).position * 0.35f;
        rig.distance = 24.0f * u::m;
        rig.nearest = 8.0f * u::m;
        rig.elevation = 14.0f * u::deg;
      }
      break;
    }
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
      if (m_logic.m_doing == Doing::driving (Machine::helicopter))
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
    const Vec3 pivot = rig.pivot;
    for (ChaseCamera::Room room : m_rooms) {
      room.centre[0] +=
        terrain::whole_periods (pivot[0] - room.centre[0], m_room_period_x);
      room.centre[2] +=
        terrain::whole_periods (pivot[2] - room.centre[2], m_room_period_z);
      m_camera.confine (room);
    }
  }

  void GameSession::settle_camera (const map::SurfaceGeometry& surface) {
    m_logic.m_fp_eye = camera_rig ().eye;
    m_camera.reset ();
    follow_camera (surface, 0 * u::s);
  }

  void GameSession::clear_controls () {
    steer (*this, Controls {});
  }

  GameSession::State GameSession::state () const {
    State state { .logic = m_logic,
                  .glider = m_glider.state (),
                  .walker = m_walker.state (),
                  .camera = m_camera.state (),
                  .stars = m_stars.state (),
                  .dust = m_dust.state (),
                  .animals = m_animals.state (),
                  .hens = m_hens.state () };
    for_each_machine_across (
      [] (Machine, const auto& body, auto& kept) { kept = body.state (); },
      m_fleet,
      state.fleet);
    return state;
  }

  void GameSession::restore (const State& state) {
    m_logic = state.logic;
    for_each_machine_across (
      [] (Machine, auto& body, const auto& kept) { body.restore (kept); },
      m_fleet,
      state.fleet);
    m_glider.restore (state.glider);
    m_walker.restore (state.walker);
    m_camera.restore (state.camera);
    m_stars.restore (state.stars);
    m_dust.restore (state.dust);
    m_animals.restore (state.animals);
    m_hens.restore (state.hens);
  }

  namespace {
    // The water as what floats on it feels it at `time`: the waves lift
    // a hull and carry it to and fro, and a river's current carries it
    // downstream. What it borrows must outlive it.
    mov::BoatWater floating_water (const WorldParams& world,
                                   const map::SurfaceGeometry& surface,
                                   const terrain::WaterSheets* water,
                                   double time) {
      const float sea = world.water_level.numerical_value_in (u::m);
      return { [&world, &surface, water, time] (position_t at) {
                return water_surface_at (world, surface, water, at, time);
              },
               [&surface, water, time, sea] (position_t at) {
                 if (!water)
                   return velocity (Vec3 ());
                 const Vec3 here = position_value (at);
                 const Vec3 under (here[0], 0.0f, here[2]);
                 const terrain::SeaWaves waves = water_waves_at (
                   surface, water, moppe::position (under), time);
                 const position_t where = moppe::position (under);
                 const float sheet = terrain::surface_elevation_value (
                   spatial::sample<terrain::surface_elevation> (*water, where));
                 if (sheet <= ground_at (surface, under) ||
                     sheet <= sea + 0.05f)
                   return waves.velocity;
                 Vec3 current =
                   spatial::sample<terrain::water_velocity> (*water, where)
                     .numerical_value_in (u::m / u::s);
                 return waves.velocity +
                        velocity (Vec3 (current[0], 0.0f, current[2]));
               } };
    }

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
      const mov::BoatWater floating = floating_water (
        world, surface, water, seconds_into_play (logic.m_total_time));
      boat.set_parked (logic.m_doing != Doing::driving (Machine::boat));
      boat.update (elapsed * u::s, floating);
      if (logic.m_doing != Doing::driving (Machine::boat))
        return;
      logic.m_odometer += length (boat.velocity ()) * elapsed;

      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const DisplayColor spray_color (0.86f, 0.92f, 0.97f);
      const Vec3 pivot (
        mov::Boat::pivot[0], mov::Boat::pivot[1], mov::Boat::pivot[2]);
      // Blue-grey smoke off the muffler, more under load and most with
      // the governor tied back.
      const Vec3 exhaust = boat.point (mov::Boat::tail_point (
        pivot + Vec3 (-0.26f, 0.27f, 0.0f), boat.swing (), boat.tilt ()));
      if (chance (logic.m_fx_rng) < 0.10f + 0.45f * boat.rpm ()) {
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
      const float bite = boat.propeller_depth ().numerical_value_in (u::m);
      if (boat.thrust () > 30.0f * u::N && bite > -0.05f && bite < 0.35f) {
        const Vec3 aft = normalized (prop - boat.point (pivot));
        Vec3 back = aft;
        back[1] = 0.0f;
        back = length2 (back) > 1e-6f ? normalized (back) : -boat.forward ();
        const float power =
          (boat.thrust () / (520.0f * u::N)).numerical_value_in (one);
        Dust::Style plume;
        plume.size = 0.12f * u::m;
        plume.lifetime = 1.0f * u::s;
        plume.downward_acceleration =
          9.0f * isq::acceleration[u::m / pow<2> (u::s)];
        plume.spread = 0.35f * one;
        plume.flake = true;
        const int drops = static_cast<int> (std::round (4.0f * power + 0.5f));
        session.dust ().emit (moppe::position (prop + Vec3 (0, 0.05f, 0)),
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
          const Vec3 at = boat.point (Vec3 (
            side * (0.45f + 0.25f * planing), 0.10f, 2.4f - 1.6f * planing));
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
      if (boat.grounded () && speed > 1.0f && chance (logic.m_fx_rng) < 0.3f) {
        Vec3 under = boat.point (Vec3 (0, 0, 1.5f));
        under[1] = ground_at (surface, under) + 0.1f;
        session.dust ().emit (moppe::position (under),
                              velocity (boat.velocity () * 0.2f),
                              1,
                              DisplayColor (0.60f, 0.52f, 0.40f));
      }
    }

    // The jet ski floats as the boat does. Ridden, its jet throws a
    // plume of water up and back from the transom, and its bow sheets
    // of spray aside, further aft the higher it planes.
    void advance_jet_ski (const WorldParams& world,
                          const map::SurfaceGeometry& surface,
                          GameSession& session,
                          const terrain::WaterSheets* water,
                          float elapsed) {
      GameLogicState& logic = session.logic ();
      mov::JetSki& ski = session.jet_ski ();
      ski.set_parked (logic.m_doing != Doing::driving (Machine::jet_ski));
      ski.update (
        elapsed * u::s,
        floating_water (
          world, surface, water, seconds_into_play (logic.m_total_time)));
      if (logic.m_doing != Doing::driving (Machine::jet_ski))
        return;
      logic.m_odometer += length (ski.velocity ()) * elapsed;

      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const DisplayColor spray_color (0.86f, 0.92f, 0.97f);
      const float power = (ski.thrust () / mov::JetSki::static_thrust ())
                            .numerical_value_in (one);
      if (power > 0.08f) {
        Vec3 back = ski.jet_direction ();
        back[1] = 0.0f;
        back = length2 (back) > 1e-6f ? normalized (back) : -ski.forward ();
        Dust::Style plume;
        plume.size = 0.11f * u::m;
        plume.lifetime = 0.9f * u::s;
        plume.downward_acceleration =
          9.0f * isq::acceleration[u::m / pow<2> (u::s)];
        plume.spread = 0.3f * one;
        plume.flake = true;
        session.dust ().emit (
          moppe::position (ski.jet () + Vec3 (0, 0.1f, 0)),
          velocity (ski.velocity () * 0.6f + back * (2.5f + 5.0f * power) +
                    Vec3 (0, 2.0f + 4.5f * power, 0)),
          static_cast<int> (std::round (5.0f * power + 0.5f)),
          spray_color,
          plume);
        if (chance (logic.m_fx_rng) < 0.6f * power) {
          Dust::Style mist;
          mist.size = 0.55f * u::m;
          mist.lifetime = 1.0f * u::s;
          mist.downward_acceleration =
            1.0f * isq::acceleration[u::m / pow<2> (u::s)];
          mist.spread = 0.9f * one;
          session.dust ().emit (
            moppe::position (ski.jet () + back * 0.9f + Vec3 (0, 0.5f, 0)),
            velocity (ski.velocity () * 0.5f + Vec3 (0, 0.8f, 0)),
            1,
            spray_color,
            mist);
        }
      }

      Vec3 way = ski.velocity ();
      way[1] = 0.0f;
      const float speed = length (way);
      if (ski.afloat () && speed > 3.0f) {
        const float planing = std::clamp ((speed - 5.0f) / 7.0f, 0.0f, 1.0f);
        Dust::Style sheet;
        sheet.size = 0.10f * u::m;
        sheet.lifetime = 0.6f * u::s;
        sheet.downward_acceleration =
          9.0f * isq::acceleration[u::m / pow<2> (u::s)];
        sheet.spread = 0.3f * one;
        sheet.flake = true;
        const int drops =
          static_cast<int> (std::round (std::min (3.0f, speed * 0.2f)));
        for (const float side : { 1.0f, -1.0f }) {
          const Vec3 at = ski.point (Vec3 (
            side * (0.3f + 0.2f * planing), 0.12f, 1.2f - 1.3f * planing));
          session.dust ().emit (
            moppe::position (at),
            velocity (ski.velocity () * 0.7f +
                      ski.left () * (side * (1.2f + 0.2f * speed)) +
                      Vec3 (0, 1.0f + 0.12f * speed, 0)),
            drops,
            spray_color,
            sheet);
        }
      }
      // Run up on the shore, the hull grinds over what is there.
      if (ski.grounded () && speed > 1.0f && chance (logic.m_fx_rng) < 0.3f) {
        Vec3 under = ski.point (Vec3 (0, 0, 0.6f));
        under[1] = ground_at (surface, under) + 0.1f;
        session.dust ().emit (moppe::position (under),
                              velocity (ski.velocity () * 0.2f),
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
                        std::span<const MachineBody> strangers,
                        const Surroundings* around) {
    if (around) {
      // What the walker does by hand takes from the input what it uses
      // before the bodies move, and the animals mind where they went.
      InputFrame rest = input;
      press_buttons (session, surface, around, rest);
      if (around->bus)
        ride_bus (session, *around->bus);
      tend_by_hand (session, *around, rest, dt);
      const GameSessionAdvanceResult result = advance_game_session (
        world, surface, session, rest, dt, trunks, ground, water, strangers);
      tend_animals (session, *around, dt);
      return result;
    }
    const float elapsed = dt.numerical_value_in (u::s);
    GameLogicState& logic = session.logic ();
    // Rivers and lakes hold back what goes through them as the sea does.
    const auto water_at = [&] (const Vec3& at) {
      return water_surface_at (world,
                               surface,
                               water,
                               moppe::position (at),
                               seconds_into_play (logic.m_total_time));
    };
    session.bike ().set_water_level (water_at (session.bike ().position ()));
    session.bike ().set_trunks (trunks);
    mov::Vehicle& mountain_bike = session.mountain_bike ();
    mountain_bike.set_water_level (water_at (mountain_bike.position ()));
    mountain_bike.set_trunks (trunks);
    mov::Vehicle& long_john = session.long_john ();
    long_john.set_water_level (water_at (long_john.position ()));
    long_john.set_trunks (trunks);

    // Where one would step out of the boat, or off the jet ski: over
    // whichever side, or the bow, has the shallowest water.
    if (logic.m_doing == Doing::driving (Machine::boat) ||
        logic.m_doing == Doing::driving (Machine::jet_ski)) {
      const bool skiff = logic.m_doing == Doing::driving (Machine::boat);
      const float beside = skiff ? 1.1f : 1.0f;
      const float amidships = skiff ? 0.4f : 0.0f;
      const float bow = (skiff ? mov::Boat::stem : mov::JetSki::stem) + 0.7f;
      Vec3 best;
      float shallowest = 1e30f;
      for (const Vec3& side : { Vec3 (beside, 0.3f, amidships),
                                Vec3 (-beside, 0.3f, amidships),
                                Vec3 (0.0f, 0.3f, bow) }) {
        Vec3 at = skiff ? session.boat ().point (side)
                        : session.jet_ski ().point (side);
        const float ground = ground_at (surface, at);
        const float depth = water_at (at).numerical_value_in (u::m) - ground;
        if (depth < shallowest) {
          shallowest = depth;
          at[1] = ground + 0.1f;
          best = at;
        }
      }
      session.set_boat_landing (best);
    }

    apply_input_frame (session, surface, input);
    tend_haul (session, dt);
    tend_sling (session, dt);
    // What is on the rope rolls free after the walker, and what hangs on
    // the helicopter's line after it.
    const auto hauled = [&] (Machine machine) {
      return session.hauling () == machine ||
             (logic.m_sling.hooked && logic.m_sling.load == machine);
    };
    // Only a ridden bike moves under its own physics; one left standing is
    // parked until someone mounts it again.
    session.bike ().set_parked (
      logic.m_doing != Doing::driving (Machine::bike) &&
      !session.can_drop_bike () && !hauled (Machine::bike));

    if (flies_bike (session)) {
      if (fly_bike (session, dt)) {
        session.glider ().land ();
        finish_glide (session);
      }
    } else {
      if (!session.can_drop_bike ())
        session.bike ().update (dt);
      if (logic.m_doing == Doing::gliding ()) {
        if (session.glider ().update (dt))
          finish_glide (session);
      }
    }
    // The mountainbike stands where it was left too.
    mountain_bike.set_parked (logic.m_doing !=
                                Doing::driving (Machine::mountain_bike) &&
                              !hauled (Machine::mountain_bike));
    mountain_bike.update (dt);
    long_john.set_parked (logic.m_doing !=
                            Doing::driving (Machine::long_john) &&
                          !hauled (Machine::long_john));
    long_john.update (dt);
    if (logic.m_doing == Doing::on_foot ())
      session.walker ().update (dt,
                                surface,
                                world,
                                trunks,
                                water,
                                seconds_into_play (logic.m_total_time));
    // The jeep stands braked wherever its driver left it.
    mov::Car& jeep = session.jeep ();
    jeep.set_trunks (trunks);
    jeep.set_parked (logic.m_doing != Doing::driving (Machine::jeep) &&
                     !hauled (Machine::jeep));
    jeep.update (dt, water_at (jeep.position ()));
    // The helicopter's rotor runs down once its pilot has stepped out.
    mov::Helicopter& heli = session.helicopter ();
    heli.set_trunks (trunks);
    heli.set_parked (logic.m_doing != Doing::driving (Machine::helicopter));
    heli.update (dt, water_at (heli.position ()));
    if (logic.m_doing == Doing::driving (Machine::helicopter)) {
      logic.m_odometer += length (heli.velocity ()) * elapsed;
      // Low over the ground, the downwash blows up what lies there.
      const float height = heli.altitude ().numerical_value_in (u::m);
      if (heli.rotor () > 0.6f * one && height < 9.0f) {
        std::uniform_real_distribution<float> chance (0.0f, 1.0f);
        const float strength =
          scalar_value (heli.rotor ()) * (1.0f - height / 9.0f);
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
    tractor.set_parked (logic.m_doing != Doing::driving (Machine::tractor) &&
                        !hauled (Machine::tractor));
    tractor.update (dt, water_at (tractor.position ()));
    if (logic.m_doing == Doing::driving (Machine::tractor)) {
      logic.m_odometer += length (tractor.velocity ()) * elapsed;
      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const float throttle = scalar_value (tractor.throttle ());
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
    mov::Car& caddy = session.caddy ();
    caddy.set_trunks (trunks);
    caddy.set_parked (logic.m_doing != Doing::driving (Machine::caddy) &&
                      !hauled (Machine::caddy));
    caddy.set_slung (logic.m_sling.hooked &&
                     logic.m_sling.load == Machine::caddy);
    caddy.update (dt, water_at (caddy.position ()));
    if (logic.m_doing == Doing::driving (Machine::caddy)) {
      logic.m_odometer += length (caddy.velocity ()) * elapsed;
      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const float throttle = scalar_value (caddy.throttle ());
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
    session.jet_ski ().set_trunks (trunks);
    advance_jet_ski (world, surface, session, water, elapsed);
    if (logic.m_doing == Doing::driving (Machine::jeep)) {
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

    strain_machines (session, dt, ground);
    bump_machines (session, strangers, ground);

    const Vec3 vehicle_position = session.subject_position ();
    // What follows is the ridden bike's: the motocross, or the pedalled
    // mountainbike, which has no engine to roost, smoke, or burn.
    mov::Vehicle& vehicle = session.ridden_bike ();
    const bool pedalled = game::pedalled (logic.m_doing);

    // A parked bike's impacts shouldn't linger until remount.
    for (mov::Vehicle* bike : { &session.bike (), &mountain_bike, &long_john })
      if (bike != &vehicle ||
          !(logic.m_doing == Doing::driving (Machine::bike) || pedalled)) {
        bike->pop_impact ();
        bike->pop_fall_drop ();
      }

    const bool in_water =
      vehicle_position[1] <
      water_at (vehicle_position).numerical_value_in (moppe::u::m) + 0.3f;
    const bool driving =
      logic.m_doing == Doing::driving (Machine::bike) || pedalled;
    const float impact = driving ? speed_value (vehicle.pop_impact ()) : 0.0f;

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
      logic.m_jump_airtime = seconds_value (vehicle.airtime ());
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

    // A wheelie scores the ground it covers on the back wheel, two points a
    // metre once it has gone five, unless it ends in a loop. Once up it
    // lasts until the front wheel comes down (for a moment more than a
    // bump's), or the bike leaves the ground for a jump.
    logic.m_wheelie_age += elapsed;
    const bool riding_up =
      logic.m_doing == Doing::driving (Machine::bike) &&
      (vehicle.wheeling () ||
       (logic.m_wheelie_m > 0.0f && !vehicle.front_down () &&
        vehicle.airtime () < 0.5f * u::s));
    if (riding_up) {
      Vec3 ground_speed = vehicle.velocity ();
      ground_speed[1] = 0.0f;
      logic.m_wheelie_m += length (ground_speed) * elapsed;
      logic.m_wheelie_gap = 0.0f;
    } else if (logic.m_wheelie_m > 0.0f) {
      logic.m_wheelie_gap += elapsed;
      if (logic.m_wheelie_gap > 0.15f ||
          logic.m_doing != Doing::driving (Machine::bike)) {
        logic.m_last_wheelie_m = logic.m_wheelie_m;
        // One looped over onto its back scores nothing.
        const bool looped = vehicle.render_normal ()[1] < 0.3f;
        logic.m_last_wheelie_points =
          logic.m_wheelie_m >= 5.0f && !looped
            ? (int)std::round (2.0f * logic.m_wheelie_m)
            : 0;
        logic.m_score += logic.m_last_wheelie_points;
        logic.m_best_wheelie_m =
          std::max (logic.m_best_wheelie_m, logic.m_wheelie_m);
        logic.m_wheelie_m = 0.0f;
        logic.m_wheelie_age = 0.0f;
      }
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
      vehicle_position -
      forward * (logic.m_doing == Doing::driving (Machine::long_john) ? 0.95f
                 : pedalled                                           ? 0.55f
                                                                      : 0.61f) +
      Vec3 (0, -0.25f, 0);

    if (clean_stunt_landing && landed_trick_quarters > 0) {
      Dust::Style trick_flash;
      trick_flash.size = 0.22f * u::m;
      trick_flash.lifetime = 0.65f * u::s;
      trick_flash.downward_acceleration =
        5.0f * isq::acceleration[u::m / pow<2> (u::s)];
      trick_flash.spread = 1.2f * one;
      trick_flash.additive = true;
      session.dust ().emit (
        moppe::position (vehicle_position + Vec3 (0, 0.1f, 0)),
        velocity (vehicle.velocity () * 0.15f + Vec3 (0, 3, 0)),
        std::min (24, 8 + landed_trick_quarters * 4),
        DisplayColor (0.25f, 0.78f, 1.0f),
        trick_flash);
    }

    // Drift kicks up dirt from the rear wheel (or spray), and so does a
    // tyre sliding under the locked rear brake, in a low fan swept out
    // sideways as the tail comes round.
    if (driving && vehicle.grounded () &&
        vehicle.drift_speed () > 6.0f * u::m / u::s) {
      const float count =
        std::min (4.0f, speed_value (vehicle.drift_speed ()) * 0.2f);
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
        vehicle_position - forward * 0.18f + Vec3 (0, -0.04f, 0);
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
        moppe::position (vehicle_position + Vec3 (0, -0.2f, 0)),
        velocity (vehicle.velocity () * 0.3f),
        3,
        spray_color);

    // Hard landings shake the camera and burst dirt outward: a low pancake of
    // dust plus a ring of ballistic clods.
    if (impact > 8.0f) {
      logic.m_shake = std::min (0.28f, 0.010f * impact);
      logic.m_shake_time = 0.0f;
      session.dust ().emit (
        moppe::position (vehicle_position + Vec3 (0, -0.25f, 0)),
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
          moppe::position (vehicle_position + Vec3 (0, -0.2f, 0)),
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
    if (driving && vehicle.pop_fall_drop () > 100.0f * u::m)
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
      const int picked = session.stars ().update (
        vehicle_position,
        static_cast<float> (seconds_into_play (logic.m_total_time)),
        seconds (elapsed));
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
        if (logic.m_doing != Doing::gliding ())
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
      logic.m_doing != Doing::on_foot () ? session.subject_speed_kmh () : 0.0f;
    const float fov_target =
      std::min (1.0f, std::max (0.0f, (kmh - 70.0f) / 180.0f));
    logic.m_fov_k += (fov_target - logic.m_fov_k) *
                     smoothing_alpha (5.0f / u::s, elapsed * u::s);

    return result;
  }
}
