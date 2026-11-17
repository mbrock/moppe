#include <moppe/game/avatar.hh>

#include <moppe/game/fishing.hh>
#include <moppe/game/walker.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace avatar_pose {
    // Hermite ease of x from e0 to e1 (either order) into [0, 1].
    float ease (float e0, float e1, float x) {
      const float t = std::clamp ((x - e0) / (e1 - e0), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    float mix (float a, float b, float t) {
      return a + (b - a) * t;
    }

    Vec3 mix (const Vec3& a, const Vec3& b, float t) {
      return a + (b - a) * t;
    }

    Vec3 flat (const Vec3& v) {
      return Vec3 (v[0], 0.0f, v[2]);
    }

    // `from` moved `by` along `way`.
    position_t
    moved (const position_t& from, const direction_t& way, meters_t by) {
      return from + quantity_cast<isq::position_vector> (along (way, by));
    }

    // Two-bone inverse kinematics: the middle joint of a limb of lengths
    // `a` and `b` reaching from `root` toward `target`, bent toward `pole`.
    // An out-of-reach target is pulled in to what the limb can touch.
    position_t bend (const position_t& root,
                     position_t& target,
                     meters_t a,
                     meters_t b,
                     const Vec3& pole) {
      Vec3 reach = (target - root).numerical_value_in (si::metre);
      meters_t distance = length (reach) * u::m;
      const meters_t longest = (a + b) * 0.999f;
      const meters_t shortest = abs (a - b) + 0.01f * u::m;
      if (distance < 1e-5f * u::m) {
        reach = Vec3 (0, -1, 0);
        distance = 1e-5f * u::m;
      }
      const direction_t axis =
        unit_direction (reach * (1.0f / meters_value (distance)));
      distance = std::clamp (distance, shortest, longest);
      target = moved (root, axis, distance);
      const meters_t along =
        (a * a + distance * distance - b * b) / (2.0f * distance);
      const meters_t out =
        sqrt (std::max (0.0f * u::m2, a * a - along * along));
      const Vec3& way = direction_value (axis);
      Vec3 side = pole - way * dot (pole, way);
      if (length2 (side) < 1e-8f)
        side = Vec3 (0, 0, 1) - way * way[2];
      return moved (
        moved (root, axis, along), unit_direction (normalized (side)), out);
    }

    // A point of a foot's path: ahead of the hips and above the ground.
    struct Key {
      float along, raise;
    };

    // A runner's swinging foot at `u` through the swing, leaving the
    // ground at `from` and landing at `to`: the heel kicks up behind
    // toward the seat, the foot comes through under a knee driven high,
    // and the shin swings out and paws back to land. A Catmull-Rom curve
    // through those keys, running straight on past its ends.
    Key run_swing (float u, float from, float to) {
      const Key keys[] = { { from, 0.0f },
                           { -0.30f, 0.50f },
                           { 0.28f, 0.42f },
                           { to + 0.16f, 0.14f },
                           { to, 0.0f } };
      constexpr int last = 4;
      const auto key = [&] (int j) {
        if (j < 0)
          return Key { 2.0f * keys[0].along - keys[1].along,
                       2.0f * keys[0].raise - keys[1].raise };
        if (j > last)
          return Key { 2.0f * keys[last].along - keys[last - 1].along,
                       2.0f * keys[last].raise - keys[last - 1].raise };
        return keys[j];
      };
      const float s = std::clamp (u, 0.0f, 1.0f) * last;
      const int j = std::min (last - 1, static_cast<int> (s));
      const float w = s - j;
      const auto curve = [w] (float a, float b, float c, float d) {
        return 0.5f * (2.0f * b + (c - a) * w +
                       (2.0f * a - 5.0f * b + 4.0f * c - d) * w * w +
                       (3.0f * b - a - 3.0f * c + d) * w * w * w);
      };
      const Key a = key (j - 1), b = key (j), c = key (j + 1), d = key (j + 2);
      return { curve (a.along, b.along, c.along, d.along),
               std::max (0.0f, curve (a.raise, b.raise, c.raise, d.raise)) };
    }
  }

  AvatarSkeleton pose_avatar (const WalkerPose& walker, seconds_t time) {
    using namespace avatar_pose;
    namespace size = avatar_size;
    AvatarSkeleton k;

    const Vec3 up (0, 1, 0);
    Vec3 f = flat (direction_value (walker.heading));
    f = length2 (f) > 1e-6f ? normalized (f) : Vec3 (0, 0, 1);
    const Vec3 r = normalized (cross (up, f));

    // Gait weights: how much the body moves at all, how much of that is a
    // run, how much it steps round on the spot, and how far into the air
    // it is. The stride speed holds its last value through the air, so a
    // jump folds the legs out of the gait they left the ground in.
    const float speed = speed_value (walker.stride_speed);
    const float turn = walker.turn_speed.numerical_value_in (u::rad / u::s);
    const float moving = ease (0.05f, 0.9f, speed);
    const float running = ease (4.2f, 7.0f, speed);
    const float stepping = (1.0f - moving) * ease (0.1f, 0.9f, std::abs (turn));
    const float gait = std::max (moving, stepping);
    const float air = walker.grounded
                        ? 0.0f
                        : ease (0.0f, 0.15f, seconds_value (walker.airborne));
    const float phase = walker.stride_phase.numerical_value_in (one);
    const float breath = std::sin (seconds_value (time) * 1.7f);
    const float sway = std::sin (seconds_value (time) * 0.6f);

    // A walk keeps a foot down most of the cycle; a run touches down for
    // under a quarter of it and is in flight between. Each foot is planted for
    // duty times the ground and the turn a cycle covers -- exactly what
    // the stride phase counts while it is down -- so it stays put.
    const float duty =
      mix (0.62f - 0.03f * std::min (speed, 3.2f), 0.23f, running);
    const float rate = per_second_value (
      gait_rate (speed * (u::m / u::s), turn * u::rad / u::s));
    const float per_cycle = rate > 1e-4f ? 1.0f / rate : 0.0f;
    const float stance = duty * speed * per_cycle * moving;
    const float stance_turn = duty * turn * per_cycle * gait;
    // A runner lands under the hips and pushes off well behind them.
    const float back = 0.06f * running * moving;
    const float lift = 0.11f * moving + 0.08f * stepping;
    const float track = mix (0.11f, 0.075f, running);
    Vec3 travel = flat (velocity_value (walker.velocity));
    travel = length2 (travel) > 0.04f ? normalized (travel) : f;

    // Heights on the ground plane the walker stands on.
    const Vec3& feet = position_value (walker.position);
    Vec3 n = direction_value (walker.ground_normal);
    if (n[1] < 0.5f)
      n = Vec3 (0, 1, 0);
    const auto ground = [&] (const Vec3& at) {
      return feet[1] -
             (n[0] * (at[0] - feet[0]) + n[2] * (at[2] - feet[2])) / n[1];
    };

    // Feet: a planted foot moves backward under the body at ground speed
    // and turns against the body as it turns; a swinging foot arcs
    // forward and lands toe-up on its heel. A runner's foot kicks up
    // behind, comes through under a high knee, and reaches out to land.
    radians_t pitch[2];
    Vec3 sole_way[2];
    // How much each foot hangs from its shin rather than keeping to the
    // ground: a runner's through the swing.
    float hang[2];
    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      const float t = std::fmod (phase + (i == 0 ? 0.0f : 0.5f), 1.0f);
      float along, yaw, raise = 0.0f;
      radians_t toe_up, heel_up;
      hang[i] = 0.0f;
      if (t < duty) {
        const float u = t / duty;
        along = stance * (0.5f - u) - back;
        yaw = stance_turn * (0.5f - u);
        toe_up = gait * mix (0.30f, 0.05f, running) *
                 (1.0f - ease (0.0f, 0.2f, u)) * u::rad;
        heel_up =
          gait * mix (0.45f, 0.8f, running) * ease (0.55f, 1.0f, u) * u::rad;
      } else {
        const float u = (t - duty) / (1.0f - duty);
        const float swung = ease (0.0f, 1.0f, u);
        hang[i] = running * moving * ease (0.0f, 0.2f, u) *
                  (1.0f - ease (0.75f, 1.0f, u));
        yaw = stance_turn * (swung - 0.5f);
        const Key stride =
          run_swing (u, -0.5f * stance - back, 0.5f * stance - back);
        along = mix (stance * (swung - 0.5f) - back, stride.along, running);
        raise = mix (lift * std::sin (PI * u), stride.raise * moving, running);
        toe_up = gait * 0.30f * ease (0.65f, 1.0f, u) * u::rad;
        heel_up = gait * mix (0.75f, 1.2f, running) *
                  (1.0f - ease (0.0f, mix (0.45f, 0.6f, running), u)) * u::rad;
      }
      Vec3 base =
        feet + yawed (travel * along + r * (side * track), yaw * u::rad);
      base[1] = ground (base);
      k.ankle[i] = position (base + up * (meters_value (size::ankle_height) +
                                          raise + 0.14f * libm::sin (heel_up) +
                                          0.05f * libm::sin (toe_up)));
      pitch[i] = toe_up - heel_up;
      sole_way[i] = yawed (f, yaw * u::rad);
    }

    // The pelvis turns forward on the side of the swinging leg, drops a
    // little on that side at a walk, and leans with the trunk. Into a
    // turn the whole body banks, as anything going round must.
    const radians_t hip_turn =
      moving * mix (0.10f, 0.08f, running) * std::cos (PI2 * phase) * u::rad;
    const float hip_drop = -moving * mix (0.06f, 0.03f, running) *
                           std::cos (PI2 * (phase - duty * 0.5f));
    const float hip_tilt = 0.03f + 0.10f * running * moving;
    const float bank =
      std::clamp (0.55f * std::atan (speed * turn / 9.81f), -0.3f, 0.3f) *
      (walker.grounded ? 1.0f : 0.5f);
    const Vec3 hip_ahead = yawed (f, hip_turn);
    const Vec3 hip_side = yawed (r, hip_turn);
    const Vec3 body_up =
      normalized (up + hip_ahead * hip_tilt + hip_side * (bank - hip_drop));
    const Vec3 body_right = normalized (cross (body_up, hip_ahead));
    k.up = unit_direction (body_up);
    k.right = unit_direction (body_right);
    k.forward = unit_direction (cross (body_right, body_up));

    // Pelvis height: dips as each foot strikes at a walk and at
    // mid-stance at a run, rides high through a run's flight, shifts over
    // the standing foot, and sinks into a landing.
    float height = ground (feet) + 0.94f - 0.05f * running;
    height += (1.0f - running) * gait * 0.024f * -std::cos (2.0f * PI2 * phase);
    height +=
      running * moving * 0.06f * -std::cos (2.0f * PI2 * (phase - duty * 0.5f));
    height +=
      0.004f * breath * (1.0f - moving) - meters_value (walker.landing_dip);
    const float shift = -moving * (1.0f - running) * 0.03f *
                          std::cos (PI2 * (phase - duty * 0.5f)) +
                        (1.0f - moving) * 0.012f * sway;
    // In the air the body is where the capsule is; the legs fold up.
    if (!walker.grounded)
      height = feet[1] + 0.94f;
    Vec3 pelvis = feet + r * (shift + 0.8f * std::sin (bank));

    // Never ask a planted leg for more than it has: the hips drop instead,
    // which is what a long stride does anyway.
    const float leg = meters_value ((size::thigh + size::shin) * 0.985f);
    if (walker.grounded)
      for (int i = 0; i < 2; ++i) {
        const Vec3 out = body_right * ((i == 0 ? -1.0f : 1.0f) *
                                       meters_value (size::hip_width));
        const Vec3& ankle = position_value (k.ankle[i]);
        const Vec3 gap = flat (ankle - pelvis - out);
        const float spread = length2 (gap);
        if (spread < leg * leg)
          height = std::min (
            height, ankle[1] - out[1] + std::sqrt (leg * leg - spread));
      }
    pelvis[1] = height;
    k.pelvis = position (pelvis);

    // Airborne legs: one knee drawn up and the other trailing on the way
    // up, both reaching for the ground on the way down.
    const float falling =
      ease (0.5f, -3.5f, speed_value (walker.vertical_speed));
    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      k.hip[i] = position (pelvis + body_right *
                                      (side * meters_value (size::hip_width)));
      if (air > 0.0f) {
        const float lead = i == 0 ? 0.22f : -0.14f;
        const float drop = i == 0 ? 0.48f : 0.74f;
        const Vec3 tucked =
          position_value (k.hip[i]) + f * mix (lead, lead * 0.3f, falling) -
          up * mix (drop, 0.84f, falling) + r * (side * 0.03f);
        k.ankle[i] = position (mix (position_value (k.ankle[i]), tucked, air));
        pitch[i] =
          pitch[i] + ((-0.45f + 0.3f * falling) * u::rad - pitch[i]) * air;
      }
      const Vec3 pole = sole_way[i] + r * (side * 0.15f);
      k.knee[i] = bend (k.hip[i], k.ankle[i], size::thigh, size::shin, pole);

      const Vec3& way = sole_way[i];
      Vec3 sole = way * libm::cos (pitch[i]) + up * libm::sin (pitch[i]);
      Vec3 instep = up * libm::cos (pitch[i]) - way * libm::sin (pitch[i]);
      if (hang[i] > 0.0f) {
        // Square to the shin, toes pointed a little.
        const Vec3 shin =
          normalized ((k.ankle[i] - k.knee[i]).numerical_value_in (si::metre));
        const Vec3 square = normalized (cross (shin, body_right));
        sole = normalized (mix (sole,
                                square * libm::cos (0.45f * u::rad) +
                                  shin * libm::sin (0.45f * u::rad),
                                hang[i]));
        instep = normalized (cross (sole, body_right));
      }
      const Vec3& ankle = position_value (k.ankle[i]);
      k.heel[i] = position (ankle - sole * 0.07f - instep * 0.07f);
      k.toe[i] = position (ankle + sole * 0.19f - instep * 0.07f);
    }

    // Spine: a runner leans into the pace, the shoulders counter-rotate
    // against the hips and keep level over the dropping hip, and the
    // chest rises with each breath. A look aside turns the head first
    // and then the chest, each as far as it goes.
    const radians_t lean = (mix (0.07f, 0.30f, running) * moving + 0.10f * air -
                            0.03f * air * falling) *
                           u::rad;
    const float aside = radians_value (walker.look_aside);
    const float head_aside = 1.0f * std::tanh (aside / 1.0f);
    const float chest_aside = 0.5f * std::tanh ((aside - head_aside) / 0.5f);
    const radians_t twist =
      (-moving * mix (0.12f, 0.20f, running) * std::cos (PI2 * phase) +
       chest_aside) *
      u::rad;
    const Vec3 turned = yawed (f, twist);
    const Vec3 chest_up =
      normalized (up + turned * libm::tan (lean) +
                  yawed (r, twist) * (bank + 0.3f * hip_drop));
    const Vec3 chest_right = normalized (cross (chest_up, turned));
    const Vec3 chest_forward = cross (chest_right, chest_up);
    const Vec3 waist = pelvis + body_up * 0.10f;
    const Vec3 neck = waist + chest_up * (0.47f + 0.003f * breath);
    k.chest_up = unit_direction (chest_up);
    k.chest_forward = unit_direction (chest_forward);
    k.waist = position (waist);
    k.chest = position (waist + chest_up * (0.27f + 0.004f * breath));
    k.neck = position (neck);

    // The head keeps its eyes on the horizon through the lean and the
    // stride and follows the walker's gaze.
    const radians_t nod =
      std::clamp (
        walker.look_pitch.numerical_value_in (u::rad) * 0.6f, -0.5f, 0.45f) *
        u::rad -
      lean * 0.6f;
    const Vec3 gaze = yawed (f, (head_aside + chest_aside) * u::rad);
    const Vec3 head_forward = gaze * libm::cos (nod) + up * libm::sin (nod);
    const Vec3 head_up = up * libm::cos (nod) - gaze * libm::sin (nod);
    k.head_forward = unit_direction (head_forward);
    k.head_up = unit_direction (head_up);
    k.head = position (neck + head_up * 0.15f + head_forward * 0.015f);

    // Arms swing against the legs -- the right arm forward as the left
    // foot strikes, a beat behind it at a walk -- loosely, nearly
    // straight. A runner's arms are bent near square and pump, the hands
    // coming up and across in front and back past the hips.
    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      const Vec3 shoulder =
        neck - chest_up * 0.07f +
        chest_right * (side * meters_value (size::shoulder_width));
      k.shoulder[i] = position (shoulder);
      const radians_t swing =
        (moving * mix (0.42f, 0.75f, running) * side *
           std::cos (PI2 * (phase - mix (0.04f, 0.0f, running))) +
         moving * running * 0.15f + air * mix (0.55f, 0.25f, falling)) *
        u::rad;
      const radians_t spread =
        (0.12f - 0.04f * running * moving + 0.012f * breath * (1.0f - moving) +
         air * mix (0.35f, 0.7f, falling)) *
        u::rad;
      const Vec3 down = -chest_up;
      Vec3 arm = down * libm::cos (swing) + chest_forward * libm::sin (swing);
      arm =
        arm * libm::cos (spread) + chest_right * (side * libm::sin (spread));
      const Vec3 elbow = shoulder + arm * meters_value (size::upper_arm);
      k.elbow[i] = position (elbow);

      const radians_t fold =
        0.25f * u::rad + moving * mix (0.2f, 1.4f, running) * u::rad +
        std::max (0.0f * u::rad, swing) * mix (0.5f, 0.35f, running) +
        air * 0.5f * u::rad;
      const Vec3 across =
        chest_forward - chest_right * (side * 0.35f * running * moving);
      Vec3 ahead = across - arm * dot (across, arm);
      ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : chest_forward;
      const Vec3 forearm = arm * libm::cos (fold) + ahead * libm::sin (fold);
      k.wrist[i] = position (elbow + forearm * meters_value (size::forearm));
    }

    // Hauling, the right hand holds the rope out toward where it is tied.
    if (walker.rope_end) {
      const Vec3& shoulder = position_value (k.shoulder[1]);
      const Vec3 toward = position_value (*walker.rope_end) - shoulder;
      const float distance = length (toward);
      const float reach =
        meters_value (0.95f * (size::upper_arm + size::forearm));
      if (distance > 1e-3f) {
        position_t hand = position (
          shoulder + toward * (std::min (distance, reach) / distance));
        k.elbow[1] = bend (k.shoulder[1],
                           hand,
                           size::upper_arm,
                           size::forearm,
                           chest_right * 0.5f - chest_up);
        k.wrist[1] = hand;
      }
    }

    // Fishing, the right hand holds the rod out before the body, or up
    // by the shoulder as it swings back to cast.
    if (walker.rod) {
      position_t hand = rod_hand (k, walker.rod->raise);
      k.elbow[1] = bend (k.shoulder[1],
                         hand,
                         size::upper_arm,
                         size::forearm,
                         chest_right * 0.6f - chest_up);
      k.wrist[1] = hand;
    }
    return k;
  }

  namespace {
    struct Trunk {
      direction_t right, chest_up, chest_forward, chest_right;
      position_t waist, chest, neck;
    };

    Trunk hold_trunk (const HoldPose& h) {
      const Vec3& up = direction_value (h.up);
      const Vec3& facing = direction_value (h.facing);
      const Vec3 chest_up =
        up * libm::cos (h.lean) + facing * libm::sin (h.lean);
      const Vec3 chest_forward =
        facing * libm::cos (h.lean) - up * libm::sin (h.lean);
      const position_t waist =
        avatar_pose::moved (h.pelvis, h.up, 0.10f * u::m);
      return {
        .right = unit_direction (normalized (cross (up, facing))),
        .chest_up = unit_direction (chest_up),
        .chest_forward = unit_direction (chest_forward),
        .chest_right =
          unit_direction (normalized (cross (chest_up, chest_forward))),
        .waist = waist,
        .chest =
          avatar_pose::moved (waist, unit_direction (chest_up), 0.27f * u::m),
        .neck =
          avatar_pose::moved (waist, unit_direction (chest_up), 0.47f * u::m),
      };
    }

    position_t shoulder_of (const Trunk& t, int side) {
      return avatar_pose::moved (
        avatar_pose::moved (t.neck, t.chest_up, -0.07f * u::m),
        t.chest_right,
        (side == 0 ? -1.0f : 1.0f) * avatar_size::shoulder_width);
    }
  }

  position_t held_shoulder (const HoldPose& hold, int side) {
    return shoulder_of (hold_trunk (hold), side);
  }

  AvatarSkeleton pose_holding (const HoldPose& h) {
    using avatar_pose::bend;
    using avatar_pose::moved;
    namespace size = avatar_size;
    const Trunk t = hold_trunk (h);
    AvatarSkeleton k;
    k.forward = h.facing;
    k.right = t.right;
    k.up = h.up;
    k.chest_up = t.chest_up;
    k.chest_forward = t.chest_forward;
    k.pelvis = h.pelvis;
    k.waist = t.waist;
    k.chest = t.chest;
    k.neck = t.neck;

    const Vec3 gaze = normalized (h.gaze);
    const Vec3 head_up = h.head_up - gaze * dot (h.head_up, gaze);
    k.head_forward = unit_direction (gaze);
    k.head_up = length2 (head_up) > 1e-6f
                  ? unit_direction (normalized (head_up))
                  : t.chest_up;
    k.head = moved (
      moved (k.neck, k.head_up, 0.15f * u::m), k.head_forward, 0.015f * u::m);

    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      k.shoulder[i] = shoulder_of (t, i);
      k.wrist[i] = h.wrist[i];
      k.elbow[i] = bend (k.shoulder[i],
                         k.wrist[i],
                         size::upper_arm,
                         size::forearm,
                         h.elbow_pole[i]);

      k.hip[i] = moved (h.pelvis, t.right, side * size::hip_width);
      k.ankle[i] = h.ankle[i];
      k.knee[i] =
        bend (k.hip[i], k.ankle[i], size::thigh, size::shin, h.knee_pole[i]);
      k.heel[i] = moved (
        moved (k.ankle[i], h.sole, -0.07f * u::m), h.instep, -0.07f * u::m);
      k.toe[i] = moved (
        moved (k.ankle[i], h.sole, 0.19f * u::m), h.instep, -0.07f * u::m);
    }
    return k;
  }
}
