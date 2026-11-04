#include <moppe/game/animal_render.hh>
#include <moppe/game/cow_model.hh>
#include <moppe/game/horse_model.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/moose_model.hh>
#include <moppe/game/pig_model.hh>
#include <moppe/game/reindeer_model.hh>
#include <moppe/game/roe_deer_model.hh>
#include <moppe/game/sheep_model.hh>
#include <moppe/gfx/mat4.hh>

#include <array>
#include <cmath>

namespace moppe::game {
  namespace {
    namespace animal_drawing {
      constexpr float animal_pi = 3.14159265f;

      // One species' parts and the pivots they swing about.
      struct Build {
        const model_mesh::Mesh* body;
        const model_mesh::Mesh* neck;
        const model_mesh::Mesh* head;
        const model_mesh::Mesh* fore_upper;
        const model_mesh::Mesh* fore_lower;
        const model_mesh::Mesh* hind_upper;
        const model_mesh::Mesh* hind_lower;
        const model_mesh::Mesh* tail;
        Vec3 withers, poll, shoulder, carpus, fore_hoof, hip, hock, hind_hoof,
          tail_root, muzzle;
      };

#define MOPPE_ANIMAL_BUILD(ns)                                                 \
  Build {                                                                      \
    &ns::body, &ns::neck, &ns::head, &ns::fore_upper, &ns::fore_lower,         \
      &ns::hind_upper, &ns::hind_lower, &ns::tail, ns::withers, ns::poll,      \
      ns::shoulder, ns::carpus, ns::fore_hoof, ns::hip, ns::hock,              \
      ns::hind_hoof, ns::tail_root, ns::muzzle                                 \
  }

      const Build& build (Species species) {
        static const std::array<Build, species_count> builds = {
          MOPPE_ANIMAL_BUILD (horse_model), MOPPE_ANIMAL_BUILD (cow_model),
          MOPPE_ANIMAL_BUILD (sheep_model), MOPPE_ANIMAL_BUILD (reindeer_model),
          MOPPE_ANIMAL_BUILD (moose_model), MOPPE_ANIMAL_BUILD (roe_deer_model),
          MOPPE_ANIMAL_BUILD (pig_model),
        };
        return builds[static_cast<std::size_t> (species)];
      }
#undef MOPPE_ANIMAL_BUILD

      Mat4 turn (float radians, const Vec3& axis) {
        return Mat4::rotation (radians * u::rad, axis);
      }

      const Vec3 x_axis (1, 0, 0), y_axis (0, 1, 0);

      // How far the neck swings down, and the head with it, for the
      // muzzle to reach the grass (a moose, browsing, reaches the
      // shrubs): found once for each species.
      struct Reach {
        float neck = 0.0f;
        float head = 0.0f;
      };

      Vec3 muzzle_at (const Build& b, float neck, float head) {
        const Mat4 m = Mat4::translation (b.withers) * turn (neck, x_axis) *
                       Mat4::translation (b.poll - b.withers) *
                       turn (head, x_axis);
        return m.transform_point (b.muzzle - b.poll);
      }

      const Reach& reach_down (Species species) {
        static const std::array<Reach, species_count> reaches = [] {
          std::array<Reach, species_count> out {};
          for (int s = 0; s < species_count; ++s) {
            const Species kind = static_cast<Species> (s);
            const Build& b = build (kind);
            const float target = kind == Species::moose ? 0.55f : 0.03f;
            // Which way of turning about x lowers the nose.
            const float sign =
              muzzle_at (b, 0.2f, 0.0f)[1] < b.muzzle[1] ? 1.0f : -1.0f;
            // The head hangs nearly straight down from the poll, its
            // muzzle a little ahead, and the neck goes down until the
            // muzzle meets the grass.
            const Vec3 face = b.muzzle - b.poll;
            const float below = std::atan2 (-face[1], face[2]);
            const float hang = 1.3f - below;
            for (float a = 0.0f; a < 2.2f; a += 0.002f)
              if (muzzle_at (b, sign * a, sign * (hang - a))[1] <= target) {
                out[s] = { sign * a, sign * (hang - a) };
                break;
              }
          }
          return out;
        }();
        return reaches[static_cast<std::size_t> (species)];
      }

      // Which way of turning about x swings a leg's hoof forward.
      float forward_sign (const Build& b) {
        const Vec3 hoof = b.fore_hoof - b.shoulder;
        return turn (0.2f, x_axis).transform_vector (hoof)[2] > hoof[2] ? 1.0f
                                                                        : -1.0f;
      }

      struct LegPose {
        float upper = 0.0f;
        float lower = 0.0f;
      };

      // A leg at `phase` (0..1) of its stride: planted and drawn back
      // through its stance, then lifted, folded, and swung forward.
      LegPose leg_at (float phase, float duty, float swing, float fold) {
        LegPose leg;
        phase -= std::floor (phase);
        if (phase < duty) {
          const float u = phase / duty;
          leg.upper = swing * (1.0f - 2.0f * u);
        } else {
          const float u = (phase - duty) / (1.0f - duty);
          leg.upper = swing * (-std::cos (animal_pi * u));
          leg.lower = fold * std::sin (animal_pi * u);
        }
        return leg;
      }

      struct Pose {
        Mat4 frame;
        Mat4 neck;
        Mat4 head;
        Mat4 tail;
        // Left fore, right fore, left hind, right hind: upper and lower.
        std::array<Mat4, 4> upper;
        std::array<Mat4, 4> lower;
      };

      Pose pose (const Animal& a,
                 const std::function<float (float, float)>& ground,
                 float time) {
        const Build& b = build (a.species);
        const SpeciesTraits& t = traits (a.species);
        const Reach& down = reach_down (a.species);
        const float fwd = forward_sign (b);

        // Set on the ground under its fore and hind hooves.
        const Vec3 facing (std::sin (a.heading), 0.0f, std::cos (a.heading));
        const float fore_z = b.fore_hoof[2], hind_z = b.hind_hoof[2];
        const Vec3 fore = a.position + facing * fore_z;
        const Vec3 hind = a.position + facing * hind_z;
        const float fore_y = ground (fore[0], fore[2]);
        const float hind_y = ground (hind[0], hind[2]);
        const float rise = (fore_y - hind_y) / (fore_z - hind_z);
        const Vec3 forward = normalized (facing + Vec3 (0.0f, rise, 0.0f));
        const Vec3 left = normalized (cross (Vec3 (0, 1, 0), forward));
        const Vec3 up = cross (forward, left);

        // The gait: a walk's four beats, a trot's diagonal pairs, or the
        // roe deer's bound, by how fast it goes.
        const float pace = std::clamp (a.speed / t.walk, 0.0f, 1.0f);
        const bool running = a.speed > 1.6f * t.walk;
        float duty = 0.62f, swing = 0.26f * pace, fold = 0.9f * pace;
        std::array<float, 4> offset = { 0.25f, 0.75f, 0.0f, 0.5f };
        float bob = 0.0f, rock = 0.0f;
        const float run_share = std::clamp ((a.speed - 1.6f * t.walk) /
                                              (t.run - 1.6f * t.walk + 0.01f),
                                            0.0f,
                                            1.0f);
        if (running) {
          if (t.bounds) {
            offset = { 0.0f, 0.06f, 0.5f, 0.56f };
            duty = 0.32f;
            swing = 0.6f;
            fold = 1.3f;
            rock = 0.12f * std::sin (2.0f * animal_pi * a.gait);
            bob = 0.08f * std::sin (2.0f * animal_pi * a.gait) * t.length;
          } else {
            offset = { 0.0f, 0.5f, 0.5f, 0.0f };
            duty = 0.42f;
            swing = 0.42f + 0.1f * run_share;
            fold = 1.2f;
            bob = 0.025f * std::cos (4.0f * animal_pi * a.gait) * t.length;
          }
        }

        Pose p;
        p.frame =
          Mat4::translation (Vec3 (
            a.position[0], 0.5f * (fore_y + hind_y) + bob, a.position[2])) *
          Mat4::basis (left, up, forward) * turn (rock * fwd, x_axis);

        // The neck: down to the grass, up to look (a negative head), and
        // turned to look aside; the head works as it crops.
        const float lower = std::max (a.head, 0.0f);
        const float raise = std::min (a.head, 0.0f);
        const float neck = down.neck * lower + raise * 0.5f * down.neck;
        const float chew =
          lower > 0.9f ? 0.06f * std::sin (time * 6.5f + a.seed % 7) : 0.0f;
        const float head =
          down.head * lower - raise * 0.3f * down.head + chew * down.head;
        p.neck = p.frame * Mat4::translation (b.withers) *
                 turn (a.look * 0.8f, y_axis) * turn (neck, x_axis);
        p.head = p.neck * Mat4::translation (b.poll - b.withers) *
                 turn (a.look * 0.3f, y_axis) * turn (head, x_axis);

        // The tail swishes at the flies, and flags as it runs.
        const float swish =
          0.28f * std::sin (time * 1.9f + (a.seed & 63)) *
          (0.5f + 0.5f * std::sin (time * 0.37f + (a.seed & 15)));
        p.tail = p.frame * Mat4::translation (b.tail_root) *
                 turn (swish, y_axis) * turn (-fwd * 0.5f * run_share, x_axis);

        // The legs: the left's parts serve the right moved across.
        for (int leg = 0; leg < 4; ++leg) {
          const bool front = leg < 2;
          const float side = leg % 2 == 0 ? 0.0f : -2.0f;
          const Vec3 top = front ? b.shoulder : b.hip;
          const Vec3 joint = front ? b.carpus : b.hock;
          const LegPose l = leg_at (a.gait + offset[leg], duty, swing, fold);
          // The foreleg folds its hoof back at the knee, the hind leg
          // forward at the hock.
          const float bend = front ? -fwd * l.lower : fwd * 0.7f * l.lower;
          const Mat4 at =
            p.frame * Mat4::translation (top + Vec3 (side * top[0], 0, 0));
          p.upper[leg] = at * turn (fwd * l.upper, x_axis);
          p.lower[leg] = p.upper[leg] * Mat4::translation (joint - top) *
                         turn (bend, x_axis);
        }
        return p;
      }

      struct Meshes {
        std::array<render::MeshPtr, 8> part;
      };

      const std::array<Meshes, species_count>& meshes (render::Renderer& r) {
        static const std::array<Meshes, species_count> all = [&r] {
          std::array<Meshes, species_count> out;
          for (int s = 0; s < species_count; ++s) {
            const Build& b = build (static_cast<Species> (s));
            const model_mesh::Mesh* parts[8] = { b.body,       b.neck,
                                                 b.head,       b.tail,
                                                 b.fore_upper, b.fore_lower,
                                                 b.hind_upper, b.hind_lower };
            for (int k = 0; k < 8; ++k) {
              render::DrawList dl;
              model_mesh::record (dl, *parts[k]);
              out[s].part[k] = r.create_mesh (dl);
            }
          }
          return out;
        }();
        return all;
      }
    }
  }
  void render_animals (render::Renderer& r,
                       std::span<const Animal> animals,
                       const std::function<float (float, float)>& ground,
                       const Vec3& camera,
                       const Vec3& forward,
                       float time,
                       float reach,
                       std::uint64_t motion_base) {
    namespace ad = animal_drawing;
    const auto& all = ad::meshes (r);
    for (std::size_t i = 0; i < animals.size (); ++i) {
      const Animal& a = animals[i];
      const Vec3 to = a.position - camera;
      const float d2 = length2 (to);
      if (d2 > reach * reach)
        continue;
      // Behind the camera, by more than its own size.
      const float size = 2.0f * traits (a.species).length;
      if (dot (to, forward) < -size)
        continue;
      const ad::Pose p = ad::pose (a, ground, time);
      const ad::Meshes& m = all[static_cast<std::size_t> (a.species)];
      std::uint64_t id = motion_base ? motion_base + i * 16 : 0;
      const auto draw = [&] (int part, const Mat4& at) {
        r.draw_mesh (*m.part[part], at, id ? ++id : 0);
      };
      draw (0, p.frame);
      draw (1, p.neck);
      draw (2, p.head);
      draw (3, p.tail);
      for (int leg = 0; leg < 4; ++leg) {
        const bool front = leg < 2;
        draw (front ? 4 : 6, p.upper[leg]);
        draw (front ? 5 : 7, p.lower[leg]);
      }
    }
  }

  Vec3 animal_muzzle (const Animal& a,
                      const std::function<float (float, float)>& ground) {
    namespace ad = animal_drawing;
    const ad::Build& b = ad::build (a.species);
    return ad::pose (a, ground, 0.0f).head.transform_point (b.muzzle - b.poll);
  }
}
