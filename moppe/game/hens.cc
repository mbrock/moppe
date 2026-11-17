#include <moppe/game/hens.hh>

#include <moppe/game/chick_model.hh>
#include <moppe/game/hen_model.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/rooster_model.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    namespace hen_parts {
      constexpr float tau = 6.28318531f;
      constexpr radians_t turn = tau * u::rad;
      const direction_t up = unit_direction (Vec3 (0.0f, 1.0f, 0.0f));
      const direction_t east = unit_direction (Vec3 (1.0f, 0.0f, 0.0f));

      // What moves a place by an offset (docs/typed-quantities.md).
      auto shift (const displacement_t& d) {
        return quantity_cast<isq::position_vector> (d);
      }

      // The way `v` points; it must have some length.
      template <Quantity Q>
      direction_t unit_of (const Q& v) {
        return unit_direction (normalized (v.numerical_value_ref_in (Q::unit)));
      }

      // The way round the up axis `a` turns from east.
      direction_t compass (radians_t a) {
        return unit_direction (Vec3 (libm::cos (a), 0.0f, libm::sin (a)));
      }

      // The whole laps of the world from `at` toward the copy of it
      // nearest `eye`.
      displacement_t laps_toward (const position_t& at,
                                  const position_t& eye,
                                  const plan_extent_t& period) {
        const Vec3& a = position_value (at);
        const Vec3& e = position_value (eye);
        const Vec2& n = plan_value (period);
        return displacement (
          Vec3 (terrain::whole_periods (e[0] - a[0], n.x ()),
                0.0f,
                terrain::whole_periods (e[2] - a[2], n.z ())));
      }

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

      direction_t forward_of (heading_t heading) {
        const float h = heading.numerical_value_in (u::rad);
        return unit_direction (Vec3 (std::sin (h), 0.0f, std::cos (h)));
      }

      heading_t heading_of (const plan_displacement_t& d) {
        return std::atan2 (plan_value (d).x (), plan_value (d).z ()) *
               heading[u::rad];
      }

      // From one place to another, seen from above.
      plan_displacement_t plan_between (const position_t& from,
                                        const position_t& to) {
        return plan_displacement (
          plan (position_value (to) - position_value (from)));
      }

      // Brought down to the floor of the barn's frame.
      displacement_t on_floor (const displacement_t& d) {
        const Vec3& v = displacement_value (d);
        return displacement (Vec3 (v[0], 0.0f, v[2]));
      }

      // Where the birds go, in the barn's frame (tools/figure/farm.py):
      // the floor between the stalls and the doors, the hens' corner by
      // the nest boxes, and the yard beyond the ramp; and the way between
      // them, through the doorway and down the ramp.
      struct Area {
        meters_t x0, x1, z0, z1;
      };
      constexpr Area walkway {
        -5.3f * u::m, 5.3f * u::m, -0.4f * u::m, 3.4f * u::m
      };
      constexpr Area corner {
        -5.15f * u::m, -3.8f * u::m, -3.3f * u::m, -0.4f * u::m
      };
      constexpr Area yard {
        -8.0f * u::m, 8.0f * u::m, 6.9f * u::m, 12.0f * u::m
      };
      const displacement_t door_in = displacement (Vec3 (0.0f, 0.0f, 3.5f));
      const displacement_t ramp_foot = displacement (Vec3 (0.0f, 0.0f, 6.4f));
      // Where a mother hen settles with her brood for the night.
      const displacement_t brood = displacement (Vec3 (-4.3f, 0.0f, -2.0f));
      // The two perches: their height in the barn's frame and their z.
      constexpr meters_t perch_y[2] = { 1.05f * u::m, 1.35f * u::m };
      constexpr meters_t perch_z[2] = { -3.25f * u::m, -3.6f * u::m };
      constexpr meters_t roost_below_z = -2.75f * u::m;

      bool in (const Area& a, const displacement_t& p) {
        const meters_t x = displacement_value (p)[0] * u::m;
        const meters_t z = displacement_value (p)[2] * u::m;
        return x >= a.x0 && x <= a.x1 && z >= a.z0 && z <= a.z1;
      }

      displacement_t clamp_to (const Area& a, const displacement_t& p) {
        const Vec3& v = displacement_value (p);
        return displacement (
          Vec3 (meters_value (std::clamp (v[0] * u::m, a.x0, a.x1)),
                v[1],
                meters_value (std::clamp (v[2] * u::m, a.z0, a.z1))));
      }

      // A point of an area, `across` of the way over it in x and `deep`
      // of the way in z.
      displacement_t somewhere (const Area& a, float across, float deep) {
        return displacement (Vec3 (meters_value (a.x0 + (a.x1 - a.x0) * across),
                                   0.0f,
                                   meters_value (a.z0 + (a.z1 - a.z0) * deep)));
      }

      position_t to_world (const Coop& c, const displacement_t& local) {
        return position (c.frame.transform_point (displacement_value (local)));
      }

      displacement_t to_local (const Coop& c, const position_t& world) {
        const Vec3 origin = c.frame.transform_point (Vec3 ());
        const Vec3 x = c.frame.transform_vector (Vec3 (1, 0, 0));
        const Vec3 z = c.frame.transform_vector (Vec3 (0, 0, 1));
        const Vec3 d = position_value (world) - origin;
        return displacement (Vec3 (dot (d, x), d[1], dot (d, z)));
      }

      meters_t stride_of (Bird::Kind kind) {
        return (kind == Bird::Kind::chick ? 0.05f : 0.11f) * u::m;
      }

      speed_t pace_of (Bird::Kind kind) {
        return (kind == Bird::Kind::chick     ? 0.55f
                : kind == Bird::Kind::rooster ? 0.5f
                                              : 0.42f) *
               u::m / u::s;
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
        const displacement_t spot =
          outside ? somewhere (yard, unit (s, 1), unit (s, 2))
                  : somewhere (walkway, unit (s, 1), unit (s, 2));
        const displacement_t local =
          displacement (Vec3 (displacement_value (spot)[0],
                              outside ? 0.0f : 0.34f,
                              displacement_value (spot)[2]));
        b.mother = mother;
        if (mother >= 0) {
          const Bird& m = birds[mother];
          const radians_t a = tau * unit (s, 3) * u::rad;
          const float turn = radians_value (a);
          b.position =
            position (position_value (m.position) +
                      Vec3 (std::cos (turn), 0, std::sin (turn)) * 0.3f);
          b.outside = m.outside;
        } else {
          b.position = to_world (coop, local);
        }
        b.heading = tau * unit (s, 4) * u::rad;
        b.left = (1.0f + 5.0f * unit (s, 5)) * u::s;
        if (kind != Bird::Kind::chick) {
          const int pole = adult % 2;
          b.roost = displacement (Vec3 (-5.5f + 0.55f * (adult / 2),
                                        meters_value (perch_y[pole]) + 0.02f,
                                        meters_value (perch_z[pole])));
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
    const displacement_t here = to_local (coop, b.position);
    const bool has_brood = std::ranges::any_of (m_birds, [&] (const Bird& o) {
      return o.mother == &b - m_birds.data ();
    });
    const auto go = [&] (const displacement_t& goal, bool outside) {
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
      b.left = 40.0f * u::s;
    };
    if (b.kind == Bird::Kind::chick) {
      b.doing = rand (r) < 0.7f ? Bird::Doing::pecking : Bird::Doing::standing;
      b.left = (1.0f + 3.0f * rand (r)) * u::s;
      return;
    }
    if (night) {
      const displacement_t base =
        has_brood ? brood
                  : displacement (Vec3 (displacement_value (b.roost)[0],
                                        0.0f,
                                        meters_value (roost_below_z)));
      const displacement_t flat = on_floor (here);
      if (!b.outside && length (displacement_value (flat - base)) < 0.3f) {
        b.doing = has_brood ? Bird::Doing::standing : Bird::Doing::roosting;
        b.left = 30.0f * u::s;
        return;
      }
      go (base, false);
      return;
    }
    const float u = rand (r);
    if (u < 0.5f) {
      b.doing = Bird::Doing::pecking;
      b.left = (3.0f + 7.0f * rand (r)) * u::s;
    } else if (u < 0.62f) {
      b.doing = Bird::Doing::standing;
      b.left = (1.5f + 3.0f * rand (r)) * u::s;
    } else {
      // Somewhere else: as often out as in, a mother most often out.
      const bool outside =
        u < 0.82f ? b.outside : rand (r) < (has_brood ? 0.8f : 0.55f);
      const displacement_t goal = outside ? somewhere (yard, rand (r), rand (r))
                                  : rand (r) < 0.25f
                                    ? somewhere (corner, rand (r), rand (r))
                                    : somewhere (walkway, rand (r), rand (r));
      if (outside) {
        const position_t at = to_world (coop, goal);
        if (!g.dry (plan (at))) {
          b.doing = Bird::Doing::pecking;
          b.left = 2.0f * u::s;
          return;
        }
      }
      go (goal, outside);
    }
  }

  void Hens::step (const AnimalGround& g,
                   std::span<const Presence> presences,
                   const position_t& focus,
                   meters_t reach,
                   hours_t hour,
                   seconds_t interval) {
    using namespace hen_parts;
    // The birds' rates are tuned per second, applied over this many.
    const float dt = seconds_value (interval);
    if (dt <= 0.0f)
      return;
    const bool night = hour >= 19.3f * si::hour || hour < 6.2f * si::hour;
    for (std::size_t i = 0; i < m_birds.size (); ++i) {
      Bird& b = m_birds[i];
      const plan_displacement_t off =
        terrain::torus_offset (plan (focus), plan (b.position), g.period);
      if (length2 (plan_value (off)) >
          meters_value (reach) * meters_value (reach))
        continue;
      const Coop& coop = m_coops[b.coop];
      std::uint32_t& r = m_random[i];
      b.left -= interval;

      // Up on the perch for the night, until the morning.
      if (b.doing == Bird::Doing::roosting) {
        b.perch =
          std::clamp (b.perch + (night ? 1.6f : -1.6f) * dt * proportion[one],
                      0.0f * proportion[one],
                      1.0f * proportion[one]);
        position_t below =
          to_world (coop,
                    displacement (Vec3 (displacement_value (b.roost)[0],
                                        0.0f,
                                        meters_value (roost_below_z))));
        below = terrain::at_elevation (plan (below), g.ground (plan (below)));
        const position_t above = to_world (coop, b.roost);
        b.position =
          position (position_value (below) +
                    (position_value (above) - position_value (below)) *
                      b.perch.numerical_value_in (one));
        const plan_displacement_t face = plan_between (
          to_world (
            coop,
            displacement (Vec3 (displacement_value (b.roost)[0], 0.0f, -1.0f))),
          to_world (
            coop,
            displacement (Vec3 (displacement_value (b.roost)[0], 0.0f, 0.0f))));
        b.heading = heading_of (face);
        b.peck = std::max (0.0f * proportion[one],
                           b.peck - 3.0f * dt * proportion[one]);
        if (!night && b.perch <= 0.0f * proportion[one]) {
          b.doing = Bird::Doing::standing;
          b.left = (1.0f + rand (r)) * u::s;
        }
        continue;
      }

      // Someone stepping among them sends them scurrying off.
      for (const Presence& p : presences) {
        const plan_displacement_t d = terrain::torus_offset (
          plan (p.position), plan (b.position), g.period);
        const meters_t near =
          (b.kind == Bird::Kind::chick ? 1.0f : 1.4f) * u::m;
        if (length2 (plan_value (d)) <
              meters_value (near) * meters_value (near) &&
            b.doing != Bird::Doing::scurrying) {
          const Vec3 away = length2 (plan_value (d)) > 1e-6f
                              ? lift (normalized (plan_value (d)))
                              : Vec3 (1, 0, 0);
          const displacement_t goal = on_floor (to_local (
            coop, position (position_value (b.position) + away * 2.5f)));
          const displacement_t here = to_local (coop, b.position);
          b.way[0] = b.outside           ? clamp_to (yard, goal)
                     : in (corner, here) ? clamp_to (corner, goal)
                                         : clamp_to (walkway, goal);
          b.ways = 1;
          b.doing = Bird::Doing::scurrying;
          b.left = 2.5f * u::s;
        }
      }

      // A chick keeps at its mother's heels.
      if (b.mother >= 0 && b.doing != Bird::Doing::scurrying) {
        const Bird& m = m_birds[b.mother];
        const radians_t a =
          (tau * unit (b.seed, 9) + 0.3f * std::sin (seconds_value (b.left))) *
          u::rad;
        const float turn = radians_value (a);
        const position_t near =
          position (position_value (m.position) +
                    Vec3 (std::cos (turn), 0.0f, std::sin (turn)) *
                      (0.25f + 0.3f * unit (b.seed, 10)));
        b.outside = m.outside;
        if (length (plan_between (b.position, near)) > 0.35f * u::m) {
          b.way[0] = to_local (coop, near);
          b.ways = 1;
          b.doing = Bird::Doing::walking;
        } else if (b.doing == Bird::Doing::walking) {
          b.ways = 0;
          b.doing = Bird::Doing::pecking;
          b.left = (1.0f + 2.0f * rand (r)) * u::s;
        }
      }

      if (b.left < seconds_t {} ||
          (b.ways == 0 && (b.doing == Bird::Doing::walking ||
                           b.doing == Bird::Doing::scurrying)))
        choose (b, g, night);
      // Dusk overtakes whatever it was about.
      if (night && b.kind != Bird::Kind::chick &&
          b.doing != Bird::Doing::walking && b.doing != Bird::Doing::roosting &&
          b.doing != Bird::Doing::scurrying && b.left > 3.0f * u::s)
        b.left = std::min (b.left, 0.5f * u::s);

      speed_t speed {};
      heading_t face = b.heading;
      proportion_t peck = 0.0f * proportion[one];
      switch (b.doing) {
      case Bird::Doing::walking:
      case Bird::Doing::scurrying: {
        if (b.ways == 0)
          break;
        const plan_displacement_t to =
          plan_between (b.position, to_world (coop, b.way[0]));
        const meters_t d = length (to);
        if (d < 0.15f * u::m) {
          for (int k = 1; k < b.ways; ++k)
            b.way[k - 1] = b.way[k];
          --b.ways;
          break;
        }
        face = heading_of (to);
        speed = b.doing == Bird::Doing::scurrying ? 1.7f * u::m / u::s
                                                  : pace_of (b.kind);
        if (b.mother >= 0)
          speed = std::min (1.2f, 0.4f + 1.2f * meters_value (d)) * u::m / u::s;
        break;
      }
      case Bird::Doing::pecking: {
        // Head down to the ground in quick dabs, a step or two between.
        const proportion_t beat =
          std::fmod (seconds_value (b.left) * 1.9f + 3.0f * unit (b.seed, 11) +
                       1000.0f,
                     1.0f) *
          proportion[one];
        peck = (beat < 0.45f * proportion[one] ? 1.0f : 0.0f) * proportion[one];
        const proportion_t shuffle =
          std::fmod (seconds_value (b.left) * 0.37f + 100.0f, 1.0f) *
          proportion[one];
        if (shuffle < 0.12f * proportion[one])
          speed = 0.2f * u::m / u::s;
        face = b.heading +
               0.8f * std::sin (seconds_value (b.left) * 0.6f + b.seed % 17) *
                 u::rad;
        break;
      }
      case Bird::Doing::standing:
      case Bird::Doing::roosting:
        break;
      }
      const radians_t turn =
        std::remainder ((face - b.heading).numerical_value_in (u::rad), tau) *
        u::rad;
      b.heading += std::clamp (turn, -5.0f * dt * u::rad, 5.0f * dt * u::rad);
      if (abs (turn) > 1.2f * u::rad)
        speed *= 0.3f;
      b.speed += std::clamp<speed_t> (
        speed - b.speed, -6.0f * dt * u::m / u::s, 6.0f * dt * u::m / u::s);
      b.peck += std::clamp (peck - b.peck,
                            -9.0f * dt * proportion[one],
                            9.0f * dt * proportion[one]);
      if (b.speed > 1e-3f * u::m / u::s) {
        position_t next = position (position_value (b.position) +
                                    direction_value (forward_of (b.heading)) *
                                      (speed_value (b.speed) * dt));
        if (g.clear)
          next = g.clear (next,
                          (b.kind == Bird::Kind::chick ? 0.05f : 0.11f) * u::m);
        b.gait += length (position_value (next) - position_value (b.position)) /
                  meters_value (stride_of (b.kind)) * isq::rotation[one];
        b.gait = fmod (b.gait, 1024.0f * isq::rotation[one]);
        b.position = next;
      }
      b.position =
        terrain::at_elevation (plan (b.position), g.ground (plan (b.position)));
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
                     const position_t& camera,
                     const direction_t& forward,
                     seconds_t time,
                     meters_t reach,
                     const plan_extent_t& period,
                     std::uint64_t motion_base) {
    using namespace hen_parts;
    // Into the meshes' matrices, in metres and seconds.
    const Vec3& up_axis = direction_value (up);
    const float clock = seconds_value (time);
    static const std::array<BirdMeshes, 3> meshes = [&r] {
      std::array<BirdMeshes, 3> out;
      for (int k = 0; k < 3; ++k) {
        const BirdBuild& b = build_of (static_cast<Bird::Kind> (k));
        out[k] = { model_mesh::bake (r, *b.body),
                   model_mesh::bake (r, *b.head),
                   model_mesh::bake (r, *b.leg) };
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
      const displacement_t lap = laps_toward (b.position, camera, period);
      const displacement_t to = b.position + shift (lap) - camera;
      if (dot (to, to) > reach * reach ||
          quantity_cast<isq::length> (dot (to, forward)) < -1.0f * u::m)
        continue;
      const BirdBuild& build = build_of (b.kind);
      const BirdMeshes& m = meshes[static_cast<int> (b.kind)];
      const direction_t f = forward_of (b.heading);
      const direction_t left = quantity_cast<direction> (cross (up, f));
      const proportion_t walking =
        std::min (1.0f, speed_value (b.speed) / 0.4f) * proportion[one];
      const float stepping = walking.numerical_value_in (one);
      const radians_t phase = tau * b.gait.numerical_value_in (one) * u::rad;
      const float cycle = radians_value (phase);
      // A little bob at each step, and a crow's stretch now and then.
      const meters_t bob =
        0.008f * stepping * std::abs (std::sin (cycle)) * u::m;
      const Mat4 frame =
        Mat4::translation (
          position_value (b.position + shift (lap) + shift (along (up, bob)))) *
        Mat4::basis (direction_value (left), up_axis, direction_value (f));
      std::uint64_t id = motion_base ? motion_base + i * 8 : 0;
      const auto draw = [&] (const render::Mesh& mesh, const Mat4& at) {
        r.draw_mesh (mesh, at, id ? ++id : 0);
      };
      draw (*m.body, frame);
      // The head dabs down to peck; walking, it bobs back and forth.
      const radians_t stretch =
        (b.kind == Bird::Kind::rooster && b.doing == Bird::Doing::standing
           ? -0.35f * std::max (0.0f, std::sin (clock * 0.7f + b.seed % 13))
           : 0.0f) *
        u::rad;
      const radians_t pitch =
        down *
        (1.15f * b.peck.numerical_value_in (one) +
         0.18f * stepping * std::sin (2.0f * cycle) + radians_value (stretch)) *
        u::rad;
      const radians_t glance = (b.doing == Bird::Doing::standing
                                  ? 0.6f * std::sin (clock * 1.3f + b.seed % 29)
                                  : 0.0f) *
                               u::rad;
      draw (*m.head,
            frame * Mat4::translation (build.neck) *
              Mat4::rotation (glance, up_axis) *
              Mat4::rotation (pitch, x_axis));
      // The legs swing in turn about the hips.
      for (int side = 0; side < 2; ++side) {
        const radians_t swing = down * -0.55f * stepping *
                                std::sin (cycle + side * 3.14159265f) * u::rad;
        const Vec3 hip = side == 0
                           ? build.hip
                           : Vec3 (-build.hip[0], build.hip[1], build.hip[2]);
        draw (*m.leg,
              frame * Mat4::translation (hip) * Mat4::rotation (swing, x_axis));
      }
    }
  }

  // -- eggs ---------------------------------------------------------

  std::vector<NestState>
  first_eggs (std::size_t nests, clock_time_t hours, std::uint32_t seed) {
    using namespace hen_parts;
    std::vector<NestState> out (nests);
    for (std::size_t k = 0; k < nests; ++k) {
      const auto n = static_cast<std::uint32_t> (k);
      out[k].eggs = static_cast<std::uint8_t> (1 + (hash (seed, n) & 1u));
      out[k].next_lay =
        hours + (1.0 + 4.0 * unit (seed ^ 0x9e57u, n)) * si::hour;
    }
    return out;
  }

  void lay_eggs (std::span<NestState> states,
                 clock_time_t hours,
                 std::uint32_t seed) {
    using namespace hen_parts;
    for (std::size_t k = 0; k < states.size (); ++k) {
      NestState& s = states[k];
      // However quickly the hours have run, each lay in its turn.
      for (int guard = 0; s.next_lay <= hours && guard < 8; ++guard) {
        if (s.eggs < nest_holds)
          ++s.eggs;
        const auto n =
          static_cast<std::uint32_t> (hours_since_midnight (s.next_lay) * 7.0);
        s.next_lay +=
          (3.5 + 3.0 * unit (seed + static_cast<std::uint32_t> (k), n)) *
          si::hour;
      }
      if (s.next_lay <= hours)
        s.next_lay = hours + 3.5 * si::hour;
    }
  }

  std::optional<std::size_t>
  nest_within_reach (std::span<const Nest> nests,
                     std::span<const NestState> states,
                     const position_t& feet,
                     const direction_t& heading,
                     const plan_extent_t& period) {
    std::optional<std::size_t> found;
    square_meters_t best = 1.3f * 1.3f * u::m * u::m;
    const Vec2 flat = plan (direction_value (heading));
    const plan_direction_t facing = plan_unit_direction (
      length2 (flat) > 1e-6f ? normalized (flat) : Vec2 (0, 1));
    for (std::size_t k = 0; k < nests.size () && k < states.size (); ++k) {
      if (states[k].eggs == 0)
        continue;
      const displacement_t reach =
        displacement (position_value (nests[k].at) - position_value (feet));
      if (std::abs (displacement_value (reach)[1]) * u::m > 1.6f * u::m)
        continue;
      const plan_displacement_t d =
        terrain::minimum_image (plan (reach), period);
      const square_meters_t d2 = length2 (plan_value (d)) * u::m * u::m;
      if (d2 >= best ||
          (d2 > 0.04f * u::m * u::m &&
           dot (plan_value (d), plan_value (facing)) * u::m < 0.2f * sqrt (d2)))
        continue;
      best = d2;
      found = k;
    }
    return found;
  }

  void draw_egg (render::DrawList& dl,
                 const position_t& at,
                 const direction_t& along_way,
                 std::uint32_t seed) {
    using namespace hen_parts;
    // A blommehöna's egg is cream, now and then a little tinted.
    const float tint = unit (seed, 1);
    dl.color (0.93f - 0.05f * tint, 0.89f - 0.09f * tint, 0.80f - 0.14f * tint);
    const direction_t a =
      length2 (direction_value (along_way)) > 1e-6f ? unit_of (along_way) : up;
    const direction_t u =
      unit_of (cross (a, std::abs (direction_value (a)[1]) < 0.9f ? up : east));
    const direction_t v = quantity_cast<direction> (cross (a, u));
    constexpr int slices = 10, stacks = 7;
    constexpr meters_t half = 0.04f * u::m, round = 0.029f * u::m;
    const auto point = [&] (int i, int j, direction_t& normal) {
      const radians_t t = 3.14159265f * u::rad * j / stacks;
      const radians_t s = turn * i / slices;
      // Fuller at the blunt end.
      const float c = libm::cos (t);
      const meters_t w = round * libm::sin (t) * (1.0f + 0.12f * c);
      const direction_t radial = u * libm::cos (s) + v * libm::sin (s);
      normal = unit_of (radial * libm::sin (t) * (half / round) + a * c);
      return at + shift (along (a, half * c)) + shift (along (radial, w));
    };
    for (int j = 0; j < stacks; ++j)
      for (int i = 0; i < slices; ++i) {
        direction_t n00, n10, n01, n11;
        const position_t p00 = point (i, j, n00), p10 = point (i + 1, j, n10);
        const position_t p01 = point (i, j + 1, n01),
                         p11 = point (i + 1, j + 1, n11);
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
                       const position_t& at,
                       radians_t yaw,
                       quantity<doneness[one], float> cooked_q,
                       std::uint32_t seed) {
    using namespace hen_parts;
    // The white spreads and sets, browning lacy at its edge, then burns.
    const float cooked = cooked_q.numerical_value_in (one);
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
    const meters_t spread = (0.042f + 0.012f * done) * u::m;
    const auto edge = [&] (int i) {
      const radians_t a = yaw + turn * i / rim;
      const meters_t r =
        spread *
        (0.8f + 0.35f * unit (seed, static_cast<std::uint32_t> (i % rim)));
      return at + shift (along (compass (a), r));
    };
    dl.normal (up);
    for (int i = 0; i < rim; ++i) {
      dl.color (white (0.0f));
      dl.vertex (at + shift (along (up, 0.006f * u::m)));
      dl.color (white (1.0f));
      dl.vertex (edge (i + 1));
      dl.vertex (edge (i));
    }
    // The yolk, a low dome a little off the middle.
    const position_t yolk = at + shift (along (compass (yaw), 0.008f * u::m)) +
                            shift (along (up, 0.004f * u::m));
    const float set_yolk = 0.6f * done;
    dl.color (mix (0.98f, 0.92f, set_yolk) * (1.0f - 0.8f * burnt),
              mix (0.64f, 0.70f, set_yolk) * (1.0f - 0.8f * burnt),
              mix (0.10f, 0.30f, set_yolk) * (1.0f - 0.8f * burnt));
    constexpr int ring = 10;
    constexpr meters_t radius = 0.017f * u::m, height = 0.011f * u::m;
    for (int i = 0; i < ring; ++i) {
      const radians_t a0 = turn * i / ring, a1 = turn * (i + 1) / ring;
      const direction_t e0 = compass (a0);
      const direction_t e1 = compass (a1);
      const position_t m0 = yolk + shift (along (e0, radius * 0.7f)) +
                            shift (along (up, height * 0.75f));
      const position_t m1 = yolk + shift (along (e1, radius * 0.7f)) +
                            shift (along (up, height * 0.75f));
      dl.normal (unit_of (e0 + up * 0.6f));
      dl.vertex (yolk + shift (along (e0, radius)));
      dl.vertex (yolk + shift (along (e1, radius)));
      dl.vertex (m1);
      dl.vertex (yolk + shift (along (e0, radius)));
      dl.vertex (m1);
      dl.vertex (m0);
      dl.normal (up);
      dl.vertex (yolk + shift (along (up, height)));
      dl.vertex (m0);
      dl.vertex (m1);
    }
  }

  void draw_nest_eggs (render::DrawList& dl,
                       std::span<const Nest> nests,
                       std::span<const NestState> states,
                       const position_t& camera,
                       const plan_extent_t& period) {
    using namespace hen_parts;
    bool open = false;
    for (std::size_t k = 0; k < nests.size () && k < states.size (); ++k) {
      // The box at the copy of the world nearest the camera.
      const direction_t& out = nests[k].out;
      const position_t box =
        nests[k].at + shift (laps_toward (nests[k].at, camera, period));
      const displacement_t seen = box - camera;
      if (states[k].eggs == 0 ||
          dot (seen, seen) > (60.0f * u::m) * (60.0f * u::m))
        continue;
      if (!open) {
        dl.state (render::DrawState ());
        dl.lit (true);
        dl.fogged (true);
        dl.begin (render::Prim::Triangles);
        open = true;
      }
      const direction_t side = quantity_cast<direction> (cross (up, out));
      for (int e = 0; e < states[k].eggs; ++e) {
        const auto s = hash (static_cast<std::uint32_t> (k) * 31u, e);
        const radians_t a = turn * (e / 3.0f + 0.1f * unit (s, 2));
        const position_t at =
          box + shift (along (side, 0.05f * libm::cos (a) * u::m)) +
          shift (along (out, 0.05f * libm::sin (a) * u::m)) +
          shift (along (up, 0.02f * u::m));
        draw_egg (dl,
                  at,
                  unit_of (side * libm::cos (a + 1.3f * u::rad) +
                           out * libm::sin (a + 1.3f * u::rad) + up * 0.15f),
                  s);
      }
    }
    if (open)
      dl.end ();
  }
}
