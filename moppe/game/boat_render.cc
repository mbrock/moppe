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
        hold.wrist[1] = rod_hand (seated, rod->raise);
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
    const meters_t stern_above =
      (position_value (stern)[1] - meters_value (water_level)) * u::m;
    const proportion_t immersed =
      std::clamp (proportion_t ((0.18f * u::m - stern_above) / (0.18f * u::m)),
                  0.0f * proportion[one],
                  1.0f * proportion[one]);
    const proportion_t deep =
      std::clamp (proportion_t ((depth - 0.12f * u::m) / (0.55f * u::m)),
                  0.0f * proportion[one],
                  1.0f * proportion[one]);
    const proportion_t water_fit = quantity_cast<proportion> (immersed * deep);
    const proportion_t dry_cutoff = 0.002f * proportion[one];
    if (water_fit <= dry_cutoff &&
        (m_marks.empty () || m_marks.back ().foam <= dry_cutoff))
      return;
    position_value (stern)[1] = meters_value (water_level + 0.065f * u::m);
    const plan_displacement_t delta =
      !m_marks.empty () ? plan (stern) - plan (m_marks.back ().stern)
                        : plan_displacement (Vec2 (0.0f, 0.0f));
    const meters_t step = length (delta);
    // Break the ribbon cleanly on teleports, world wraps, or big jumps so we
    // never stretch a wake quad across land or air.
    if (!m_marks.empty () && step > 6.0f * u::m) {
      m_marks.clear ();
    } else if (!m_marks.empty () && step < 0.35f * u::m &&
               time - m_marks.back ().time < 0.14f * u::s) {
      return;
    }
    // Only emit Kelvin wakes when moving forward under way; backing off a
    // shoal must not flip the arms across the hull into a diamond box.
    const plan_direction_t plan_ahead =
      plan_unit_direction (plan (direction_value (ahead)));
    const meters_t forward_step =
      std::max (0.0f * u::m, meters_t (dot (delta, plan_ahead)));
    const speed_t speed =
      !m_marks.empty () && time > m_marks.back ().time
        ? speed_t (forward_step / (time - m_marks.back ().time))
        : speed_t (0.0f * u::m / u::s);
    // Gentle multi-frequency spatial modulation gives organic froth density
    // along the wake without harsh per-segment banding.
    const proportion_t wave_a =
      (0.5f + 0.5f * std::sin (position_value (stern)[0] * 0.72f +
                               position_value (stern)[2] * 0.58f)) *
      proportion[one];
    const proportion_t wave_b =
      (0.5f + 0.5f * std::sin (position_value (stern)[0] * 1.65f -
                               position_value (stern)[2] * 1.25f)) *
      proportion[one];
    const proportion_t foam = quantity_cast<proportion> (
      water_fit * ((0.72f + 0.18f * wave_a.numerical_value_in (one) +
                    0.10f * wave_b.numerical_value_in (one)) *
                   proportion[one]));
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
    (void)light;
    if (m_marks.size () < 2)
      return;
    render::DrawState soft;
    soft.blend = true;
    soft.depth_write = false;
    soft.cull = false;
    dl.state (soft);
    // Lit upward so wake whitewater shares the sun + sky HDR radiance of the
    // river's own foam instead of rendering as an unlit grey stripe.
    dl.lit (true);
    dl.fogged (true);
    dl.normal (Vec3 (0.0f, 1.0f, 0.0f));
    const DisplayColor white (0.93f, 0.96f, 0.97f);
    const proportion_t none = 0.0f * proportion[one];
    // Below this a strip's foam is not drawn.
    const proportion_t faint = 0.008f * proportion[one];
    dl.begin (render::Prim::Triangles);
    // Lift enough to clear sloped river grid diagonals and subdivide long
    // chords so wake ribbons drape smoothly over waves without clipping.
    const auto on_water = [&] (position_t at, meters_t extra) {
      position_value (at)[1] = meters_value (level (at) + extra);
      return at;
    };
    const auto aligned_across = [] (const plan_displacement_t& tangent,
                                    const direction_t& reference) {
      if (length (tangent) <= 0.01f * u::m)
        return reference;
      const plan_direction_t dir = direction_of (tangent);
      const direction_t port = unit_direction (
        Vec3 (plan_value (dir).z (), 0.0f, -plan_value (dir).x ()));
      return dot (port, reference).numerical_value_in (one) < 0.0f
               ? unit_direction (-direction_value (port))
               : port;
    };
    const std::size_t n = m_marks.size ();
    // Smooth horizontal port-side across vector from the actual wake path,
    // aligned with the hull's port side so slowing or reversing never flips
    // left and right arms across each other.
    std::vector<direction_t> path_across (n);
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t prev = i > 0 ? i - 1 : i;
      const std::size_t next = i + 1 < n ? i + 1 : i;
      path_across[i] =
        aligned_across (plan (m_marks[next].stern) - plan (m_marks[prev].stern),
                        m_marks[i].across);
    }
    // Each strip as finely subdivided quads with a soft 5-vertex bell-curve
    // cross-section (avoiding hard gamma-compressed rail edges).
    const auto strip = [&] (auto offset, auto width, proportion_t strength) {
      std::vector<proportion_t> faded (n, none);
      std::vector<position_t> centers (n);
      std::vector<meters_t> half_widths (n, 0.0f * u::m);
      for (std::size_t i = 0; i < n; ++i) {
        const WakeMark& m = m_marks[i];
        const seconds_t age = time - m.time;
        const proportion_t under_way = std::clamp (
          proportion_t ((m.speed - 1.4f * u::m / u::s) / (4.0f * u::m / u::s)),
          none,
          1.0f * proportion[one]);
        const proportion_t fade =
          std::max (none, proportion_t (1.0f * proportion[one] - age / lasts));
        const proportion_t settled_lin =
          std::min (1.0f, seconds_value (age) * 1.8f) * proportion[one];
        const proportion_t settled =
          quantity_cast<proportion> (settled_lin * settled_lin);
        const proportion_t end_taper =
          i == 0
            ? none
            : (i == 1
                 ? 0.35f * proportion[one]
                 : (i == 2 ? 0.75f * proportion[one] : 1.0f * proportion[one]));
        faded[i] =
          quantity_cast<proportion> (strength * m.foam * under_way * fade *
                                     fade * fade * settled * end_taper);
        centers[i] = m.stern + quantity_cast<isq::position_vector> (
                                 along (path_across[i], offset (m, age)));
        half_widths[i] = 0.5f * width (m, age);
      }
      // Compute each arm's own local lateral direction from its offset
      // centerline, keeping sign aligned with path_across so quads never twist.
      std::vector<direction_t> arm_across (n);
      for (std::size_t i = 0; i < n; ++i) {
        const std::size_t prev = i > 0 ? i - 1 : i;
        const std::size_t next = i + 1 < n ? i + 1 : i;
        arm_across[i] = aligned_across (
          plan (centers[next]) - plan (centers[prev]), path_across[i]);
      }
      struct CrossSlice {
        std::array<position_t, 5> points;
        std::array<proportion_t, 5> alphas;
      };
      for (std::size_t i = 0; i + 1 < n; ++i) {
        const proportion_t fa = faded[i], fb = faded[i + 1];
        if (fa <= faint && fb <= faint)
          continue;
        const plan_displacement_t arm_step =
          plan (centers[i + 1]) - plan (centers[i]);
        const plan_displacement_t hull_step =
          plan (m_marks[i + 1].stern) - plan (m_marks[i].stern);
        const meters_t arm_len = length (arm_step);
        if (arm_len < 0.04f * u::m || arm_len > 6.5f * u::m)
          continue;
        // Drop segments where the inner Kelvin arm folds backward across the
        // center of curvature during a sharp turn.
        const meters_t hull_len = length (hull_step);
        if (hull_len > 0.01f * u::m &&
            dot (arm_step, hull_step) <= 0.15f * arm_len * hull_len)
          continue;
        const int slices =
          std::clamp (static_cast<int> (std::ceil (
                        (arm_len / (0.45f * u::m)).numerical_value_in (one))),
                      1,
                      8);
        const auto sample_slice = [&] (proportion_t t) -> CrossSlice {
          const plan_position_t mid_plan =
            plan (centers[i]) + (plan (centers[i + 1]) - plan (centers[i])) *
                                  t.numerical_value_in (one);
          const position_t mid = at_height (mid_plan, 0.0f * u::m);
          const plan_displacement_t lat =
            along (plan_unit_direction (plan (direction_value (arm_across[i]))),
                   (1.0f - t.numerical_value_in (one)) * u::m) +
            along (
              plan_unit_direction (plan (direction_value (arm_across[i + 1]))),
              t.numerical_value_in (one) * u::m);
          const direction_t across =
            length (lat) > 0.01f * u::m
              ? unit_direction (lift (plan_value (direction_of (lat)), 0.0f))
              : arm_across[i];
          const meters_t hw =
            moppe::lerp (half_widths[i], half_widths[i + 1], t);
          const auto outer =
            quantity_cast<isq::position_vector> (along (across, hw));
          const auto flank =
            quantity_cast<isq::position_vector> (along (across, 0.50f * hw));
          const proportion_t peak = moppe::lerp (fa, fb, t);
          const proportion_t shoulder =
            quantity_cast<proportion> (peak * (0.22f * proportion[one]));
          return { { on_water (mid - outer, 0.095f * u::m),
                     on_water (mid - flank, 0.105f * u::m),
                     on_water (mid, 0.115f * u::m),
                     on_water (mid + flank, 0.105f * u::m),
                     on_water (mid + outer, 0.095f * u::m) },
                   { none, shoulder, peak, shoulder, none } };
        };
        CrossSlice prev_slice = sample_slice (none);
        for (int s = 1; s <= slices; ++s) {
          const proportion_t t =
            (static_cast<float> (s) / static_cast<float> (slices)) *
            proportion[one];
          const CrossSlice next_slice = sample_slice (t);
          for (std::size_t k = 0; k < 4; ++k) {
            dl.color (white, prev_slice.alphas[k]);
            dl.vertex (prev_slice.points[k]);
            dl.color (white, prev_slice.alphas[k + 1]);
            dl.vertex (prev_slice.points[k + 1]);
            dl.color (white, next_slice.alphas[k + 1]);
            dl.vertex (next_slice.points[k + 1]);
            dl.color (white, prev_slice.alphas[k]);
            dl.vertex (prev_slice.points[k]);
            dl.color (white, next_slice.alphas[k + 1]);
            dl.vertex (next_slice.points[k + 1]);
            dl.color (white, next_slice.alphas[k]);
            dl.vertex (next_slice.points[k]);
          }
          prev_slice = next_slice;
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
          return 0.36f * u::m + 0.24f * u::m / u::s * age;
        },
        0.18f * proportion[one]);
    // The churned water behind the propeller, widening and settling.
    strip ([] (const WakeMark&, seconds_t) { return 0.0f * u::m; },
           [] (const WakeMark&, seconds_t age) {
             return 0.80f * u::m + 0.44f * u::m / u::s * age;
           },
           0.24f * proportion[one]);
    dl.end ();
    dl.lit (true);
    dl.state (render::DrawState ());
  }

  void
  BoatWakes::draw (render::Renderer& r,
                   const BoatPose& boat,
                   const JetSkiPose& jet_ski,
                   std::span<const PeerCraft> peers,
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
    std::erase_if (m_peers, [&] (const auto& entry) {
      return std::ranges::none_of (
        peers, [&] (const PeerCraft& p) { return p.player == entry.first; });
    });
    for (const PeerCraft& peer : peers) {
      auto& wakes = m_peers[peer.player];
      trail (wakes.first, peer.boat);
      trail (wakes.second, as_boat (peer.jet_ski));
    }
    r.draw_list (m_dl, 0x0002);
  }
}
