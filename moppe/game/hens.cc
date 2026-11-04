#include <moppe/game/hens.hh>

#include <moppe/game/chick_model.hh>
#include <moppe/game/hen_model.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/rooster_model.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    namespace hen_parts {
      constexpr float tau = 6.28318531f;
      const Vec3 up (0.0f, 1.0f, 0.0f);

      std::uint32_t hash (std::uint32_t a, std::uint32_t b) {
        std::uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u);
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return h;
      }

      float unit (std::uint32_t a, std::uint32_t b) {
        return static_cast<float> (hash (a, b) >> 8) * (1.0f / 16777216.0f);
      }

      Vec3 forward_of (float heading) {
        return Vec3 (std::sin (heading), 0.0f, std::cos (heading));
      }

      float heading_of (const Vec3& d) {
        return std::atan2 (d[0], d[2]);
      }

      // Where the birds go, in the barn's frame (tools/figure/farm.py):
      // the floor between the stalls and the doors, the hens' corner by
      // the nest boxes, and the yard beyond the ramp; and the way between
      // them, through the doorway and down the ramp.
      struct Area {
        float x0, x1, z0, z1;
      };
      constexpr Area walkway { -5.3f, 5.3f, -0.4f, 3.4f };
      constexpr Area corner { -5.15f, -3.8f, -3.3f, -0.4f };
      constexpr Area yard { -8.0f, 8.0f, 6.9f, 12.0f };
      const Vec3 door_in (0.0f, 0.0f, 3.5f);
      const Vec3 ramp_foot (0.0f, 0.0f, 6.4f);
      // Where a mother hen settles with her brood for the night.
      const Vec3 brood (-4.3f, 0.0f, -2.0f);
      // The two perches: their height in the barn's frame and their z.
      constexpr float perch_y[2] = { 1.05f, 1.35f };
      constexpr float perch_z[2] = { -3.25f, -3.6f };
      constexpr float roost_below_z = -2.75f;

      bool in (const Area& a, const Vec3& p) {
        return p[0] >= a.x0 && p[0] <= a.x1 && p[2] >= a.z0 && p[2] <= a.z1;
      }

      Vec3 clamp_to (const Area& a, Vec3 p) {
        p[0] = std::clamp (static_cast<float> (p[0]), a.x0, a.x1);
        p[2] = std::clamp (static_cast<float> (p[2]), a.z0, a.z1);
        return p;
      }

      Vec3 somewhere (const Area& a, float u, float v) {
        return Vec3 (a.x0 + (a.x1 - a.x0) * u, 0.0f, a.z0 + (a.z1 - a.z0) * v);
      }

      Vec3 to_world (const Coop& c, const Vec3& local) {
        return c.frame.transform_point (local);
      }

      Vec3 to_local (const Coop& c, const Vec3& world) {
        const Vec3 origin = c.frame.transform_point (Vec3 ());
        const Vec3 x = c.frame.transform_vector (Vec3 (1, 0, 0));
        const Vec3 z = c.frame.transform_vector (Vec3 (0, 0, 1));
        const Vec3 d = world - origin;
        return Vec3 (dot (d, x), d[1], dot (d, z));
      }

      float stride_of (Bird::Kind kind) {
        return kind == Bird::Kind::chick ? 0.05f : 0.11f;
      }

      float pace_of (Bird::Kind kind) {
        return kind == Bird::Kind::chick     ? 0.55f
               : kind == Bird::Kind::rooster ? 0.5f
                                             : 0.42f;
      }
    }
  }

  std::vector<Bird> plan_birds (std::span<const Coop> coops,
                                std::uint32_t seed) {
    using namespace hen_parts;
    std::vector<Bird> birds;
    for (std::size_t c = 0; c < coops.size (); ++c) {
      const Coop& coop = coops[c];
      const std::uint32_t h = hash (seed, static_cast<std::uint32_t> (c));
      int adult = 0;
      const auto add = [&] (Bird::Kind kind, bool outside, int mother) {
        const std::uint32_t s =
          hash (h, static_cast<std::uint32_t> (birds.size ()));
        Bird b;
        b.kind = kind;
        b.seed = s;
        b.coop = static_cast<std::uint32_t> (c);
        b.outside = outside;
        Vec3 local = outside ? somewhere (yard, unit (s, 1), unit (s, 2))
                             : somewhere (walkway, unit (s, 1), unit (s, 2));
        local[1] = outside ? 0.0f : 0.34f;
        b.mother = mother;
        if (mother >= 0) {
          const Bird& m = birds[mother];
          const float a = tau * unit (s, 3);
          b.position = m.position + Vec3 (std::cos (a), 0, std::sin (a)) * 0.3f;
          b.outside = m.outside;
        } else {
          b.position = to_world (coop, local);
        }
        b.heading = tau * unit (s, 4);
        b.left = 1.0f + 5.0f * unit (s, 5);
        if (kind != Bird::Kind::chick) {
          const int pole = adult % 2;
          b.roost = Vec3 (
            -5.5f + 0.55f * (adult / 2), perch_y[pole] + 0.02f, perch_z[pole]);
          ++adult;
        }
        birds.push_back (b);
        return static_cast<int> (birds.size () - 1);
      };
      add (Bird::Kind::rooster, true, -1);
      for (int k = 0; k < 5; ++k)
        add (Bird::Kind::hen, k % 2 == 0, -1);
      // A hen who sat on a clutch of her own, and what hatched.
      const int mother = add (Bird::Kind::hen, true, -1);
      const int brood_size = 3 + static_cast<int> (unit (h, 77) * 3.0f);
      for (int k = 0; k < brood_size; ++k)
        add (Bird::Kind::chick, true, mother);
    }
    return birds;
  }

  void Hens::rebuild (std::vector<Coop> coops, std::vector<Bird> birds) {
    m_coops = std::move (coops);
    m_birds = std::move (birds);
    m_random.resize (m_birds.size ());
    for (std::size_t i = 0; i < m_birds.size (); ++i)
      m_random[i] = m_birds[i].seed | 1u;
  }

  float Hens::rand (std::uint32_t& state) const {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float> (state >> 8) * (1.0f / 16777216.0f);
  }

  // What next: by day pecking about, a walk to somewhere else in the
  // barn or the yard, or out or in through the doors; at dusk in and up
  // onto the perch.
  void Hens::choose (Bird& b, const AnimalGround& g, bool night) {
    using namespace hen_parts;
    std::uint32_t& r = m_random[&b - m_birds.data ()];
    const Coop& coop = m_coops[b.coop];
    const Vec3 here = to_local (coop, b.position);
    const bool has_brood = std::ranges::any_of (m_birds, [&] (const Bird& o) {
      return o.mother == &b - m_birds.data ();
    });
    const auto go = [&] (Vec3 goal, bool outside) {
      b.ways = 0;
      if (b.outside && !outside) {
        b.way[b.ways++] = ramp_foot;
        b.way[b.ways++] = door_in;
      } else if (!b.outside && outside) {
        b.way[b.ways++] = door_in;
        b.way[b.ways++] = ramp_foot;
      }
      b.way[b.ways++] = goal;
      b.outside = outside;
      b.doing = Bird::Doing::walking;
      b.left = 40.0f;
    };
    if (b.kind == Bird::Kind::chick) {
      b.doing = rand (r) < 0.7f ? Bird::Doing::pecking : Bird::Doing::standing;
      b.left = 1.0f + 3.0f * rand (r);
      return;
    }
    if (night) {
      const Vec3 base =
        has_brood ? brood : Vec3 (b.roost[0], 0.0f, roost_below_z);
      Vec3 flat = here;
      flat[1] = 0.0f;
      if (!b.outside && length (flat - base) < 0.3f) {
        b.doing = has_brood ? Bird::Doing::standing : Bird::Doing::roosting;
        b.left = 30.0f;
        return;
      }
      go (base, false);
      return;
    }
    const float u = rand (r);
    if (u < 0.5f) {
      b.doing = Bird::Doing::pecking;
      b.left = 3.0f + 7.0f * rand (r);
    } else if (u < 0.62f) {
      b.doing = Bird::Doing::standing;
      b.left = 1.5f + 3.0f * rand (r);
    } else {
      // Somewhere else: as often out as in, a mother most often out.
      const bool outside =
        u < 0.82f ? b.outside : rand (r) < (has_brood ? 0.8f : 0.55f);
      Vec3 goal = outside            ? somewhere (yard, rand (r), rand (r))
                  : rand (r) < 0.25f ? somewhere (corner, rand (r), rand (r))
                                     : somewhere (walkway, rand (r), rand (r));
      if (outside) {
        const Vec3 at = to_world (coop, goal);
        if (!g.dry (at[0], at[2])) {
          b.doing = Bird::Doing::pecking;
          b.left = 2.0f;
          return;
        }
      }
      go (goal, outside);
    }
  }

  void Hens::step (const AnimalGround& g,
                   std::span<const Presence> presences,
                   const Vec3& focus,
                   float reach,
                   float hour,
                   float dt) {
    using namespace hen_parts;
    if (dt <= 0.0f)
      return;
    const bool night = hour >= 19.3f || hour < 6.2f;
    for (std::size_t i = 0; i < m_birds.size (); ++i) {
      Bird& b = m_birds[i];
      Vec3 off = b.position - focus;
      if (off[0] * off[0] + off[2] * off[2] > reach * reach)
        continue;
      const Coop& coop = m_coops[b.coop];
      std::uint32_t& r = m_random[i];
      b.left -= dt;

      // Up on the perch for the night, until the morning.
      if (b.doing == Bird::Doing::roosting) {
        b.perch =
          std::clamp (b.perch + (night ? 1.6f : -1.6f) * dt, 0.0f, 1.0f);
        Vec3 below = to_world (coop, Vec3 (b.roost[0], 0.0f, roost_below_z));
        below[1] = g.ground (below[0], below[2]);
        const Vec3 above = to_world (coop, b.roost);
        b.position = below + (above - below) * b.perch;
        const Vec3 face = to_world (coop, Vec3 (b.roost[0], 0.0f, 0.0f)) -
                          to_world (coop, Vec3 (b.roost[0], 0.0f, -1.0f));
        b.heading = heading_of (face);
        b.peck = std::max (0.0f, b.peck - 3.0f * dt);
        if (!night && b.perch <= 0.0f) {
          b.doing = Bird::Doing::standing;
          b.left = 1.0f + rand (r);
        }
        continue;
      }

      // Someone stepping among them sends them scurrying off.
      for (const Presence& p : presences) {
        Vec3 d = b.position - p.position;
        d[1] = 0.0f;
        const float near = b.kind == Bird::Kind::chick ? 1.0f : 1.4f;
        if (length2 (d) < near * near && b.doing != Bird::Doing::scurrying) {
          const Vec3 away =
            length2 (d) > 1e-6f ? normalized (d) : Vec3 (1, 0, 0);
          Vec3 goal = to_local (coop, b.position + away * 2.5f);
          goal[1] = 0.0f;
          const Vec3 here = to_local (coop, b.position);
          goal = b.outside           ? clamp_to (yard, goal)
                 : in (corner, here) ? clamp_to (corner, goal)
                                     : clamp_to (walkway, goal);
          b.way[0] = goal;
          b.ways = 1;
          b.doing = Bird::Doing::scurrying;
          b.left = 2.5f;
        }
      }

      // A chick keeps at its mother's heels.
      if (b.mother >= 0 && b.doing != Bird::Doing::scurrying) {
        const Bird& m = m_birds[b.mother];
        const float a = tau * unit (b.seed, 9) + 0.3f * std::sin (b.left);
        const Vec3 near = m.position + Vec3 (std::cos (a), 0.0f, std::sin (a)) *
                                         (0.25f + 0.3f * unit (b.seed, 10));
        Vec3 d = near - b.position;
        d[1] = 0.0f;
        b.outside = m.outside;
        if (length (d) > 0.35f) {
          b.way[0] = to_local (coop, near);
          b.ways = 1;
          b.doing = Bird::Doing::walking;
        } else if (b.doing == Bird::Doing::walking) {
          b.ways = 0;
          b.doing = Bird::Doing::pecking;
          b.left = 1.0f + 2.0f * rand (r);
        }
      }

      if (b.left < 0.0f || (b.ways == 0 && (b.doing == Bird::Doing::walking ||
                                            b.doing == Bird::Doing::scurrying)))
        choose (b, g, night);
      // Dusk overtakes whatever it was about.
      if (night && b.kind != Bird::Kind::chick &&
          b.doing != Bird::Doing::walking && b.doing != Bird::Doing::roosting &&
          b.doing != Bird::Doing::scurrying && b.left > 3.0f)
        b.left = std::min (b.left, 0.5f);

      float speed = 0.0f;
      float face = b.heading;
      float peck = 0.0f;
      switch (b.doing) {
      case Bird::Doing::walking:
      case Bird::Doing::scurrying: {
        if (b.ways == 0)
          break;
        Vec3 to = to_world (coop, b.way[0]) - b.position;
        to[1] = 0.0f;
        const float d = length (to);
        if (d < 0.15f) {
          for (int k = 1; k < b.ways; ++k)
            b.way[k - 1] = b.way[k];
          --b.ways;
          break;
        }
        face = heading_of (to);
        speed = b.doing == Bird::Doing::scurrying ? 1.7f : pace_of (b.kind);
        if (b.mother >= 0)
          speed = std::min (1.2f, 0.4f + 1.2f * d);
        break;
      }
      case Bird::Doing::pecking: {
        // Head down to the ground in quick dabs, a step or two between.
        const float beat =
          std::fmod (b.left * 1.9f + 3.0f * unit (b.seed, 11) + 1000.0f, 1.0f);
        peck = beat < 0.45f ? 1.0f : 0.0f;
        const float shuffle = std::fmod (b.left * 0.37f + 100.0f, 1.0f);
        if (shuffle < 0.12f)
          speed = 0.2f;
        face = b.heading + 0.8f * std::sin (b.left * 0.6f + b.seed % 17);
        break;
      }
      case Bird::Doing::standing:
      case Bird::Doing::roosting:
        break;
      }
      const float turn = std::remainder (face - b.heading, tau);
      b.heading += std::clamp (turn, -5.0f * dt, 5.0f * dt);
      if (std::abs (turn) > 1.2f)
        speed *= 0.3f;
      b.speed += std::clamp (speed - b.speed, -6.0f * dt, 6.0f * dt);
      b.peck += std::clamp (peck - b.peck, -9.0f * dt, 9.0f * dt);
      if (b.speed > 1e-3f) {
        Vec3 next = b.position + forward_of (b.heading) * (b.speed * dt);
        if (g.clear)
          next = g.clear (next, b.kind == Bird::Kind::chick ? 0.05f : 0.11f);
        b.gait += length (next - b.position) / stride_of (b.kind);
        b.gait = std::fmod (b.gait, 1024.0f);
        b.position = next;
      }
      b.position[1] = g.ground (b.position[0], b.position[2]);
    }
  }

  // -- drawing ------------------------------------------------------

  namespace {
    namespace hen_parts {
      struct BirdBuild {
        const model_mesh::Mesh* body;
        const model_mesh::Mesh* head;
        const model_mesh::Mesh* leg;
        Vec3 neck, hip;
      };

      const BirdBuild& build_of (Bird::Kind kind) {
        static const BirdBuild builds[3] = {
          { &hen_model::body,
            &hen_model::head,
            &hen_model::leg,
            hen_model::neck,
            hen_model::hip },
          { &rooster_model::body,
            &rooster_model::head,
            &rooster_model::leg,
            rooster_model::neck,
            rooster_model::hip },
          { &chick_model::body,
            &chick_model::head,
            &chick_model::leg,
            chick_model::neck,
            chick_model::hip },
        };
        return builds[static_cast<int> (kind)];
      }

      struct BirdMeshes {
        render::MeshPtr body, head, leg;
      };
    }
  }

  void render_birds (render::Renderer& r,
                     std::span<const Bird> birds,
                     const Vec3& camera,
                     const Vec3& forward,
                     float time,
                     float reach,
                     std::uint64_t motion_base) {
    using namespace hen_parts;
    static const std::array<BirdMeshes, 3> meshes = [&r] {
      std::array<BirdMeshes, 3> out;
      for (int k = 0; k < 3; ++k) {
        const BirdBuild& b = build_of (static_cast<Bird::Kind> (k));
        const auto bake = [&r] (const model_mesh::Mesh& mesh) {
          render::DrawList dl;
          model_mesh::record (dl, mesh);
          return r.create_mesh (dl);
        };
        out[k] = { bake (*b.body), bake (*b.head), bake (*b.leg) };
      }
      return out;
    }();
    // Which way of turning about x brings the beak down.
    const Vec3 x_axis (1, 0, 0);
    const float down = Mat4::rotation (0.3f * u::rad, x_axis)
                             .transform_vector (Vec3 (0, 0, 1))[1] < 0.0f
                         ? 1.0f
                         : -1.0f;
    for (std::size_t i = 0; i < birds.size (); ++i) {
      const Bird& b = birds[i];
      const Vec3 to = b.position - camera;
      if (length2 (to) > reach * reach || dot (to, forward) < -1.0f)
        continue;
      const BirdBuild& build = build_of (b.kind);
      const BirdMeshes& m = meshes[static_cast<int> (b.kind)];
      const Vec3 f = forward_of (b.heading);
      const Vec3 left = cross (up, f);
      const float walking = std::min (1.0f, b.speed / 0.4f);
      const float phase = tau * b.gait;
      // A little bob at each step, and a crow's stretch now and then.
      const float bob = 0.008f * walking * std::abs (std::sin (phase));
      const Mat4 frame =
        Mat4::translation (b.position + up * bob) * Mat4::basis (left, up, f);
      std::uint64_t id = motion_base ? motion_base + i * 8 : 0;
      const auto draw = [&] (const render::Mesh& mesh, const Mat4& at) {
        r.draw_mesh (mesh, at, id ? ++id : 0);
      };
      draw (*m.body, frame);
      // The head dabs down to peck; walking, it bobs back and forth.
      const float stretch =
        b.kind == Bird::Kind::rooster && b.doing == Bird::Doing::standing
          ? -0.35f * std::max (0.0f, std::sin (time * 0.7f + b.seed % 13))
          : 0.0f;
      const float pitch =
        down *
        (1.15f * b.peck + 0.18f * walking * std::sin (2.0f * phase) + stretch);
      const float glance = b.doing == Bird::Doing::standing
                             ? 0.6f * std::sin (time * 1.3f + b.seed % 29)
                             : 0.0f;
      draw (*m.head,
            frame * Mat4::translation (build.neck) *
              Mat4::rotation (glance * u::rad, up) *
              Mat4::rotation (pitch * u::rad, x_axis));
      // The legs swing in turn about the hips.
      for (int side = 0; side < 2; ++side) {
        const float swing =
          down * -0.55f * walking * std::sin (phase + side * 3.14159265f);
        const Vec3 hip = side == 0
                           ? build.hip
                           : Vec3 (-build.hip[0], build.hip[1], build.hip[2]);
        draw (*m.leg,
              frame * Mat4::translation (hip) *
                Mat4::rotation (swing * u::rad, x_axis));
      }
    }
  }

  // -- eggs ---------------------------------------------------------

  std::vector<NestState>
  first_eggs (std::size_t nests, double hours, std::uint32_t seed) {
    using namespace hen_parts;
    std::vector<NestState> out (nests);
    for (std::size_t k = 0; k < nests; ++k) {
      const auto n = static_cast<std::uint32_t> (k);
      out[k].eggs = static_cast<std::uint8_t> (1 + (hash (seed, n) & 1u));
      out[k].next_lay = hours + 1.0 + 4.0 * unit (seed ^ 0x9e57u, n);
    }
    return out;
  }

  void
  lay_eggs (std::span<NestState> states, double hours, std::uint32_t seed) {
    using namespace hen_parts;
    for (std::size_t k = 0; k < states.size (); ++k) {
      NestState& s = states[k];
      // However quickly the hours have run, each lay in its turn.
      for (int guard = 0; s.next_lay <= hours && guard < 8; ++guard) {
        if (s.eggs < nest_holds)
          ++s.eggs;
        const auto n = static_cast<std::uint32_t> (s.next_lay * 7.0);
        s.next_lay +=
          3.5 + 3.0 * unit (seed + static_cast<std::uint32_t> (k), n);
      }
      if (s.next_lay <= hours)
        s.next_lay = hours + 3.5;
    }
  }

  std::optional<std::size_t>
  nest_within_reach (std::span<const Nest> nests,
                     std::span<const NestState> states,
                     const Vec3& feet,
                     const Vec3& heading) {
    std::optional<std::size_t> found;
    float best = 1.3f * 1.3f;
    Vec3 facing = heading;
    facing[1] = 0.0f;
    facing = length2 (facing) > 1e-6f ? normalized (facing) : Vec3 (0, 0, 1);
    for (std::size_t k = 0; k < nests.size () && k < states.size (); ++k) {
      if (states[k].eggs == 0)
        continue;
      Vec3 d = nests[k].at - feet;
      if (std::abs (static_cast<float> (d[1])) > 1.6f)
        continue;
      d[1] = 0.0f;
      const float d2 = length2 (d);
      if (d2 >= best || (d2 > 0.04f && dot (d, facing) < 0.2f * std::sqrt (d2)))
        continue;
      best = d2;
      found = k;
    }
    return found;
  }

  void draw_egg (render::DrawList& dl,
                 const Vec3& at,
                 const Vec3& along,
                 std::uint32_t seed) {
    using namespace hen_parts;
    // A blommehöna's egg is cream, now and then a little tinted.
    const float tint = unit (seed, 1);
    dl.color (0.93f - 0.05f * tint, 0.89f - 0.09f * tint, 0.80f - 0.14f * tint);
    const Vec3 a = length2 (along) > 1e-6f ? normalized (along) : up;
    Vec3 u = cross (a, std::abs (a[1]) < 0.9f ? up : Vec3 (1, 0, 0));
    u = normalized (u);
    const Vec3 v = cross (a, u);
    constexpr int slices = 10, stacks = 7;
    constexpr float half = 0.04f, round = 0.029f;
    const auto point = [&] (int i, int j, Vec3& normal) {
      const float t = 3.14159265f * j / stacks;
      const float s = tau * i / slices;
      // Fuller at the blunt end.
      const float c = std::cos (t);
      const float w = round * std::sin (t) * (1.0f + 0.12f * c);
      const Vec3 radial = u * std::cos (s) + v * std::sin (s);
      normal = normalized (radial * std::sin (t) * (half / round) + a * c);
      return at + a * (half * c) + radial * w;
    };
    for (int j = 0; j < stacks; ++j)
      for (int i = 0; i < slices; ++i) {
        Vec3 n00, n10, n01, n11;
        const Vec3 p00 = point (i, j, n00), p10 = point (i + 1, j, n10);
        const Vec3 p01 = point (i, j + 1, n01), p11 = point (i + 1, j + 1, n11);
        dl.normal (n00);
        dl.vertex (p00);
        dl.normal (n01);
        dl.vertex (p01);
        dl.normal (n11);
        dl.vertex (p11);
        dl.normal (n00);
        dl.vertex (p00);
        dl.normal (n11);
        dl.vertex (p11);
        dl.normal (n10);
        dl.vertex (p10);
      }
  }

  void draw_fried_egg (render::DrawList& dl,
                       const Vec3& at,
                       float yaw,
                       float cooked,
                       std::uint32_t seed) {
    using namespace hen_parts;
    // The white spreads and sets, browning lacy at its edge, then burns.
    const float done = std::clamp (cooked, 0.0f, 1.0f);
    const float burnt = std::clamp (cooked - 1.0f, 0.0f, 1.0f);
    const auto mix = [] (float a, float b, float t) { return a + (b - a) * t; };
    const DisplayColor raw (0.80f, 0.80f, 0.74f);
    const DisplayColor set (0.97f, 0.96f, 0.91f);
    const DisplayColor brown (0.62f, 0.42f, 0.18f);
    const DisplayColor black (0.10f, 0.08f, 0.06f);
    const auto white = [&] (float edge) {
      DisplayColor c (mix (raw.red, set.red, done),
                      mix (raw.green, set.green, done),
                      mix (raw.blue, set.blue, done));
      const float lace = edge * done * 0.8f;
      c = DisplayColor (mix (c.red, brown.red, lace),
                        mix (c.green, brown.green, lace),
                        mix (c.blue, brown.blue, lace));
      return DisplayColor (
        mix (c.red, black.red, burnt * (0.5f + 0.5f * edge)),
        mix (c.green, black.green, burnt * (0.5f + 0.5f * edge)),
        mix (c.blue, black.blue, burnt * (0.5f + 0.5f * edge)));
    };
    constexpr int rim = 14;
    const float spread = 0.042f + 0.012f * done;
    const auto edge = [&] (int i) {
      const float a = yaw + tau * i / rim;
      const float r =
        spread *
        (0.8f + 0.35f * unit (seed, static_cast<std::uint32_t> (i % rim)));
      return at + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
    };
    dl.normal (up);
    for (int i = 0; i < rim; ++i) {
      dl.color (white (0.0f));
      dl.vertex (at + up * 0.006f);
      dl.color (white (1.0f));
      dl.vertex (edge (i + 1));
      dl.vertex (edge (i));
    }
    // The yolk, a low dome a little off the middle.
    const Vec3 yolk =
      at + Vec3 (std::cos (yaw), 0.0f, std::sin (yaw)) * 0.008f + up * 0.004f;
    const float set_yolk = 0.6f * done;
    dl.color (mix (0.98f, 0.92f, set_yolk) * (1.0f - 0.8f * burnt),
              mix (0.64f, 0.70f, set_yolk) * (1.0f - 0.8f * burnt),
              mix (0.10f, 0.30f, set_yolk) * (1.0f - 0.8f * burnt));
    constexpr int ring = 10;
    constexpr float radius = 0.017f, height = 0.011f;
    for (int i = 0; i < ring; ++i) {
      const float a0 = tau * i / ring, a1 = tau * (i + 1) / ring;
      const Vec3 e0 (std::cos (a0), 0.0f, std::sin (a0));
      const Vec3 e1 (std::cos (a1), 0.0f, std::sin (a1));
      const Vec3 m0 = yolk + e0 * (radius * 0.7f) + up * (height * 0.75f);
      const Vec3 m1 = yolk + e1 * (radius * 0.7f) + up * (height * 0.75f);
      dl.normal (normalized (e0 + up * 0.6f));
      dl.vertex (yolk + e0 * radius);
      dl.vertex (yolk + e1 * radius);
      dl.vertex (m1);
      dl.vertex (yolk + e0 * radius);
      dl.vertex (m1);
      dl.vertex (m0);
      dl.normal (up);
      dl.vertex (yolk + up * height);
      dl.vertex (m0);
      dl.vertex (m1);
    }
  }

  void draw_nest_eggs (render::DrawList& dl,
                       std::span<const Nest> nests,
                       std::span<const NestState> states,
                       const Vec3& camera) {
    using namespace hen_parts;
    bool open = false;
    for (std::size_t k = 0; k < nests.size () && k < states.size (); ++k) {
      if (states[k].eggs == 0 || length2 (nests[k].at - camera) > 60.0f * 60.0f)
        continue;
      if (!open) {
        dl.state (render::DrawState ());
        dl.lit (true);
        dl.fogged (true);
        dl.begin (render::Prim::Triangles);
        open = true;
      }
      const Vec3 side = cross (up, nests[k].out);
      for (int e = 0; e < states[k].eggs; ++e) {
        const auto s = hash (static_cast<std::uint32_t> (k) * 31u, e);
        const float a = tau * (e / 3.0f + 0.1f * unit (s, 2));
        const Vec3 at = nests[k].at + side * (0.05f * std::cos (a)) +
                        nests[k].out * (0.05f * std::sin (a)) + up * 0.02f;
        draw_egg (dl,
                  at,
                  normalized (side * std::cos (a + 1.3f) +
                              nests[k].out * std::sin (a + 1.3f) + up * 0.15f),
                  s);
      }
    }
    if (open)
      dl.end ();
  }
}
