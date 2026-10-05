#include <moppe/game/avatar.hh>

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

    // Two-bone inverse kinematics: the middle joint of a limb of lengths
    // `a` and `b` reaching from `root` toward `target`, bent toward `pole`.
    // An out-of-reach target is pulled in to what the limb can touch.
    Vec3
    bend (const Vec3& root, Vec3& target, float a, float b, const Vec3& pole) {
      Vec3 reach = target - root;
      float distance = length (reach);
      const float longest = (a + b) * 0.999f;
      const float shortest = std::abs (a - b) + 0.01f;
      if (distance < 1e-5f) {
        reach = Vec3 (0, -1, 0);
        distance = 1e-5f;
      }
      const Vec3 axis = reach * (1.0f / distance);
      distance = std::clamp (distance, shortest, longest);
      target = root + axis * distance;
      const float along =
        (a * a + distance * distance - b * b) / (2.0f * distance);
      const float out = std::sqrt (std::max (0.0f, a * a - along * along));
      Vec3 side = pole - axis * dot (pole, axis);
      if (length2 (side) < 1e-8f)
        side = Vec3 (0, 0, 1) - axis * axis[2];
      normalize (side);
      return root + axis * along + side * out;
    }
  }

  AvatarSkeleton pose_avatar (const WalkerPose& walker, float time) {
    using namespace avatar_pose;
    namespace size = avatar_size;
    AvatarSkeleton k;

    const Vec3 up (0, 1, 0);
    Vec3 f = flat (walker.heading);
    f = length2 (f) > 1e-6f ? normalized (f) : Vec3 (0, 0, 1);
    const Vec3 r = normalized (cross (up, f));
    k.forward = f;
    k.right = r;
    k.up = up;

    // Gait weights: how much the body moves at all, how much of that is a
    // run, and how far into the air it is.
    // The stride speed holds its last value through the air, so a jump
    // folds the legs out of the gait they left the ground in.
    const float speed = walker.stride_speed;
    const float moving = ease (0.05f, 0.9f, speed);
    const float running = ease (4.2f, 7.0f, speed);
    const float air =
      walker.grounded ? 0.0f : ease (0.0f, 0.15f, walker.airborne_seconds);
    const float phase = walker.stride_phase;
    const float breath = std::sin (time * 1.7f);
    const float sway = std::sin (time * 0.6f);

    // A walk keeps a foot down most of the cycle; a run spends most of it
    // in flight. Each foot is planted for duty * cycle metres -- exactly
    // the ground the stride phase covers while it is down.
    const float duty =
      mix (0.62f - 0.03f * std::min (speed, 3.2f), 0.34f, running);
    const float stance =
      duty * stride_cycle_length (std::max (speed, 0.5f)) * moving;
    const float lift = mix (0.11f, 0.30f, running) * moving;
    Vec3 travel = flat (walker.velocity);
    travel = length2 (travel) > 0.04f ? normalized (travel) : f;

    // Heights on the ground plane the walker stands on.
    const Vec3& feet = walker.position;
    Vec3 n = walker.ground_normal;
    if (n[1] < 0.5f)
      n = Vec3 (0, 1, 0);
    const auto ground = [&] (const Vec3& at) {
      return feet[1] -
             (n[0] * (at[0] - feet[0]) + n[2] * (at[2] - feet[2])) / n[1];
    };

    // Feet: a planted foot moves backward under the body at ground speed;
    // a swinging foot arcs forward and lands toe-up on its heel.
    float pitch[2];
    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      const float t = std::fmod (phase + (i == 0 ? 0.0f : 0.5f), 1.0f);
      float along, raise = 0.0f, toe_up, heel_up;
      if (t < duty) {
        const float u = t / duty;
        along = stance * (0.5f - u);
        toe_up = moving * 0.30f * (1.0f - ease (0.0f, 0.2f, u));
        heel_up = moving * mix (0.45f, 0.75f, running) * ease (0.55f, 1.0f, u);
      } else {
        const float u = (t - duty) / (1.0f - duty);
        along = stance * (ease (0.0f, 1.0f, u) - 0.5f);
        // A runner's heel kicks up early and high behind; a walker's foot
        // just clears the ground.
        const float arc =
          std::sin (PI * std::pow (u, mix (1.0f, 0.65f, running)));
        raise = lift * arc;
        toe_up = moving * 0.30f * ease (0.65f, 1.0f, u);
        heel_up =
          moving * mix (0.75f, 1.1f, running) * (1.0f - ease (0.0f, 0.45f, u));
      }
      Vec3 base = feet + travel * along + r * (side * 0.11f);
      base[1] = ground (base);
      k.ankle[i] =
        base + up * (size::ankle_height + raise + 0.14f * std::sin (heel_up) +
                     0.05f * std::sin (toe_up));
      pitch[i] = toe_up - heel_up;
    }

    // Pelvis: dips as each foot strikes at a walk, at mid-stance at a run,
    // shifts over the standing foot, and sinks into a landing.
    float height = ground (feet) + 0.94f - 0.06f * running;
    height +=
      (1.0f - running) * moving * 0.022f * -std::cos (2.0f * PI2 * phase);
    height += running * 0.045f * -std::cos (2.0f * PI2 * (phase - duty * 0.5f));
    height += 0.004f * breath * (1.0f - moving) - walker.landing_dip;
    const float shift = -moving * (1.0f - running) * 0.025f *
                          std::cos (PI2 * (phase - duty * 0.5f)) +
                        (1.0f - moving) * 0.012f * sway;
    // In the air the body is where the capsule is; the legs fold up.
    if (!walker.grounded)
      height = feet[1] + 0.94f;
    Vec3 pelvis = feet + r * shift;

    // Never ask a planted leg for more than it has: the hips drop instead,
    // which is what a long stride does anyway.
    const float leg = (size::thigh + size::shin) * 0.985f;
    if (walker.grounded)
      for (int i = 0; i < 2; ++i) {
        const Vec3 hip =
          pelvis + r * ((i == 0 ? -1.0f : 1.0f) * size::hip_width);
        const Vec3 gap = flat (k.ankle[i] - hip);
        const float spread = length2 (gap);
        if (spread < leg * leg)
          height =
            std::min (height, k.ankle[i][1] + std::sqrt (leg * leg - spread));
      }
    pelvis[1] = height;
    k.pelvis = pelvis;

    // Airborne legs: one knee drawn up and the other trailing on the way
    // up, both reaching for the ground on the way down.
    const float falling = ease (0.5f, -3.5f, walker.vertical_speed);
    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      k.hip[i] = pelvis + r * (side * size::hip_width);
      if (air > 0.0f) {
        const float lead = i == 0 ? 0.22f : -0.14f;
        const float drop = i == 0 ? 0.48f : 0.74f;
        const Vec3 tucked = k.hip[i] + f * mix (lead, lead * 0.3f, falling) -
                            up * mix (drop, 0.84f, falling) +
                            r * (side * 0.03f);
        k.ankle[i] = mix (k.ankle[i], tucked, air);
        pitch[i] = mix (pitch[i], -0.45f + 0.3f * falling, air);
      }
      const Vec3 pole = f + r * (side * 0.15f);
      k.knee[i] = bend (k.hip[i], k.ankle[i], size::thigh, size::shin, pole);

      const Vec3 sole = f * std::cos (pitch[i]) + up * std::sin (pitch[i]);
      const Vec3 instep = up * std::cos (pitch[i]) - f * std::sin (pitch[i]);
      k.heel[i] = k.ankle[i] - sole * 0.07f - instep * 0.07f;
      k.toe[i] = k.ankle[i] + sole * 0.19f - instep * 0.07f;
    }

    // Spine: a runner leans into the pace, the shoulders counter-rotate
    // against the hips, and the chest rises with each breath.
    const float lean = mix (0.08f, 0.22f, running) * moving + 0.10f * air -
                       0.03f * air * falling;
    const float twist =
      moving * mix (0.10f, 0.18f, running) * std::cos (PI2 * phase);
    const Vec3 turned = f * std::cos (twist) - r * std::sin (twist);
    k.chest_up = up * std::cos (lean) + f * std::sin (lean);
    k.chest_forward =
      normalized (turned * std::cos (lean) - up * std::sin (lean));
    const Vec3 chest_right = normalized (cross (k.chest_up, k.chest_forward));
    k.waist = pelvis + up * 0.10f;
    k.chest = k.waist + k.chest_up * (0.27f + 0.004f * breath);
    k.neck = k.waist + k.chest_up * (0.47f + 0.003f * breath);

    // The head keeps its eyes on the horizon through the lean and follows
    // the walker's gaze a little.
    const float nod =
      std::clamp (walker.look_pitch * 0.6f, -0.5f, 0.45f) - lean * 0.6f;
    k.head_forward = f * std::cos (nod) + up * std::sin (nod);
    k.head_up = up * std::cos (nod) - f * std::sin (nod);
    k.head = k.neck + k.head_up * 0.15f + k.head_forward * 0.015f;

    // Arms swing against the legs -- the right arm forward as the left
    // foot strikes -- with elbows that fold further the faster the pace.
    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      k.shoulder[i] = k.neck - k.chest_up * 0.07f +
                      chest_right * (side * size::shoulder_width);
      const float swing =
        moving * mix (0.50f, 0.85f, running) * side * std::cos (PI2 * phase) +
        air * mix (0.55f, 0.25f, falling);
      const float spread = 0.12f + 0.012f * breath * (1.0f - moving) +
                           air * mix (0.35f, 0.7f, falling);
      const Vec3 down = -k.chest_up;
      Vec3 arm = down * std::cos (swing) + k.chest_forward * std::sin (swing);
      arm = arm * std::cos (spread) + chest_right * (side * std::sin (spread));
      k.elbow[i] = k.shoulder[i] + arm * size::upper_arm;

      const float fold = 0.22f + moving * mix (0.25f, 1.45f, running) +
                         std::max (0.0f, swing) * 0.4f + air * 0.5f;
      Vec3 ahead = k.chest_forward - arm * dot (k.chest_forward, arm);
      ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : k.chest_forward;
      const Vec3 forearm = arm * std::cos (fold) + ahead * std::sin (fold);
      k.wrist[i] = k.elbow[i] + forearm * size::forearm;
    }
    return k;
  }
}
