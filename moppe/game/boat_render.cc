#include <moppe/game/avatar.hh>
#include <moppe/game/boat_model.hh>
#include <moppe/game/boat_render.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/fishing.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace moppe::game {
  namespace {
    // The engine is a unity build; the other machines' drawing has
    // helpers of the same names.
    namespace boat_drawing {
      struct BoatMeshes {
        render::MeshPtr hull;
        render::MeshPtr longtail;
        render::MeshPtr propeller;
      };

      const BoatMeshes& boat_meshes (render::Renderer& r) {
        static const BoatMeshes meshes = [&r] {
          BoatMeshes m;
          m.hull = model_mesh::bake (r, boat_model::hull);
          m.longtail = model_mesh::bake (r, boat_model::longtail);
          m.propeller = model_mesh::bake (r, boat_model::propeller);
          return m;
        }();
        return meshes;
      }

      // The long-tail's frame about its pivot: swung about the boat's up
      // after being tipped about its left (mov::Boat::tail_point).
      Mat4 tail_frame (const BoatPose& boat, const Mat4& frame) {
        return frame * Mat4::translation (boat_model::pivot) *
               Mat4::rotation (boat.swing, Vec3 (0, 1, 0)) *
               Mat4::rotation (boat.tilt, Vec3 (1, 0, 0));
      }

      // The helmsman on the after seat, the right hand on the tiller's
      // grip, the left on the knee, feet on the sole; fishing, the hand
      // from the knee holds the rod out and the head turns to the float.
      AvatarSkeleton pose_helmsman (const BoatPose& boat,
                                    const Mat4& frame,
                                    const Mat4& tail,
                                    const std::optional<RodPose>& rod) {
        const Vec3 up = normalized (frame.transform_vector (Vec3 (0, 1, 0)));
        const Vec3 facing =
          normalized (frame.transform_vector (Vec3 (0, 0, 1)));
        const Vec3 left = normalized (cross (up, facing));
        const Vec3 foot[2] = { boat_model::foot_right, boat_model::foot_left };

        HoldPose hold;
        hold.facing = unit_direction (facing);
        hold.up = unit_direction (up);
        position_value (hold.pelvis) =
          frame.transform_point (boat_model::seat_helm) + up * 0.09f;
        hold.lean = 0.12f * u::rad;
        hold.gaze = normalized (facing + up * 0.05f);
        hold.head_up = up;
        hold.sole = unit_direction (facing);
        hold.instep = unit_direction (up);

        const Vec3 grip =
          tail.transform_point (boat_model::tiller_grip - boat_model::pivot);
        const Vec3 knee = position_value (hold.pelvis) + facing * 0.42f +
                          left * 0.16f + up * 0.08f;
        const Vec3 hand[2] = { grip, knee };
        for (int i = 0; i < 2; ++i) {
          const float side = i == 0 ? -1.0f : 1.0f;
          const Vec3 shoulder = position_value (held_shoulder (hold, i));
          hold.wrist[i] =
            position (hand[i] - normalized (hand[i] - shoulder) * 0.07f);
          hold.elbow_pole[i] = left * side - up * 0.6f - facing * 0.2f;
          hold.ankle[i] =
            position (frame.transform_point (foot[i]) + up * 0.085f);
          hold.knee_pole[i] = up + facing * 0.6f + left * (side * 0.3f);
        }
        (void)boat;
        if (!rod)
          return pose_holding (hold);
        Vec3 toward =
          position_value (rod->water) - position_value (hold.pelvis);
        toward = toward - up * dot (toward, up);
        if (length2 (toward) > 1e-4f)
          hold.gaze = normalized (normalized (toward) - up * 0.15f);
        // Posed once to find the shoulders, and again with the hand where
        // a rod raised so high is held from them.
        const AvatarSkeleton seated = pose_holding (hold);
        hold.wrist[1] =
          position (rod_hand (seated, rod->raise.numerical_value_in (u::rad)));
        hold.elbow_pole[1] = left * 0.6f - up;
        return pose_holding (hold);
      }

      // Someone riding along on the fore seat, hands on their knees and
      // feet on the sole ahead of it, looking where the boat goes.
      AvatarSkeleton pose_passenger (const Mat4& frame) {
        const Vec3 up = normalized (frame.transform_vector (Vec3 (0, 1, 0)));
        const Vec3 facing =
          normalized (frame.transform_vector (Vec3 (0, 0, 1)));
        const Vec3 left = normalized (cross (up, facing));
        HoldPose hold;
        hold.facing = unit_direction (facing);
        hold.up = unit_direction (up);
        position_value (hold.pelvis) =
          frame.transform_point (boat_model::seat_fore) + up * 0.09f;
        hold.lean = 0.18f * u::rad;
        hold.gaze = normalized (facing + up * 0.03f);
        hold.head_up = up;
        hold.sole = unit_direction (facing);
        hold.instep = unit_direction (up);
        for (int i = 0; i < 2; ++i) {
          const float side = i == 0 ? -1.0f : 1.0f;
          const Vec3 knee = position_value (hold.pelvis) + facing * 0.40f +
                            left * (side * 0.13f) + up * 0.07f;
          const Vec3 shoulder = position_value (held_shoulder (hold, i));
          hold.wrist[i] =
            position (knee - normalized (knee - shoulder) * 0.07f);
          hold.elbow_pole[i] = left * side - up * 0.5f;
          position_value (hold.ankle[i]) =
            frame.transform_point (
              boat_model::seat_fore + Vec3 (side * -0.12f, 0.0f, 0.45f) -
              Vec3 (0.0f,
                    boat_model::seat_fore[1] - boat_model::foot_left[1],
                    0.0f)) +
            up * 0.085f;
          hold.knee_pole[i] = up + facing * 0.6f + left * (side * 0.3f);
        }
        return pose_holding (hold);
      }
    }
  }
  using namespace boat_drawing;

  void render_boat (render::Renderer& r,
                    render::DrawList& dl,
                    const BoatPose& boat,
                    std::optional<Outfit> helmsman,
                    uint64_t motion_base,
                    const std::optional<RodPose>& rod,
                    seconds_t time,
                    std::optional<Outfit> passenger) {
    const BoatMeshes& meshes = boat_meshes (r);
    const Mat4 frame =
      model_mesh::body_frame (boat.position, boat.forward, boat.up);
    const Mat4 tail = tail_frame (boat, frame);
    if (passenger)
      figure::draw (dl, pose_passenger (frame), *passenger);
    if (helmsman || rod) {
      const AvatarSkeleton k = pose_helmsman (boat, frame, tail, rod);
      if (helmsman)
        figure::draw (dl, k, *helmsman);
      if (rod)
        draw_rod (dl, k, *rod, seconds_value (time));
    }

    r.draw_mesh (*meshes.hull, frame, motion_base + 1);
    r.draw_mesh (*meshes.longtail, tail, motion_base + 2);
    // The propeller turns about the shaft, which runs from the hub up to
    // the pivot.
    const Vec3 shaft = normalized (boat_model::pivot - boat_model::hub);
    const Vec3 side = normalized (cross (Vec3 (0, 1, 0), shaft));
    const Vec3 over = cross (shaft, side);
    r.draw_mesh (*meshes.propeller,
                 tail *
                   Mat4::translation (boat_model::hub - boat_model::pivot) *
                   Mat4::basis (side, over, shaft) *
                   Mat4::rotation (boat.propeller, Vec3 (0, 0, 1)),
                 motion_base + 3);
  }

  void BoatWake::follow (const BoatPose& boat,
                         meters_t water_level,
                         meters_t depth,
                         seconds_t time) {
    while (!m_marks.empty () && time - m_marks.front ().time > lasts)
      m_marks.pop_front ();
    if (!m_marks.empty () && m_marks.back ().time > time)
      m_marks.clear ();
    Vec3 level_ahead = direction_value (boat.forward);
    level_ahead[1] = 0.0f;
    if (length2 (level_ahead) < 1e-6f)
      return;
    const direction_t ahead = unit_direction (normalized (level_ahead));
    position_t stern = boat.position - quantity_cast<isq::position_vector> (
                                         along (ahead, 1.2f * u::m));
    // Only where the stern is down in water that floats it.
    if (position_value (stern)[1] > meters_value (water_level + 0.15f * u::m) ||
        depth < 0.15f * u::m) {
      if (!m_marks.empty () && m_marks.back ().speed > 0.0f * u::m / u::s)
        m_marks.push_back ({ m_marks.back ().stern,
                             m_marks.back ().across,
                             0.0f * u::m / u::s,
                             time });
      return;
    }
    position_value (stern)[1] = meters_value (water_level + 0.03f * u::m);
    if (!m_marks.empty () &&
        length2 (displacement_value (stern - m_marks.back ().stern)) <
          0.6f * 0.6f &&
        time - m_marks.back ().time < 0.5f * u::s)
      return;
    const speed_t speed =
      !m_marks.empty () && time > m_marks.back ().time
        ? speed_t (length (displacement_value (stern - m_marks.back ().stern)) *
                   u::m / (time - m_marks.back ().time))
        : speed_t (0.0f * u::m / u::s);
    // A hash of where it is for the foam, so that it does not shimmer.
    const float hash = std::sin (position_value (stern)[0] * 12.9898f +
                                 position_value (stern)[2] * 78.233f) *
                       43758.5453f;
    const proportion_t foam =
      (0.25f + 0.75f * (hash - std::floor (hash))) * proportion[one];
    m_marks.push_back (
      { stern,
        unit_direction (
          Vec3 (direction_value (ahead)[2], 0.0f, -direction_value (ahead)[0])),
        std::min (speed, speed_t (20.0f * u::m / u::s)),
        time,
        foam });
  }

  void BoatWake::draw (
    render::DrawList& dl,
    seconds_t time,
    proportion_t light,
    const std::function<meters_t (const position_t&)>& level) const {
    if (m_marks.size () < 2)
      return;
    render::DrawState soft;
    soft.blend = true;
    soft.depth_write = false;
    soft.cull = false;
    dl.state (soft);
    dl.lit (false);
    dl.fogged (true);
    const proportion_t shade =
      0.25f * proportion[one] +
      0.7f * std::clamp (light, 0.0f * proportion[one], 1.0f * proportion[one]);
    const DisplayColor white (shade.numerical_value_in (one),
                              shade.numerical_value_in (one),
                              (shade * 1.02f).numerical_value_in (one));
    const proportion_t none = 0.0f * proportion[one];
    // Below this a strip's foam is not drawn.
    const proportion_t faint = 0.01f * proportion[one];
    dl.begin (render::Prim::Triangles);
    // On the water as it stands now, which the waves lift and lower.
    const auto on_water = [&] (position_t at) {
      position_value (at)[1] = meters_value (level (at) + 0.03f * u::m);
      return at;
    };
    // Each strip as quads between consecutive marks: `offset` places its
    // middle across the wake and `width` its breadth, by the mark's age.
    const auto strip = [&] (auto offset, auto width, proportion_t strength) {
      // Each mark's middle and edges, lifted once and shared by the quads
      // either side of it.
      const std::size_t n = m_marks.size ();
      std::vector<proportion_t> faded (n);
      for (std::size_t i = 0; i < n; ++i) {
        const WakeMark& m = m_marks[i];
        const seconds_t age = time - m.time;
        const proportion_t under_way = std::clamp (
          proportion_t ((m.speed - 1.0f * u::m / u::s) / (4.0f * u::m / u::s)),
          none,
          1.0f * proportion[one]);
        const proportion_t fade =
          std::max (none, proportion_t (1.0f * proportion[one] - age / lasts));
        const proportion_t settled =
          std::min (1.0f, seconds_value (age) * 4.0f + 0.3f) * proportion[one];
        faded[i] = strength * m.foam * under_way * fade * fade * fade * settled;
      }
      std::vector<std::array<position_t, 3>> lifted (n);
      for (std::size_t i = 0; i < n; ++i) {
        const bool seen = faded[i] > faint || (i > 0 && faded[i - 1] > faint) ||
                          (i + 1 < n && faded[i + 1] > faint);
        if (!seen)
          continue;
        const WakeMark& m = m_marks[i];
        const seconds_t age = time - m.time;
        const position_t c = m.stern + quantity_cast<isq::position_vector> (
                                         along (m.across, offset (m, age)));
        const auto w = quantity_cast<isq::position_vector> (
          along (m.across, 0.5f * width (m, age)));
        lifted[i] = { on_water (c), on_water (c - w), on_water (c + w) };
      }
      for (std::size_t i = 0; i + 1 < m_marks.size (); ++i) {
        const proportion_t fa = faded[i], fb = faded[i + 1];
        if (fa <= faint && fb <= faint)
          continue;
        const position_t& ca = lifted[i][0];
        const position_t& cb = lifted[i + 1][0];
        // Two halves, each from the strip's middle line, where the foam
        // is thickest, out to an edge that has none.
        for (const int edge : { 1, 2 }) {
          dl.color (white, fa);
          dl.vertex (ca);
          dl.color (white, none);
          dl.vertex (lifted[i][edge]);
          dl.vertex (lifted[i + 1][edge]);
          dl.color (white, fa);
          dl.vertex (ca);
          dl.color (white, none);
          dl.vertex (lifted[i + 1][edge]);
          dl.color (white, fb);
          dl.vertex (cb);
        }
      }
    };
    // The arms spread at some 19.5 degrees (tan = 0.35) from the track.
    for (const float side : { 1.0f, -1.0f })
      strip (
        [side] (const WakeMark& m, seconds_t age) {
          return side * (0.75f * u::m + 0.35f * m.speed * age);
        },
        [] (const WakeMark&, seconds_t age) {
          return 0.2f * u::m + 0.15f * u::m / u::s * age;
        },
        0.2f * proportion[one]);
    // The churned water behind the propeller, widening and settling.
    strip ([] (const WakeMark&, seconds_t) { return 0.0f * u::m; },
           [] (const WakeMark&, seconds_t age) {
             return 0.6f * u::m + 0.35f * u::m / u::s * age;
           },
           0.26f * proportion[one]);
    dl.end ();
    dl.lit (true);
    dl.state (render::DrawState ());
  }

  void
  BoatWakes::draw (render::Renderer& r,
                   const BoatPose& boat,
                   const JetSkiPose& jet_ski,
                   const BoatPose* their_boat,
                   const JetSkiPose* their_jet_ski,
                   const std::function<meters_t (const position_t&)>& level,
                   const std::function<meters_t (const position_t&)>& ground,
                   seconds_t time,
                   proportion_t daylight) {
    // The jet ski leaves a wake as a boat of its place and heading.
    const auto as_boat = [] (const JetSkiPose& ski) {
      return BoatPose { .position = ski.position,
                        .forward = ski.forward,
                        .up = ski.up };
    };
    const auto trail = [&] (BoatWake& wake, const BoatPose& pose) {
      const position_t& at = pose.position;
      wake.follow (pose, level (at), level (at) - ground (at), time);
      wake.draw (m_dl, time, daylight, level);
    };
    m_dl.clear ();
    trail (m_boat, boat);
    trail (m_jet_ski, as_boat (jet_ski));
    if (their_boat && their_jet_ski) {
      trail (m_their_boat, *their_boat);
      trail (m_their_jet_ski, as_boat (*their_jet_ski));
    } else {
      m_their_boat.clear ();
      m_their_jet_ski.clear ();
    }
    r.draw_list (m_dl, 0x0002);
  }
}
