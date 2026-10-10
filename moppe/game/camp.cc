#include <moppe/game/camp.hh>

#include <moppe/game/sprites.hh>
#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace moppe::game {
  // Named, not anonymous: the game builds as one unit, where another
  // file's helpers of the same name would collide.
  namespace camp_parts {
    constexpr float tau = 6.2831853f;
    constexpr float pi = 3.14159265f;
    const Vec3 world_up (0, 1, 0);

    // A loaded hammock's lowest point sits this far above the higher of
    // its trees' roots, and sags by this much of its span; empty it hangs
    // shallower. Its cloth takes the middle of the span and lines the rest.
    constexpr float seat_height = 0.60f;
    constexpr float loaded_sag = 0.20f;
    constexpr float empty_sag = 0.12f;
    constexpr float cloth_from = 0.15f;
    constexpr float cloth_to = 0.85f;

    // What a tree must be to carry a strap, and how level its roots must
    // be with its partner's.
    constexpr float least_height = 5.0f;
    constexpr float least_girth = 0.07f;
    constexpr float most_girth = 0.6f;
    constexpr float most_step = 0.7f;
    constexpr float highest_strap = 2.5f;
    // Its branches must start above the strap and the head of whoever
    // sits up in the hammock; and a tree that is mostly crown -- a spruce
    // -- hangs its boughs wide and low, so it must be bare much higher.
    constexpr float least_clear = 2.4f;
    constexpr float least_clear_under_deep_crown = 4.5f;
    constexpr float deep_crown = 0.6f;
    // Nothing else may stand this near the line between them.
    constexpr float clear_way = 0.55f;

    Vec3 flat (Vec3 v) {
      v[1] = 0.0f;
      return v;
    }

    float mix (float a, float b, float t) {
      return a + (b - a) * t;
    }

    // The hung hammock's geometry: a parabola between the two points where
    // its lines leave the bark, swung about the line between them, with
    // the cloth spread across it and gathered to a point at each end.
    struct Shape {
      Vec3 hang[2];
      Vec3 run;
      Vec3 down, side, up;
      float sag = 0.0f;
      float half_width = 0.0f;
      float lift = 0.0f;

      // A point on the sagging centre line, 0 at the foot's tree to 1 at
      // the head's, and the line's direction there, headward.
      Vec3 centre (float t) const {
        return hang[0] + run * t + down * (4.0f * sag * t * (1.0f - t));
      }
      Vec3 tangent (float t) const {
        return normalized (run + down * (4.0f * sag * (1.0f - 2.0f * t)));
      }
      // The way a body lying at `t` faces: square to the line, upward.
      Vec3 normal (float t) const {
        const Vec3 along = tangent (t);
        return normalized (up - along * dot (up, along));
      }
      // The cloth: `u` along it from foot to head, `v` across from -1 to
      // 1. Its edges run straighter than its middle, so they rise.
      Vec3 cloth (float u, float v) const {
        const float t = cloth_from + (cloth_to - cloth_from) * u;
        const float bell = std::sin (pi * u);
        return centre (t) +
               side *
                 (half_width * std::pow (std::max (bell, 0.0f), 0.55f) * v) +
               up * (lift * bell * v * v);
      }
    };

    Shape shape_of (const HammockSite& site, float load, float swing) {
      Shape s;
      const Vec3 along = hammock_along (site);
      s.hang[0] = site.strap[0] + along * site.girth[0];
      s.hang[1] = site.strap[1] - along * site.girth[1];
      s.run = s.hang[1] - s.hang[0];
      const Vec3 axis = normalized (s.run);
      const Vec3 up = normalized (world_up - axis * dot (world_up, axis));
      const Vec3 side = cross (axis, up);
      s.down = side * std::sin (swing) - up * std::cos (swing);
      s.up = -s.down;
      s.side = side * std::cos (swing) + up * std::sin (swing);
      const float span = length (s.run);
      s.sag = span * mix (empty_sag, loaded_sag, load);
      s.half_width = mix (0.34f, 0.54f, load);
      s.lift = mix (0.08f, 0.30f, load);
      return s;
    }

    bool same_tree (const mov::Trunk& a, const mov::Trunk& b) {
      return a.root[0] == b.root[0] && a.root[2] == b.root[2];
    }

    bool carries (const mov::Trunk& trunk) {
      if (trunk.height < least_height || trunk.radius < least_girth ||
          trunk.radius > most_girth)
        return false;
      const bool deep = trunk.height - trunk.clear > deep_crown * trunk.height;
      return trunk.clear >= (deep ? least_clear_under_deep_crown : least_clear);
    }

    // Whether a hammock hangs between `a` and `b`, with `others` the trees
    // that might stand in its way; fills `site` if so.
    bool fits (const mov::Trunk& a,
               const mov::Trunk& b,
               std::span<const mov::Trunk> others,
               const GroundHeight& ground,
               HammockSite& site) {
      if (!carries (a) || !carries (b))
        return false;
      const Vec3 between = flat (b.root - a.root);
      const float apart = length (between);
      const float span = apart - a.radius - b.radius;
      if (span < hammock_least_span || span > hammock_most_span)
        return false;
      if (std::fabs (a.root[1] - b.root[1]) > most_step)
        return false;
      const float top = std::max (a.root[1], b.root[1]);
      const float strap = top + seat_height + loaded_sag * span;
      if (strap - std::min (a.root[1], b.root[1]) > highest_strap)
        return false;
      const Vec3 along = between * (1.0f / apart);
      // The ground must not rise into the cloth.
      for (int i = 1; i < 6; ++i) {
        const Vec3 at = a.root + between * (i / 6.0f);
        if (ground (at[0], at[2]) > top + 0.2f)
          return false;
      }
      for (const mov::Trunk& other : others) {
        if (same_tree (other, a) || same_tree (other, b) || other.height < 0.8f)
          continue;
        const Vec3 to = flat (other.root - a.root);
        const float reach = dot (to, along);
        if (reach < 0.0f || reach > apart)
          continue;
        const float off = length (to - along * reach);
        if (off < clear_way + other.radius)
          return false;
      }
      // The head lies toward the higher root, looking out over the feet.
      const bool swap = a.root[1] > b.root[1];
      const mov::Trunk& foot = swap ? b : a;
      const mov::Trunk& head = swap ? a : b;
      // Each strap goes round its trunk where the trunk, leaning as it
      // does, passes the strap's height; a trunk is a little slimmer there
      // than at its foot.
      const auto up_trunk = [strap] (const mov::Trunk& trunk) {
        return trunk.root +
               trunk.axis * ((strap - trunk.root[1]) /
                             std::max (0.5f, float (trunk.axis[1])));
      };
      site.strap[0] = up_trunk (foot);
      site.strap[1] = up_trunk (head);
      site.girth[0] = 0.9f * foot.radius;
      site.girth[1] = 0.9f * head.radius;
      return true;
    }

    // A round bar from `a` to `b`, as triangles.
    void tube (render::DrawList& dl,
               const Vec3& a,
               const Vec3& b,
               float radius,
               int sides) {
      const Vec3 axis = normalized (b - a);
      Vec3 u = cross (axis, world_up);
      if (length2 (u) < 1e-6f)
        u = cross (axis, Vec3 (1, 0, 0));
      u = normalized (u);
      const Vec3 v = cross (axis, u);
      for (int i = 0; i < sides; ++i) {
        const float t0 = tau * i / sides;
        const float t1 = tau * (i + 1) / sides;
        const Vec3 n0 = u * std::cos (t0) + v * std::sin (t0);
        const Vec3 n1 = u * std::cos (t1) + v * std::sin (t1);
        const auto put = [&] (const Vec3& p, const Vec3& n) {
          dl.normal (n);
          dl.vertex (p + n * radius);
        };
        put (a, n0);
        put (b, n0);
        put (b, n1);
        put (a, n0);
        put (b, n1);
        put (a, n1);
        // The ends, closed.
        dl.normal (-axis);
        dl.vertex (a);
        dl.vertex (a + n1 * radius);
        dl.vertex (a + n0 * radius);
        dl.normal (axis);
        dl.vertex (b);
        dl.vertex (b + n0 * radius);
        dl.vertex (b + n1 * radius);
      }
    }

    // A soft sprite facing the camera, stretched along `axis`; seen end-on
    // it rounds out.
    void flame_sprite (render::DrawList& dl,
                       const Vec3& camera,
                       const Vec3& centre,
                       const Vec3& axis,
                       float half_length,
                       float half_width,
                       DisplayColor colour,
                       float alpha) {
      const Vec3 view = normalized (camera - centre);
      Vec3 side = cross (axis, view);
      if (length2 (side) < 1e-8f)
        side = cross (Vec3 (1, 0, 0), view);
      side = normalized (side);
      const Vec3 up = cross (view, side);
      const float end_on = std::fabs (dot (axis, view));
      const float along =
        half_length + (half_width - half_length) * end_on * end_on;
      const Vec3 u = up * along, v = side * half_width;
      dl.color (colour, alpha);
      dl.uv (0, 0);
      dl.vertex (centre - u - v);
      dl.uv (1, 0);
      dl.vertex (centre - u + v);
      dl.uv (1, 1);
      dl.vertex (centre + u + v);
      dl.uv (0, 1);
      dl.vertex (centre + u - v);
    }
  }

  Vec3 hammock_middle (const HammockSite& site) {
    using namespace camp_parts;
    return (site.strap[0] + site.strap[1]) * 0.5f;
  }

  Vec3 hammock_along (const HammockSite& site) {
    using namespace camp_parts;
    const Vec3 between = flat (site.strap[1] - site.strap[0]);
    return length2 (between) > 1e-6f ? normalized (between) : Vec3 (0, 0, 1);
  }

  Vec3 hammock_across (const HammockSite& site) {
    using namespace camp_parts;
    return cross (hammock_along (site), world_up);
  }

  float hammock_distance (const HammockSite& site, const Vec3& feet) {
    using namespace camp_parts;
    const Vec3 between = flat (site.strap[1] - site.strap[0]);
    const float apart2 = std::max (length2 (between), 1e-6f);
    const Vec3 to = flat (feet - site.strap[0]);
    const float t = std::clamp (dot (to, between) / apart2, 0.0f, 1.0f);
    return length (to - between * t);
  }

  float hammock_load (const Camp& camp, double now) {
    using namespace camp_parts;
    const float since = static_cast<float> (now - camp.rest_time);
    const float eased = smoothstep (0.0f, 0.8f, since);
    return camp.resting ? eased : 1.0f - eased;
  }

  std::optional<HammockSite>
  hammock_site_at (std::span<const mov::Trunk> trunks,
                   const Vec3& feet,
                   const GroundHeight& ground) {
    using namespace camp_parts;
    std::optional<HammockSite> best;
    float best_score = std::numeric_limits<float>::infinity ();
    for (std::size_t i = 0; i < trunks.size (); ++i)
      for (std::size_t j = i + 1; j < trunks.size (); ++j) {
        const mov::Trunk& a = trunks[i];
        const mov::Trunk& b = trunks[j];
        const Vec3 between = flat (b.root - a.root);
        const float apart2 = length2 (between);
        if (apart2 < 1.0f || apart2 > 49.0f)
          continue;
        // Standing between them: along the middle of the line, and near it.
        const Vec3 to = flat (feet - a.root);
        const float t = dot (to, between) / apart2;
        if (t < 0.12f || t > 0.88f)
          continue;
        const float off = length (to - between * t);
        if (off > 1.5f)
          continue;
        HammockSite site;
        if (!fits (a, b, trunks, ground, site))
          continue;
        const float score =
          off + 0.5f * std::fabs (t - 0.5f) * std::sqrt (apart2);
        if (score < best_score) {
          best_score = score;
          best = site;
        }
      }
    return best;
  }

  std::optional<HammockSite>
  nearest_hammock_site (std::span<const mov::Trunk> trunks,
                        const Vec3& near,
                        const GroundHeight& ground,
                        float shade) {
    using namespace camp_parts;
    // Sorted along x, a tree's possible partners are its neighbours in the
    // order.
    std::vector<mov::Trunk> sorted (trunks.begin (), trunks.end ());
    std::ranges::sort (sorted, [] (const mov::Trunk& a, const mov::Trunk& b) {
      return a.root[0] < b.root[0];
    });
    constexpr float window = hammock_most_span + 2.0f * most_girth;
    // The crown standing over a place: each tree round it by the depth of
    // its crown over its distance, found by a walk through the order from
    // where the place's x falls.
    const auto overhead = [&] (const Vec3& middle) {
      float crown = 0.0f;
      const auto from = std::ranges::lower_bound (
        sorted, middle[0] - open_sky, {}, [] (const mov::Trunk& trunk) {
          return static_cast<float> (trunk.root[0]);
        });
      for (auto trunk = from;
           trunk != sorted.end () && trunk->root[0] <= middle[0] + open_sky;
           ++trunk)
        if (const float away = length (flat (trunk->root - middle));
            trunk->height > 3.0f && away < open_sky)
          crown += (trunk->height - trunk->clear) / (away + 2.0f);
      return crown;
    };
    std::optional<HammockSite> best;
    float best_distance = std::numeric_limits<float>::infinity ();
    std::size_t first = 0;
    for (std::size_t i = 0; i < sorted.size (); ++i) {
      const mov::Trunk& a = sorted[i];
      if (!carries (a))
        continue;
      while (sorted[first].root[0] < a.root[0] - window)
        ++first;
      std::size_t last = i;
      while (last + 1 < sorted.size () &&
             sorted[last + 1].root[0] <= a.root[0] + window)
        ++last;
      const std::span<const mov::Trunk> around (sorted.data () + first,
                                                last - first + 1);
      for (std::size_t j = i + 1; j <= last; ++j) {
        const mov::Trunk& b = sorted[j];
        const Vec3 middle = (a.root + b.root) * 0.5f;
        float distance = length (flat (middle - near));
        if (distance >= best_distance)
          continue;
        HammockSite site;
        if (!fits (a, b, around, ground, site))
          continue;
        if (shade > 0.0f)
          distance += shade * overhead (middle);
        if (distance >= best_distance)
          continue;
        best_distance = distance;
        best = site;
      }
    }
    return best;
  }

  void draw_hammock (render::DrawList& dl,
                     const HammockSite& site,
                     float load,
                     float swing) {
    using namespace camp_parts;
    const Shape s = shape_of (site, load, swing);
    render::DrawState both_sides;
    both_sides.cull = false;
    dl.state (both_sides);
    dl.lit (true);
    dl.fogged (true);
    dl.begin (render::Prim::Triangles);

    // The cloth: madder canvas with two unbleached stripes and a darker
    // hem.
    constexpr int along = 24;
    constexpr int across = 10;
    const DisplayColor canvas (0.70f, 0.27f, 0.16f);
    const DisplayColor stripe (0.88f, 0.80f, 0.62f);
    const DisplayColor hem (0.46f, 0.17f, 0.11f);
    const auto at = [&] (int i, int j) {
      return s.cloth (static_cast<float> (i) / along, 2.0f * j / across - 1.0f);
    };
    const auto normal_at = [&] (int i, int j) {
      const Vec3 du =
        at (std::min (i + 1, along), j) - at (std::max (i - 1, 0), j);
      const Vec3 dv =
        at (i, std::min (j + 1, across)) - at (i, std::max (j - 1, 0));
      Vec3 n = cross (du, dv);
      if (length2 (n) < 1e-10f)
        return s.up;
      n = normalized (n);
      return dot (n, s.up) < 0.0f ? -n : n;
    };
    for (int j = 0; j < across; ++j) {
      dl.color (j == 0 || j == across - 1   ? hem
                : j == 2 || j == across - 3 ? stripe
                                            : canvas);
      for (int i = 0; i < along; ++i) {
        const auto put = [&] (int a, int b) {
          dl.normal (normal_at (a, b));
          dl.vertex (at (a, b));
        };
        put (i, j);
        put (i + 1, j);
        put (i + 1, j + 1);
        put (i, j);
        put (i + 1, j + 1);
        put (i, j + 1);
      }
    }

    // The lines from the gathered ends to the bark, and the webbing round
    // each trunk.
    dl.color (0.78f, 0.70f, 0.52f);
    tube (dl, s.hang[0], s.cloth (0.0f, 0.0f), 0.011f, 5);
    tube (dl, s.cloth (1.0f, 0.0f), s.hang[1], 0.011f, 5);
    dl.color (0.17f, 0.16f, 0.15f);
    for (int end = 0; end < 2; ++end) {
      constexpr int sides = 14;
      const float radius = site.girth[end] + 0.008f;
      for (int i = 0; i < sides; ++i) {
        const float a0 = tau * i / sides;
        const float a1 = tau * (i + 1) / sides;
        const Vec3 n0 (std::cos (a0), 0.0f, std::sin (a0));
        const Vec3 n1 (std::cos (a1), 0.0f, std::sin (a1));
        const Vec3 low = site.strap[end] - world_up * 0.03f;
        const Vec3 high = site.strap[end] + world_up * 0.03f;
        const auto put = [&] (const Vec3& p, const Vec3& n) {
          dl.normal (n);
          dl.vertex (p + n * radius);
        };
        put (low, n0);
        put (high, n0);
        put (high, n1);
        put (low, n0);
        put (high, n1);
        put (low, n1);
      }
    }
    dl.end ();
    dl.state (render::DrawState ());
  }

  AvatarSkeleton pose_resting (const HammockSite& site,
                               float swing,
                               const Vec3& gaze,
                               float time) {
    using namespace camp_parts;
    const Shape s = shape_of (site, 1.0f, swing);
    const float span = length (s.run);
    // The hips lie a little toward the foot from the lowest point, so the
    // whole body is cradled.
    const float at = 0.46f;
    const Vec3 facing = s.normal (at + 0.04f);
    const Vec3 headward = s.tangent (at + 0.06f);
    const Vec3 right = normalized (cross (headward, facing));
    const float breath = 0.006f * std::sin (time * 1.3f);

    HoldPose hold;
    hold.facing = facing;
    hold.up = headward;
    // High enough in the cloth that the pack on their back stays in it.
    hold.pelvis = s.centre (at) + s.normal (at) * 0.21f;
    hold.lean = 0.16f;
    // The head rests back; it turns only part of the way after the eyes.
    hold.gaze = normalized (facing + normalized (gaze) * 0.7f);
    hold.head_up = s.tangent (at + 0.22f);
    const float cross_ankles = 0.86f / span;
    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      hold.wrist[i] = hold.pelvis + headward * 0.25f +
                      facing * (0.13f + breath) + right * (side * 0.035f);
      hold.elbow_pole[i] = right * side - facing * 0.4f;
      const float foot = at - cross_ankles;
      hold.ankle[i] = s.centre (foot) + s.normal (foot) * (0.10f + 0.05f * i) +
                      s.side * (side * 0.03f);
      hold.knee_pole[i] = facing + right * (side * 0.2f);
    }
    // The feet fall slack, toes up and a little away.
    const Vec3 at_feet = s.normal (at - cross_ankles);
    const Vec3 to_head = s.tangent (at - cross_ankles);
    hold.sole = normalized (at_feet * 0.82f - to_head * 0.57f);
    hold.instep = normalized (to_head * 0.82f + at_feet * 0.57f);
    return pose_holding (hold);
  }

  Vec3 resting_eye (const HammockSite& site, float swing) {
    using namespace camp_parts;
    const Shape s = shape_of (site, 1.0f, swing);
    const AvatarSkeleton k = pose_resting (site, swing, s.up, 0.0f);
    return k.head + s.normal (0.6f) * 0.09f;
  }

  Vec3 resting_gaze (const HammockSite& site, float yaw, float pitch) {
    using namespace camp_parts;
    // Yaw turns from the foot of the hammock; a sleeper looks along
    // their own length and up.
    const Vec3 ahead = -hammock_along (site);
    const Vec3 right = cross (ahead, world_up);
    return (ahead * std::cos (yaw) + right * std::sin (yaw)) *
             std::cos (pitch) +
           world_up * std::sin (pitch);
  }

  float fire_burn (const Camp& camp, double now) {
    using namespace camp_parts;
    const float since = static_cast<float> (now - camp.fire_time);
    if (camp.fire)
      return 0.12f + 0.88f * smoothstep (0.0f, 3.5f, since);
    return since < 1.5f ? 1.0f - smoothstep (0.0f, 1.5f, since) : 0.0f;
  }

  Vec3 fire_light_position (const Camp& camp) {
    using namespace camp_parts;
    return camp.hearth + world_up * 0.55f;
  }

  DisplayColor fire_light_color (float burn, float time) {
    const float flicker = 0.80f + 0.10f * std::sin (time * 11.3f) +
                          0.06f * std::sin (time * 23.7f + 1.3f) +
                          0.04f * std::sin (time * 5.1f + 0.4f);
    // A display colour: the renderer's light is its 2.2 power.
    return scale_display (
      DisplayColor (1.50f, 1.04f, 0.56f),
      std::pow (std::clamp (burn * flicker, 0.0f, 1.0f), 1.0f / 2.2f));
  }

  void draw_hearth (render::DrawList& dl, const Vec3& hearth, float burn) {
    using namespace camp_parts;
    const auto seed = static_cast<std::uint32_t> (
      std::lround (hearth[0] * 13.0f) * 7919 + std::lround (hearth[2] * 13.0f));
    dl.state (render::DrawState ());
    dl.lit (true);
    dl.fogged (true);

    // A ring of fieldstones.
    constexpr int stones = 9;
    for (int i = 0; i < stones; ++i) {
      const float around =
        tau * (i + 0.35f * hash_lane (seed, 10 + i)) / stones;
      const float size = 0.085f + 0.045f * hash_lane (seed, 30 + i);
      const float grey = 0.36f + 0.16f * hash_lane (seed, 50 + i);
      dl.color (grey, grey * 0.97f, grey * 0.92f);
      dl.push ();
      dl.translate (hearth +
                    Vec3 (std::cos (around), 0.0f, std::sin (around)) * 0.43f +
                    world_up * (size * 0.45f));
      dl.rotate (tau * hash_lane (seed, 70 + i) * u::rad, 0, 1, 0);
      dl.scale (1.25f, 0.72f, 0.95f);
      dl.sphere (size, 7, 5);
      dl.pop ();
    }

    dl.begin (render::Prim::Triangles);
    // The bed of ash.
    constexpr int rim = 14;
    dl.color (0.10f, 0.095f, 0.09f);
    dl.normal (world_up);
    const Vec3 bed = hearth + world_up * 0.02f;
    for (int i = 0; i < rim; ++i) {
      const float a0 = tau * i / rim;
      const float a1 = tau * (i + 1) / rim;
      dl.vertex (bed);
      dl.vertex (bed + Vec3 (std::cos (a1), 0.0f, std::sin (a1)) * 0.40f);
      dl.vertex (bed + Vec3 (std::cos (a0), 0.0f, std::sin (a0)) * 0.40f);
    }
    // Split logs leaned together over it, and two laid across the bottom;
    // they char as the fire takes.
    const float charred = 1.0f - 0.55f * burn;
    constexpr int logs = 5;
    for (int i = 0; i < logs; ++i) {
      const float around = tau * (i + 0.3f * hash_lane (seed, 90 + i)) / logs;
      const Vec3 out (std::cos (around), 0.0f, std::sin (around));
      const float bark = (0.20f + 0.08f * hash_lane (seed, 110 + i)) * charred;
      dl.color (bark, bark * 0.74f, bark * 0.52f);
      tube (dl,
            hearth + out * 0.29f + world_up * 0.05f,
            hearth - out * 0.07f + world_up * 0.40f,
            0.042f,
            6);
    }
    dl.color (0.17f * charred, 0.12f * charred, 0.085f * charred);
    tube (dl,
          hearth + Vec3 (-0.26f, 0.06f, -0.10f),
          hearth + Vec3 (0.26f, 0.06f, 0.08f),
          0.05f,
          6);
    tube (dl,
          hearth + Vec3 (-0.08f, 0.07f, 0.25f),
          hearth + Vec3 (0.10f, 0.07f, -0.25f),
          0.05f,
          6);
    dl.end ();

    // Embers under the logs glow by their own light.
    if (burn > 0.03f) {
      dl.lit (false);
      dl.begin (render::Prim::Triangles);
      dl.color (burn, 0.36f * burn * burn, 0.07f * burn * burn);
      dl.normal (world_up);
      const Vec3 embers = hearth + world_up * 0.035f;
      for (int i = 0; i < rim; ++i) {
        const float a0 = tau * i / rim;
        const float a1 = tau * (i + 1) / rim;
        dl.vertex (embers);
        dl.vertex (embers + Vec3 (std::cos (a1), 0.0f, std::sin (a1)) * 0.22f);
        dl.vertex (embers + Vec3 (std::cos (a0), 0.0f, std::sin (a0)) * 0.22f);
      }
      dl.end ();
      dl.lit (true);
    }
  }

  void draw_fire (render::Renderer& renderer,
                  const Vec3& hearth,
                  const Vec3& camera,
                  float burn,
                  float time) {
    using namespace camp_parts;
    if (burn <= 0.01f)
      return;
    static const render::TexturePtr disc = make_soft_disc_texture (renderer);
    static render::DrawList dl;
    dl.clear ();
    render::DrawState glow;
    glow.blend = true;
    glow.additive = true;
    glow.depth_write = false;
    glow.cull = false;
    dl.state (glow);
    dl.lit (false);
    dl.fogged (true);
    dl.set_texture (disc.get ());
    dl.begin (render::Prim::Quads);

    // The warm air round the fire, for the bloom to take up.
    flame_sprite (dl,
                  camera,
                  hearth + world_up * 0.45f,
                  world_up,
                  1.0f,
                  1.0f,
                  DisplayColor (1.0f, 0.42f, 0.12f),
                  0.15f * burn);
    flame_sprite (dl,
                  camera,
                  hearth + world_up * 0.6f,
                  world_up,
                  2.3f,
                  2.3f,
                  DisplayColor (1.0f, 0.40f, 0.12f),
                  0.045f * burn);

    // Tongues of flame: each a chain of blobs rising from the logs,
    // leaning and licking on its own phase, cooling from pale yellow
    // through orange to a red tip.
    constexpr int tongues = 4;
    constexpr int blobs = 6;
    for (int tongue = 0; tongue < tongues; ++tongue) {
      const float phase = 1.9f * tongue;
      const float around = tau * tongue / tongues + 0.6f;
      const Vec3 foot = Vec3 (std::cos (around), 0.0f, std::sin (around)) *
                        (tongue == 0 ? 0.0f : 0.11f);
      const float lick = 0.80f + 0.20f *
                                   std::sin (time * (6.1f + tongue) + phase) *
                                   std::sin (time * 3.3f + 2.0f * phase);
      const float height =
        (tongue == 0 ? 0.95f : 0.62f) * lick * std::pow (burn, 0.7f);
      for (int i = 0; i < blobs; ++i) {
        const float t = (i + 0.5f) / blobs;
        const Vec3 lean (0.07f * std::sin (time * 4.7f + 3.0f * t + phase),
                         0.0f,
                         0.07f *
                           std::sin (time * 5.9f + 2.2f * t + 1.7f * phase));
        const Vec3 centre = hearth + foot * (1.0f - 0.7f * t) +
                            world_up * (0.12f + t * height) + lean * t;
        const DisplayColor colour =
          t < 0.30f   ? DisplayColor (1.0f, 0.88f, 0.55f)
          : t < 0.65f ? DisplayColor (1.0f, 0.55f, 0.16f)
                      : DisplayColor (0.95f, 0.26f, 0.07f);
        const float fade = (1.0f - t) * (1.0f - t);
        flame_sprite (dl,
                      camera,
                      centre,
                      normalized (world_up + lean * 2.0f),
                      0.8f * height / blobs + 0.06f,
                      0.035f + 0.11f * (1.0f - 0.7f * t),
                      colour,
                      (0.22f + 0.50f * fade) * burn);
      }
    }
    // The white heart of it.
    for (int i = 0; i < 2; ++i)
      flame_sprite (dl,
                    camera,
                    hearth + world_up * (0.14f + 0.10f * i),
                    world_up,
                    0.12f,
                    0.085f,
                    DisplayColor (1.0f, 0.94f, 0.78f),
                    (0.75f - 0.25f * i) * burn);
    dl.end ();
    dl.set_texture (nullptr);
    dl.state (render::DrawState ());
    renderer.draw_list (dl, 0x3000);
  }
}
