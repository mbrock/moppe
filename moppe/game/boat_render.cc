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

      render::MeshPtr bake_boat_part (render::Renderer& r,
                                      const model_mesh::Mesh& mesh) {
        render::DrawList dl;
        model_mesh::record (dl, mesh);
        return r.create_mesh (dl);
      }

      const BoatMeshes& boat_meshes (render::Renderer& r) {
        static const BoatMeshes meshes = [&r] {
          BoatMeshes m;
          m.hull = bake_boat_part (r, boat_model::hull);
          m.longtail = bake_boat_part (r, boat_model::longtail);
          m.propeller = bake_boat_part (r, boat_model::propeller);
          return m;
        }();
        return meshes;
      }

      Mat4 boat_frame (const BoatPose& boat) {
        const Vec3 forward = normalized (boat.forward);
        const Vec3 left = normalized (cross (boat.up, forward));
        const Vec3 up = cross (forward, left);
        return Mat4::translation (boat.position) *
               Mat4::basis (left, up, forward);
      }

      // The long-tail's frame about its pivot: swung about the boat's up
      // after being tipped about its left (mov::Boat::tail_point).
      Mat4 tail_frame (const BoatPose& boat, const Mat4& frame) {
        return frame * Mat4::translation (boat_model::pivot) *
               Mat4::rotation (boat.swing_radians * u::rad, Vec3 (0, 1, 0)) *
               Mat4::rotation (boat.tilt_radians * u::rad, Vec3 (1, 0, 0));
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
        hold.facing = facing;
        hold.up = up;
        hold.pelvis =
          frame.transform_point (boat_model::seat_helm) + up * 0.09f;
        hold.lean = 0.12f;
        hold.gaze = normalized (facing + up * 0.05f);
        hold.head_up = up;
        hold.sole = facing;
        hold.instep = up;

        const Vec3 grip =
          tail.transform_point (boat_model::tiller_grip - boat_model::pivot);
        const Vec3 knee = hold.pelvis + facing * 0.42f + left * 0.16f +
                          up * 0.08f;
        const Vec3 hand[2] = { grip, knee };
        for (int i = 0; i < 2; ++i) {
          const float side = i == 0 ? -1.0f : 1.0f;
          const Vec3 shoulder = held_shoulder (hold, i);
          hold.wrist[i] = hand[i] - normalized (hand[i] - shoulder) * 0.07f;
          hold.elbow_pole[i] = left * side - up * 0.6f - facing * 0.2f;
          hold.ankle[i] = frame.transform_point (foot[i]) + up * 0.085f;
          hold.knee_pole[i] = up + facing * 0.6f + left * (side * 0.3f);
        }
        (void)boat;
        if (!rod)
          return pose_holding (hold);
        Vec3 toward = rod->water - hold.pelvis;
        toward = toward - up * dot (toward, up);
        if (length2 (toward) > 1e-4f)
          hold.gaze = normalized (normalized (toward) - up * 0.15f);
        // Posed once to find the shoulders, and again with the hand where
        // a rod raised so high is held from them.
        const AvatarSkeleton seated = pose_holding (hold);
        hold.wrist[1] = rod_hand (seated, rod->raise);
        hold.elbow_pole[1] = left * 0.6f - up;
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
                    float time) {
    const BoatMeshes& meshes = boat_meshes (r);
    const Mat4 frame = boat_frame (boat);
    const Mat4 tail = tail_frame (boat, frame);
    if (helmsman || rod) {
      const AvatarSkeleton k = pose_helmsman (boat, frame, tail, rod);
      if (helmsman)
        figure::draw (dl, k, *helmsman);
      if (rod)
        draw_rod (dl, k, *rod, time);
    }

    r.draw_mesh (*meshes.hull, frame, motion_base + 1);
    r.draw_mesh (*meshes.longtail, tail, motion_base + 2);
    // The propeller turns about the shaft, which runs from the hub up to
    // the pivot.
    const Vec3 shaft = normalized (boat_model::pivot - boat_model::hub);
    const Vec3 side = normalized (cross (Vec3 (0, 1, 0), shaft));
    const Vec3 over = cross (shaft, side);
    r.draw_mesh (*meshes.propeller,
                 tail * Mat4::translation (boat_model::hub - boat_model::pivot) *
                   Mat4::basis (side, over, shaft) *
                   Mat4::rotation (boat.propeller_radians * u::rad,
                                   Vec3 (0, 0, 1)),
                 motion_base + 3);
  }

  void BoatWake::follow (const BoatPose& boat,
                         float water_level,
                         float depth,
                         float time) {
    while (!m_marks.empty () && time - m_marks.front ().time > lasts)
      m_marks.pop_front ();
    if (!m_marks.empty () && m_marks.back ().time > time)
      m_marks.clear ();
    Vec3 ahead = boat.forward;
    ahead[1] = 0.0f;
    if (length2 (ahead) < 1e-6f)
      return;
    ahead = normalized (ahead);
    Vec3 stern = boat.position - ahead * 1.2f;
    // Only where the stern is down in water that floats it.
    if (stern[1] > water_level + 0.15f || depth < 0.15f) {
      if (!m_marks.empty () && m_marks.back ().speed > 0.0f)
        m_marks.push_back ({ m_marks.back ().stern,
                             m_marks.back ().across,
                             0.0f,
                             time });
      return;
    }
    stern[1] = water_level + 0.03f;
    if (!m_marks.empty () &&
        length2 (stern - m_marks.back ().stern) < 0.6f * 0.6f &&
        time - m_marks.back ().time < 0.5f)
      return;
    const float speed = !m_marks.empty () && time > m_marks.back ().time
                          ? length (stern - m_marks.back ().stern) /
                              (time - m_marks.back ().time)
                          : 0.0f;
    // A hash of where it is for the foam, so that it does not shimmer.
    const float hash = std::sin (stern[0] * 12.9898f + stern[2] * 78.233f) *
                       43758.5453f;
    const float foam = 0.25f + 0.75f * (hash - std::floor (hash));
    m_marks.push_back ({ stern,
                         Vec3 (ahead[2], 0.0f, -ahead[0]),
                         std::min (speed, 20.0f),
                         time,
                         foam });
  }

  void BoatWake::draw (render::DrawList& dl,
                       float time,
                       float light,
                       const std::function<float (const Vec3&)>& level) const {
    if (m_marks.size () < 2)
      return;
    render::DrawState soft;
    soft.blend = true;
    soft.depth_write = false;
    soft.cull = false;
    dl.state (soft);
    dl.lit (false);
    dl.fogged (true);
    const float shade = 0.25f + 0.7f * std::clamp (light, 0.0f, 1.0f);
    dl.begin (render::Prim::Triangles);
    // On the water as it stands now, which the waves lift and lower.
    const auto on_water = [&] (Vec3 at) {
      at[1] = level (at) + 0.03f;
      return at;
    };
    // Each strip as quads between consecutive marks: `offset` places its
    // middle across the wake and `width` its breadth, by the mark's age.
    const auto strip = [&] (auto offset, auto width, float strength) {
      // Each mark's middle and edges, lifted once and shared by the quads
      // either side of it.
      const std::size_t n = m_marks.size ();
      std::vector<float> faded (n);
      for (std::size_t i = 0; i < n; ++i) {
        const WakeMark& m = m_marks[i];
        const float age = time - m.time;
        const float under_way = std::clamp ((m.speed - 1.0f) / 4.0f,
                                            0.0f, 1.0f);
        const float fade = std::max (0.0f, 1.0f - age / lasts);
        faded[i] = strength * m.foam * under_way * fade * fade * fade *
                   std::min (1.0f, age * 4.0f + 0.3f);
      }
      std::vector<std::array<Vec3, 3>> lifted (n);
      for (std::size_t i = 0; i < n; ++i) {
        const bool seen = faded[i] > 0.01f ||
                          (i > 0 && faded[i - 1] > 0.01f) ||
                          (i + 1 < n && faded[i + 1] > 0.01f);
        if (!seen)
          continue;
        const WakeMark& m = m_marks[i];
        const float age = time - m.time;
        const Vec3 c = m.stern + m.across * offset (m, age);
        const Vec3 w = m.across * (0.5f * width (m, age));
        lifted[i] = { on_water (c), on_water (c - w), on_water (c + w) };
      }
      for (std::size_t i = 0; i + 1 < m_marks.size (); ++i) {
        const float fa = faded[i], fb = faded[i + 1];
        if (fa <= 0.01f && fb <= 0.01f)
          continue;
        const Vec3& ca = lifted[i][0];
        const Vec3& cb = lifted[i + 1][0];
        // Two halves, each from the strip's middle line, where the foam
        // is thickest, out to an edge that has none.
        for (const int edge : { 1, 2 }) {
          dl.color (shade, shade, shade * 1.02f, fa);
          dl.vertex (ca);
          dl.color (shade, shade, shade * 1.02f, 0.0f);
          dl.vertex (lifted[i][edge]);
          dl.vertex (lifted[i + 1][edge]);
          dl.color (shade, shade, shade * 1.02f, fa);
          dl.vertex (ca);
          dl.color (shade, shade, shade * 1.02f, 0.0f);
          dl.vertex (lifted[i + 1][edge]);
          dl.color (shade, shade, shade * 1.02f, fb);
          dl.vertex (cb);
        }
      }
    };
    // The arms spread at some 19.5 degrees (tan = 0.35) from the track.
    for (const float side : { 1.0f, -1.0f })
      strip (
        [side] (const WakeMark& m, float age) {
          return side * (0.75f + 0.35f * m.speed * age);
        },
        [] (const WakeMark&, float age) { return 0.2f + 0.15f * age; },
        0.2f);
    // The churned water behind the propeller, widening and settling.
    strip ([] (const WakeMark&, float) { return 0.0f; },
           [] (const WakeMark&, float age) { return 0.6f + 0.35f * age; },
           0.26f);
    dl.end ();
    dl.lit (true);
    dl.state (render::DrawState ());
  }
}
