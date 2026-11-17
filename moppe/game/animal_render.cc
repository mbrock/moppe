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
#include <moppe/terrain/torus.hh>

#include <array>
#include <cmath>

namespace moppe::game {
  namespace {
    namespace animal_drawing {
      constexpr radians_t half_turn = 3.14159265f * u::rad;

      // Shares of a stride: where a leg is in its cycle, how long its
      // hoof is down.
      constexpr auto of_stride = proportion[one];

      // One species' parts and the pivots they swing about, as offsets
      // from the model's origin.
      struct Build {
        const model_mesh::Mesh* body;
        const model_mesh::Mesh* neck;
        const model_mesh::Mesh* head;
        const model_mesh::Mesh* fore_upper;
        const model_mesh::Mesh* fore_lower;
        const model_mesh::Mesh* hind_upper;
        const model_mesh::Mesh* hind_lower;
        const model_mesh::Mesh* tail;
        displacement_t withers, poll, shoulder, carpus, fore_hoof, hip, hock,
          hind_hoof, tail_root, muzzle;
      };

#define MOPPE_ANIMAL_BUILD(ns)                                                 \
  Build {                                                                      \
    &ns::body, &ns::neck, &ns::head, &ns::fore_upper, &ns::fore_lower,         \
      &ns::hind_upper, &ns::hind_lower, &ns::tail, displacement (ns::withers), \
      displacement (ns::poll), displacement (ns::shoulder),                    \
      displacement (ns::carpus), displacement (ns::fore_hoof),                 \
      displacement (ns::hip), displacement (ns::hock),                         \
      displacement (ns::hind_hoof), displacement (ns::tail_root),              \
      displacement (ns::muzzle)                                                \
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

      Mat4 turn (radians_t angle, const direction_t& axis) {
        return Mat4::rotation (angle, direction_value (axis));
      }

      Mat4 translation (const displacement_t& offset) {
        return Mat4::translation (displacement_value (offset));
      }

      const direction_t x_axis = unit_direction (Vec3 (1, 0, 0)),
                        y_axis = unit_direction (Vec3 (0, 1, 0));

      // How far the neck swings down, and the head with it, for the
      // muzzle to reach the grass (a moose, browsing, reaches the
      // shrubs): found once for each species.
      struct Reach {
        radians_t neck = 0.0f * u::rad;
        radians_t head = 0.0f * u::rad;
      };

      displacement_t
      muzzle_at (const Build& b, radians_t neck, radians_t head) {
        const Mat4 m = translation (b.withers) * turn (neck, x_axis) *
                       translation (b.poll - b.withers) * turn (head, x_axis);
        return displacement (
          m.transform_point (displacement_value (b.muzzle - b.poll)));
      }

      // How high an offset reaches above the model's origin.
      meters_t height_of (const displacement_t& offset) {
        return displacement_value (offset)[1] * u::m;
      }

      const Reach& reach_down (Species species) {
        static const std::array<Reach, species_count> reaches = [] {
          std::array<Reach, species_count> out {};
          for (int s = 0; s < species_count; ++s) {
            const Species kind = static_cast<Species> (s);
            const Build& b = build (kind);
            const meters_t target =
              kind == Species::moose ? 0.55f * u::m : 0.03f * u::m;
            // Which way of turning about x lowers the nose.
            const int sign =
              height_of (muzzle_at (b, 0.2f * u::rad, 0.0f * u::rad)) <
                  height_of (b.muzzle)
                ? 1
                : -1;
            // The head hangs nearly straight down from the poll, its
            // muzzle a little ahead, and the neck goes down until the
            // muzzle meets the grass.
            const displacement_t face = b.muzzle - b.poll;
            const radians_t below = std::atan2 (-displacement_value (face)[1],
                                                displacement_value (face)[2]) *
                                    u::rad;
            const radians_t hang = 1.3f * u::rad - below;
            for (radians_t a = 0.0f * u::rad; a < 2.2f * u::rad;
                 a += 0.002f * u::rad)
              if (height_of (muzzle_at (b, sign * a, sign * (hang - a))) <=
                  target) {
                out[s] = { sign * a, sign * (hang - a) };
                break;
              }
          }
          return out;
        }();
        return reaches[static_cast<std::size_t> (species)];
      }

      // Which way of turning about x swings a leg's hoof forward.
      int forward_sign (const Build& b) {
        const displacement_t hoof = b.fore_hoof - b.shoulder;
        return turn (0.2f * u::rad, x_axis)
                     .transform_vector (displacement_value (hoof))[2] >
                   displacement_value (hoof)[2]
                 ? 1
                 : -1;
      }

      struct LegPose {
        radians_t upper = 0.0f * u::rad;
        radians_t lower = 0.0f * u::rad;
      };

      // A leg at `phase` (0..1) of its stride: planted and drawn back
      // through its stance, then lifted, folded, and swung forward.
      LegPose leg_at (proportion_t phase,
                      proportion_t duty,
                      radians_t swing,
                      radians_t fold) {
        LegPose leg;
        phase -= std::floor (phase.numerical_value_in (one)) * of_stride;
        if (phase < duty) {
          const proportion_t through = quantity_cast<proportion> (phase / duty);
          leg.upper = swing * (1.0f * one - 2.0f * through);
        } else {
          const proportion_t through = quantity_cast<proportion> (
            (phase - duty) / (1.0f * of_stride - duty));
          leg.upper = swing * (-std::cos (radians_value (half_turn * through)));
          leg.lower = fold * std::sin (radians_value (half_turn * through));
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
                 const terrain::ElevationField& ground,
                 seconds_t time) {
        const Build& b = build (a.species);
        const SpeciesTraits& t = traits (a.species);
        const Reach& down = reach_down (a.species);
        const int fwd = forward_sign (b);

        // What the simulation keeps as floats, read once.
        const position_t here = position (a.position);
        const heading_t bearing = a.heading * heading[u::rad];
        const proportion_t gait = a.gait * of_stride;
        const proportion_t head_down = a.head * proportion[one];
        const radians_t look = a.look * u::rad;

        // Set on the ground under its fore and hind hooves.
        const direction_t facing =
          unit_direction (Vec3 (std::sin (radians_value (bearing)),
                                0.0f,
                                std::cos (radians_value (bearing))));
        const meters_t fore_z = displacement_value (b.fore_hoof)[2] * u::m,
                       hind_z = displacement_value (b.hind_hoof)[2] * u::m;
        const position_t fore =
          here + quantity_cast<isq::position_vector> (along (facing, fore_z));
        const position_t hind =
          here + quantity_cast<isq::position_vector> (along (facing, hind_z));
        const meters_t fore_y =
          terrain::surface_elevation_value (ground (plan (fore))) * u::m;
        const meters_t hind_y =
          terrain::surface_elevation_value (ground (plan (hind))) * u::m;
        const grade_t rise =
          quantity_cast<grade> ((fore_y - hind_y) / (fore_z - hind_z));
        const direction_t forward = unit_direction (
          normalized (direction_value (facing) +
                      Vec3 (0.0f, rise.numerical_value_in (one), 0.0f)));
        const direction_t left = unit_direction (normalized (
          cross (direction_value (y_axis), direction_value (forward))));
        const direction_t up = unit_direction (
          cross (direction_value (forward), direction_value (left)));

        // The gait: a walk's four beats, a trot's diagonal pairs, or the
        // roe deer's bound, by how fast it goes.
        const proportion_t pace =
          std::clamp ((a.speed / t.walk).numerical_value_in (one), 0.0f, 1.0f) *
          proportion[one];
        const bool running = a.speed > 1.6f * t.walk;
        proportion_t duty = 0.62f * of_stride;
        radians_t swing = 0.26f * u::rad * pace, fold = 0.9f * u::rad * pace;
        std::array<proportion_t, 4> offset = { 0.25f * of_stride,
                                               0.75f * of_stride,
                                               0.0f * of_stride,
                                               0.5f * of_stride };
        meters_t bob = 0.0f * u::m;
        radians_t rock = 0.0f * u::rad;
        const proportion_t run_share =
          std::clamp (((a.speed - 1.6f * t.walk) /
                       (t.run - 1.6f * t.walk + 0.01f * u::m / u::s))
                        .numerical_value_in (one),
                      0.0f,
                      1.0f) *
          proportion[one];
        if (running) {
          if (t.bounds) {
            offset = { 0.0f * of_stride,
                       0.06f * of_stride,
                       0.5f * of_stride,
                       0.56f * of_stride };
            duty = 0.32f * of_stride;
            swing = 0.6f * u::rad;
            fold = 1.3f * u::rad;
            rock = 0.12f * u::rad *
                   std::sin (radians_value (2.0f * half_turn * gait));
            bob = 0.08f * std::sin (radians_value (2.0f * half_turn * gait)) *
                  t.length;
          } else {
            offset = { 0.0f * of_stride,
                       0.5f * of_stride,
                       0.5f * of_stride,
                       0.0f * of_stride };
            duty = 0.42f * of_stride;
            swing = 0.42f * u::rad + 0.1f * u::rad * run_share;
            fold = 1.2f * u::rad;
            bob = 0.025f * std::cos (radians_value (4.0f * half_turn * gait)) *
                  t.length;
          }
        }

        Pose p;
        p.frame = Mat4::translation (
                    Vec3 (position_value (here)[0],
                          meters_value (0.5f * (fore_y + hind_y) + bob),
                          position_value (here)[2])) *
                  Mat4::basis (direction_value (left),
                               direction_value (up),
                               direction_value (forward)) *
                  turn (rock * fwd, x_axis);

        // The neck: down to the grass, up to look (a negative head), and
        // turned to look aside; the head works as it crops, nodding by a
        // share of its reach either way.
        const proportion_t lower = std::max (head_down, 0.0f * proportion[one]);
        const proportion_t raise = std::min (head_down, 0.0f * proportion[one]);
        const radians_t neck = down.neck * lower + raise * 0.5f * down.neck;
        const proportion_t chew =
          lower > 0.9f * proportion[one]
            ? 0.06f *
                std::sin (radians_value (time * (6.5f * u::rad / u::s)) +
                          a.seed % 7) *
                proportion[one]
            : 0.0f * proportion[one];
        const radians_t head =
          down.head * lower - raise * 0.3f * down.head + chew * down.head;
        p.neck = p.frame * translation (b.withers) *
                 turn (look * 0.8f, y_axis) * turn (neck, x_axis);
        p.head = p.neck * translation (b.poll - b.withers) *
                 turn (look * 0.3f, y_axis) * turn (head, x_axis);

        // The tail swishes at the flies, and flags as it runs.
        const radians_t swish =
          0.28f * u::rad *
          std::sin (radians_value (time * (1.9f * u::rad / u::s)) +
                    (a.seed & 63)) *
          (0.5f +
           0.5f * std::sin (radians_value (time * (0.37f * u::rad / u::s)) +
                            (a.seed & 15)));
        p.tail = p.frame * translation (b.tail_root) * turn (swish, y_axis) *
                 turn (-fwd * (0.5f * u::rad) * run_share, x_axis);

        // The legs: the left's parts serve the right moved across.
        for (int leg = 0; leg < 4; ++leg) {
          const bool front = leg < 2;
          const int side = leg % 2 == 0 ? 0 : -2;
          const displacement_t top = front ? b.shoulder : b.hip;
          const displacement_t joint = front ? b.carpus : b.hock;
          const LegPose l = leg_at (gait + offset[leg], duty, swing, fold);
          // The foreleg folds its hoof back at the knee, the hind leg
          // forward at the hock.
          const radians_t bend = front ? -fwd * l.lower : fwd * 0.7f * l.lower;
          const Mat4 at =
            p.frame *
            translation (top + displacement (Vec3 (
                                 side * displacement_value (top)[0], 0, 0)));
          p.upper[leg] = at * turn (fwd * l.upper, x_axis);
          p.lower[leg] =
            p.upper[leg] * translation (joint - top) * turn (bend, x_axis);
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
            for (int k = 0; k < 8; ++k)
              out[s].part[k] = model_mesh::bake (r, *parts[k]);
          }
          return out;
        }();
        return all;
      }
    }
  }
  void render_animals (render::Renderer& r,
                       std::span<const Animal> animals,
                       const terrain::ElevationField& ground,
                       const position_t& camera,
                       const direction_t& forward,
                       seconds_t time,
                       meters_t reach,
                       const plan_extent_t& period,
                       std::uint64_t motion_base) {
    namespace ad = animal_drawing;
    const auto& all = ad::meshes (r);
    for (std::size_t i = 0; i < animals.size (); ++i) {
      const Animal& a = animals[i];
      const position_t here = position (a.position);
      const plan_displacement_t seam = plan (camera) - plan (here);
      const displacement_t lap =
        displacement (Vec3 (terrain::whole_periods (plan_value (seam).x (),
                                                    plan_value (period).x ()),
                            0.0f,
                            terrain::whole_periods (plan_value (seam).z (),
                                                    plan_value (period).z ())));
      const auto to = here + quantity_cast<isq::position_vector> (lap) - camera;
      if (dot (to, to) > reach * reach)
        continue;
      // Behind the camera, by more than its own size.
      const meters_t size = 2.0f * traits (a.species).length;
      if (dot (to, forward) < -size)
        continue;
      const ad::Pose p = ad::pose (a, ground, time);
      const ad::Meshes& m = all[static_cast<std::size_t> (a.species)];
      std::uint64_t id = motion_base ? motion_base + i * 16 : 0;
      const auto draw = [&] (int part, const Mat4& at) {
        r.draw_mesh (*m.part[part], ad::translation (lap) * at, id ? ++id : 0);
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

  position_t animal_muzzle (const Animal& a,
                            const terrain::ElevationField& ground) {
    namespace ad = animal_drawing;
    const ad::Build& b = ad::build (a.species);
    return position (
      ad::pose (a, ground, 0.0f * u::s)
        .head.transform_point (displacement_value (b.muzzle - b.poll)));
  }
}
