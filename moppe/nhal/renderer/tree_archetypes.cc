#include <moppe/nhal/renderer/tree_archetypes.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace moppe::nhal::detail {
  namespace {
    constexpr float pi = 3.14159265358979323846f;
    constexpr float two_pi = 2.0f * pi;
    constexpr float golden_angle = 2.39996322972865332f;

    struct Vec3f {
      float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    Vec3f operator+ (Vec3f a, Vec3f b) {
      return { a.x + b.x, a.y + b.y, a.z + b.z };
    }
    Vec3f operator- (Vec3f a, Vec3f b) {
      return { a.x - b.x, a.y - b.y, a.z - b.z };
    }
    Vec3f operator* (Vec3f a, float s) {
      return { a.x * s, a.y * s, a.z * s };
    }

    float dot3 (Vec3f a, Vec3f b) {
      return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3f cross3 (Vec3f a, Vec3f b) {
      return { a.y * b.z - a.z * b.y,
               a.z * b.x - a.x * b.z,
               a.x * b.y - a.y * b.x };
    }

    float len3 (Vec3f v) {
      return std::sqrt (dot3 (v, v));
    }

    Vec3f norm3 (Vec3f v) {
      const float l = len3 (v);
      return l > 1e-6f ? v * (1.0f / l) : Vec3f { 0.0f, 1.0f, 0.0f };
    }

    Vec3f mix3 (Vec3f a, Vec3f b, float t) {
      return a * (1.0f - t) + b * t;
    }

    float clampf (float x, float lo, float hi) {
      return std::clamp (x, lo, hi);
    }

    float lerpf (float a, float b, float t) {
      return a + (b - a) * t;
    }

    // Deterministic splitmix-style PRNG for repeatable archetypes across
    // platforms.
    struct Rng {
      std::uint32_t state;

      explicit Rng (std::uint32_t seed) : state (seed ? seed : 0x9e3779b9u) {}

      std::uint32_t next_u32 () {
        std::uint32_t z = (state += 0x9e3779b9u);
        z = (z ^ (z >> 16)) * 0x85ebca6bu;
        z = (z ^ (z >> 13)) * 0xc2b2ae35u;
        return z ^ (z >> 16);
      }

      float next () {
        return float (next_u32 () & 0x00ffffffu) / 16777216.0f;
      }

      float range (float lo, float hi) {
        return lerpf (lo, hi, next ());
      }
    };

    struct SpeciesRef {
      float height;
      float crown_radius;
      float trunk_radius;
      float crown_base;
    };

    // Reference proportions in metres for normalizing each species' sculpted
    // archetype into canonical (trunk_radius / crown_radius / height) space.
    SpeciesRef species_ref (std::uint32_t species) {
      switch (species) {
      case 0: // Silver birch
        return { 18.0f, 3.6f, 0.153f, 0.44f };
      case 1: // Norway spruce
        return { 24.0f, 3.1f, 0.187f, 0.11f };
      case 2: // Scots pine
        return { 20.0f, 4.2f, 0.170f, 0.56f };
      case 3: // Mountain / fell birch
        return { 4.8f, 2.5f, 0.068f, 0.16f };
      default: // Standing deadwood snag
        return { 16.0f, 2.2f, 0.125f, 0.30f };
      }
    }

    struct TubeNode {
      Vec3f pos;
      float radius = 0.1f;
      float flutter = 0.0f;
      float ao = 0.85f;
      float flare = 0.0f; // root buttress fluting amplitude
      float jag = 0.0f;   // broken-top jaggedness amplitude
    };

    struct MeshWriter {
      TreeArchetypeLibrary& lib;
      SpeciesRef ref;
      std::vector<Vec3f> raw_pos;

      std::uint16_t emit (Vec3f pos_m,
                          Vec3f normal,
                          float ao,
                          float u,
                          float v,
                          float flutter,
                          float part,
                          bool trunk_radial) {
        const std::size_t idx = lib.vertices.size ();
        if (idx >= 65535u)
          throw std::runtime_error ("NHAL: tree archetype vertices > uint16");
        const float r_div = trunk_radial ? ref.trunk_radius : ref.crown_radius;
        const Vec3f n = norm3 (normal);
        TreeArchetypeVertex vert {};
        vert.pos_scale = { pos_m.x / r_div,
                           pos_m.y / ref.height,
                           pos_m.z / r_div,
                           trunk_radial ? 0.0f : 1.0f };
        vert.norm_ao = { n.x, n.y, n.z, clampf (ao, 0.32f, 1.0f) };
        vert.uv_meta = { u, v, clampf (flutter, 0.0f, 1.0f), part };
        lib.vertices.push_back (vert);
        raw_pos.push_back (pos_m);
        return std::uint16_t (idx);
      }

      // Emits a triangle with guaranteed outward CCW winding relative to
      // `outward` so hardware back-face culling is always safe.
      void
      tri (std::uint16_t a, std::uint16_t b, std::uint16_t c, Vec3f outward) {
        const Vec3f pa = raw_pos[a], pb = raw_pos[b], pc = raw_pos[c];
        const Vec3f g = cross3 (pb - pa, pc - pa);
        if (dot3 (g, g) < 1e-12f)
          return;
        if (dot3 (g, outward) < 0.0f)
          std::swap (b, c);
        lib.indices.push_back (a);
        lib.indices.push_back (b);
        lib.indices.push_back (c);
      }

      void add_tube (const std::vector<TubeNode>& nodes,
                     int sides,
                     float part,
                     bool trunk_radial,
                     bool cap_top,
                     Rng& rng) {
        if (nodes.size () < 2 || sides < 3)
          return;
        const int n = int (nodes.size ());
        std::vector<std::uint16_t> rings;
        rings.reserve (std::size_t (n) * sides);

        // Parallel-transport frame so curved boughs never twist or pinch.
        Vec3f tangent = norm3 (nodes[1].pos - nodes[0].pos);
        Vec3f ref_axis = std::abs (tangent.y) < 0.92f
                           ? Vec3f { 0.0f, 1.0f, 0.0f }
                           : Vec3f { 1.0f, 0.0f, 0.0f };
        Vec3f u_axis = norm3 (cross3 (tangent, ref_axis));
        Vec3f v_axis = norm3 (cross3 (tangent, u_axis));

        const float phase = rng.range (0.0f, two_pi);
        const float buttress_lobes = float (rng.range (3.0f, 5.5f));
        std::vector<float> jag_offsets (sides, 0.0f);
        for (int s = 0; s < sides; ++s)
          jag_offsets[s] = rng.range (-1.0f, 1.0f);

        for (int i = 0; i < n; ++i) {
          if (i > 0) {
            const Vec3f next_t = i + 1 < n
                                   ? norm3 (nodes[i + 1].pos - nodes[i - 1].pos)
                                   : norm3 (nodes[i].pos - nodes[i - 1].pos);
            u_axis = norm3 (u_axis - next_t * dot3 (u_axis, next_t));
            v_axis = norm3 (cross3 (next_t, u_axis));
            tangent = next_t;
          }
          const TubeNode& nd = nodes[i];
          for (int s = 0; s < sides; ++s) {
            const float ang = phase + two_pi * float (s) / float (sides);
            const Vec3f radial =
              u_axis * std::cos (ang) + v_axis * std::sin (ang);
            float r = nd.radius;
            if (nd.flare > 0.0f) {
              const float flute =
                0.5f + 0.5f * std::cos (buttress_lobes * ang + 0.7f);
              r *= 1.0f + nd.flare * flute;
            }
            Vec3f p = nd.pos + radial * r;
            if (nd.jag > 0.0f)
              p = p + tangent * (nd.jag * jag_offsets[s]);
            const float bark_u =
              ang * nd.radius /
              (trunk_radial ? ref.trunk_radius : ref.crown_radius);
            const float bark_v = p.y / ref.height;
            rings.push_back (emit (p,
                                   radial,
                                   nd.ao,
                                   bark_u,
                                   bark_v,
                                   nd.flutter,
                                   part,
                                   trunk_radial));
          }
        }

        for (int i = 0; i + 1 < n; ++i) {
          const Vec3f mid = mix3 (nodes[i].pos, nodes[i + 1].pos, 0.5f);
          for (int s = 0; s < sides; ++s) {
            const int sn = (s + 1) % sides;
            const std::uint16_t a = rings[i * sides + s];
            const std::uint16_t b = rings[i * sides + sn];
            const std::uint16_t c = rings[(i + 1) * sides + s];
            const std::uint16_t d = rings[(i + 1) * sides + sn];
            const Vec3f out1 =
              (raw_pos[a] + raw_pos[b] + raw_pos[c]) * (1.0f / 3.0f) - mid;
            const Vec3f out2 =
              (raw_pos[b] + raw_pos[d] + raw_pos[c]) * (1.0f / 3.0f) - mid;
            tri (a, c, b, out1);
            tri (b, c, d, out2);
          }
        }

        if (cap_top) {
          const TubeNode& last = nodes.back ();
          const Vec3f tip_pos = last.pos + tangent * (last.radius * 0.8f);
          const std::uint16_t tip = emit (tip_pos,
                                          tangent,
                                          last.ao,
                                          0.0f,
                                          tip_pos.y / ref.height,
                                          last.flutter,
                                          part,
                                          trunk_radial);
          const int base = (n - 1) * sides;
          for (int s = 0; s < sides; ++s) {
            const int sn = (s + 1) % sides;
            tri (rings[base + s], tip, rings[base + sn], tangent);
          }
        }
      }

      // Softly ruffled, isotropic geodesic organic foliage cluster (Birch
      // weeping puffs, Scots Pine billowing cushions, Fell Birch airy tufts).
      // Uses a deformed icosahedron (12v/20t) for hero/mid tiers so clusters
      // never show hexagonal saucer caps from below, and a deformed octahedron
      // (6v/8t) for far & near-shadow tier 0.
      void add_organic_puff (Vec3f centre,
                             float rx,
                             float ry,
                             float rz,
                             float droop,
                             Vec3f crown_core,
                             float crown_y0,
                             float crown_y1,
                             float clump_seed,
                             float base_ao,
                             float flutter,
                             int detail) {
        auto frac = [] (float x) { return x - std::floor (x); };
        const float p1 = frac (clump_seed * 3.17f + 0.13f) * two_pi;
        const float p2 = frac (clump_seed * 7.31f + 0.41f) * two_pi;
        const float yaw = frac (clump_seed * 13.73f + 0.29f) * two_pi;
        const float pitch = (frac (clump_seed * 5.91f) - 0.5f) * 0.45f;
        const float cy = std::cos (yaw), sy = std::sin (yaw);
        const float cp = std::cos (pitch), sp = std::sin (pitch);

        auto emit_unit = [&] (Vec3f u) {
          // Rotate unit direction organically per cluster, then apply smooth
          // 3D harmonic lobe bumps so no two puffs read as geometric solids.
          const Vec3f r1 { u.x, u.y * cp - u.z * sp, u.y * sp + u.z * cp };
          const Vec3f d { r1.x * cy - r1.z * sy, r1.y, r1.x * sy + r1.z * cy };
          const float ang = std::atan2 (d.z, d.x);
          const float bump = 1.0f + 0.17f * std::sin (3.0f * ang + p1) +
                             0.12f * std::cos (2.0f * d.y * pi + p2) +
                             0.08f * std::sin (5.0f * ang + d.y * 3.0f + p1);
          // Weeping birch lower skirt droops gently; pine stays billowing.
          const float y_scale = d.y < 0.0f ? ry * (1.0f + droop * 0.35f) : ry;
          const Vec3f off { d.x * rx * bump,
                            d.y * y_scale * bump,
                            d.z * rz * bump };
          const Vec3f p = centre + off;
          const Vec3f local_n = norm3 ({ off.x / std::max (rx, 0.05f),
                                         off.y / std::max (ry, 0.05f),
                                         off.z / std::max (rz, 0.05f) });
          const Vec3f env_n = norm3 (p - crown_core);
          // Blend tree-envelope dome normal with local cluster relief so the
          // crown reads as one sunlit organic volume with soft tufted depth.
          const Vec3f n = norm3 (mix3 (local_n, env_n, 0.56f));
          const float crown_h =
            clampf ((p.y - crown_y0) / std::max (crown_y1 - crown_y0, 0.1f),
                    0.0f,
                    1.0f);
          const float vert_ao = lerpf (0.76f, 1.05f, 0.5f * (d.y + 1.0f));
          const float ao = clampf (base_ao * vert_ao, 0.38f, 1.0f);
          return emit (p, n, ao, clump_seed, crown_h, flutter, 2.0f, false);
        };

        if (detail == 0) {
          static constexpr Vec3f oct_v[6] {
            { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f },
          };
          static constexpr int oct_f[8][3] {
            { 0, 3, 2 }, { 0, 4, 3 }, { 0, 5, 4 }, { 0, 2, 5 },
            { 1, 2, 3 }, { 1, 3, 4 }, { 1, 4, 5 }, { 1, 5, 2 },
          };
          std::uint16_t v[6];
          for (int i = 0; i < 6; ++i)
            v[i] = emit_unit (oct_v[i]);
          for (const auto& f : oct_f) {
            const Vec3f out =
              (raw_pos[v[f[0]]] + raw_pos[v[f[1]]] + raw_pos[v[f[2]]]) *
                (1.0f / 3.0f) -
              centre;
            tri (v[f[0]], v[f[1]], v[f[2]], out);
          }
          return;
        }

        // Unit icosahedron: 12 vertices, 20 isotropic triangles.
        constexpr float phi = 1.61803398875f;
        const float inv = 1.0f / std::sqrt (1.0f + phi * phi);
        const float a = inv, b = phi * inv;
        const Vec3f ico_v[12] {
          { -a, b, 0 }, { a, b, 0 }, { -a, -b, 0 }, { a, -b, 0 },
          { 0, -a, b }, { 0, a, b }, { 0, -a, -b }, { 0, a, -b },
          { b, 0, -a }, { b, 0, a }, { -b, 0, -a }, { -b, 0, a },
        };
        static constexpr int ico_f[20][3] {
          { 0, 11, 5 },  { 0, 5, 1 },  { 0, 1, 7 },  { 0, 7, 10 },
          { 0, 10, 11 }, { 1, 5, 9 },  { 5, 11, 4 }, { 11, 10, 2 },
          { 10, 7, 6 },  { 7, 1, 8 },  { 3, 9, 4 },  { 3, 4, 2 },
          { 3, 2, 6 },   { 3, 6, 8 },  { 3, 8, 9 },  { 4, 9, 5 },
          { 2, 4, 11 },  { 6, 2, 10 }, { 8, 6, 7 },  { 9, 8, 1 },
        };
        std::uint16_t v[12];
        for (int i = 0; i < 12; ++i)
          v[i] = emit_unit (ico_v[i]);
        for (const auto& f : ico_f) {
          const Vec3f out =
            (raw_pos[v[f[0]]] + raw_pos[v[f[1]]] + raw_pos[v[f[2]]]) *
              (1.0f / 3.0f) -
            centre;
          tri (v[f[0]], v[f[1]], v[f[2]], out);
        }
      }

      // 3D cambered, drooping, upturned-tip scalloped Norway spruce bough
      // plume: sunlit ruffled upper ridge + shadowed underside keel.
      void add_spruce_frond (Vec3f attach,
                             float azimuth,
                             float length,
                             float width,
                             float thickness,
                             float droop,
                             float upturn,
                             float crown_y0,
                             float crown_y1,
                             float clump_seed,
                             float base_ao,
                             float flutter,
                             int stations) {
        const Vec3f dir { std::cos (azimuth), 0.0f, std::sin (azimuth) };
        const Vec3f side { -dir.z, 0.0f, dir.x };
        const Vec3f up { 0.0f, 1.0f, 0.0f };

        auto spine = [&] (float t) -> Vec3f {
          const float dip = -droop * std::sin (pi * std::pow (t, 0.82f));
          const float lift = upturn * std::pow (t, 2.3f);
          return attach + dir * (length * t) + up * (dip + lift);
        };

        auto emit_frond_v = [&] (Vec3f p, Vec3f n, float ao_mul, float t) {
          const float crown_h =
            clampf ((p.y - crown_y0) / std::max (crown_y1 - crown_y0, 0.1f),
                    0.0f,
                    1.0f);
          const float ao =
            clampf (lerpf (base_ao * 0.72f, base_ao, t) * ao_mul, 0.35f, 1.0f);
          return emit (p,
                       n,
                       ao,
                       clump_seed,
                       crown_h,
                       flutter * (0.25f + 0.75f * t),
                       2.0f,
                       false);
        };

        const Vec3f root_p = spine (0.0f);
        const std::uint16_t root_v =
          emit_frond_v (root_p, dir * -1.0f, 0.55f, 0.0f);

        std::vector<std::array<std::uint16_t, 4>> rings;
        rings.reserve (stations);
        for (int s = 1; s <= stations; ++s) {
          const float t = float (s) / float (stations + 1);
          const Vec3f c = spine (t);
          // Feathered envelope with scalloped sub-spray undulations along
          // the bough length.
          const float env = std::sin (pi * std::pow (t, 0.68f));
          const float scallop =
            1.0f + 0.18f * std::sin (float (s) * 2.4f + clump_seed * two_pi);
          const float w = width * (0.35f + 0.78f * env) * scallop;
          const float th = thickness * (0.45f + 0.65f * env);
          // Outer wing tips droop slightly for a tented snow-shedding bough.
          const Vec3f p_top = c + up * (th * 0.85f);
          const Vec3f p_r = c + side * w - up * (th * 0.18f);
          const Vec3f p_bot = c - up * (th * 0.65f);
          const Vec3f p_l = c - side * w - up * (th * 0.18f);

          const Vec3f n_top = norm3 (up * 0.85f + dir * 0.45f);
          const Vec3f n_r = norm3 (side * 0.72f + up * 0.48f + dir * 0.35f);
          const Vec3f n_bot = norm3 (up * -0.82f + dir * 0.35f);
          const Vec3f n_l = norm3 (side * -0.72f + up * 0.48f + dir * 0.35f);

          rings.push_back ({ emit_frond_v (p_top, n_top, 1.04f, t),
                             emit_frond_v (p_r, n_r, 0.94f, t),
                             emit_frond_v (p_bot, n_bot, 0.60f, t),
                             emit_frond_v (p_l, n_l, 0.94f, t) });
        }

        const Vec3f tip_p = spine (1.0f);
        const Vec3f tip_n = norm3 (dir * 0.75f + up * 0.66f);
        const std::uint16_t tip_v = emit_frond_v (tip_p, tip_n, 1.06f, 1.0f);

        // Root cap to first ring.
        const Vec3f c0 = spine (0.5f / float (stations + 1));
        for (int k = 0; k < 4; ++k) {
          const int kn = (k + 1) % 4;
          const Vec3f out =
            (raw_pos[root_v] + raw_pos[rings[0][k]] + raw_pos[rings[0][kn]]) *
              (1.0f / 3.0f) -
            c0;
          tri (root_v, rings[0][kn], rings[0][k], out);
        }

        // Intermediate segments along bough.
        for (int s = 0; s + 1 < stations; ++s) {
          const Vec3f cm = spine ((float (s) + 1.5f) / float (stations + 1));
          for (int k = 0; k < 4; ++k) {
            const int kn = (k + 1) % 4;
            const std::uint16_t a = rings[s][k];
            const std::uint16_t b = rings[s][kn];
            const std::uint16_t c = rings[s + 1][k];
            const std::uint16_t d = rings[s + 1][kn];
            const Vec3f o1 =
              (raw_pos[a] + raw_pos[b] + raw_pos[c]) * (1.0f / 3.0f) - cm;
            const Vec3f o2 =
              (raw_pos[b] + raw_pos[d] + raw_pos[c]) * (1.0f / 3.0f) - cm;
            tri (a, b, c, o1);
            tri (b, d, c, o2);
          }
        }

        // Last ring to feathered needle tip.
        const auto& last = rings.back ();
        const Vec3f ce =
          spine ((float (stations) + 0.5f) / float (stations + 1));
        for (int k = 0; k < 4; ++k) {
          const int kn = (k + 1) % 4;
          const Vec3f out =
            (raw_pos[last[k]] + raw_pos[last[kn]] + raw_pos[tip_v]) *
              (1.0f / 3.0f) -
            ce;
          tri (last[k], last[kn], tip_v, out);
        }
      }
    };

    // -- Species 0: Nordic Silver Birch (Betula pendula) --------------------
    // Slender gently S-curved white trunk with dark flared root buttresses,
    // ascending gracefully forked limbs, and airy multi-cluster weeping leaf
    // crowns with real sky gaps between sub-crowns.
    void build_birch (MeshWriter& w, std::uint32_t variant, int tier) {
      Rng rng (0x1001u + variant * 7919u);
      const float H = w.ref.height;
      const float R = w.ref.crown_radius;
      const float r0 = w.ref.trunk_radius;
      const float crown_y0 = H * rng.range (0.38f, 0.46f);
      const float crown_y1 = H * 1.0f;

      const float curve_dir = rng.range (0.0f, two_pi);
      const float curve_amp = r0 * rng.range (0.45f, 1.15f);
      auto trunk_at = [&] (float t) -> Vec3f {
        const float s = std::sin (pi * t * 1.15f) * curve_amp * t;
        return { std::cos (curve_dir) * s,
                 lerpf (-0.025f * H, 0.84f * H, t),
                 std::sin (curve_dir) * s };
      };

      const int trunk_sides = tier == 2 ? 7 : (tier == 1 ? 5 : 3);
      const int trunk_rings = tier == 2 ? 6 : (tier == 1 ? 4 : 3);
      std::vector<TubeNode> trunk;
      for (int i = 0; i < trunk_rings; ++i) {
        const float t = float (i) / float (trunk_rings - 1);
        const float taper =
          i == 0 ? 1.85f : lerpf (1.06f, 0.16f, std::pow (t, 0.85f));
        trunk.push_back ({ trunk_at (t),
                           r0 * taper,
                           t * t * 0.25f,
                           lerpf (0.68f, 0.88f, t),
                           i == 0 ? 0.35f : 0.0f,
                           0.0f });
      }
      Rng tube_rng (0x81001u + variant * 3571u);
      w.add_tube (trunk, trunk_sides, 0.0f, true, tier < 2, tube_rng);

      // Primary ascending curved limbs and sub-crown centers.
      struct SubCrown {
        Vec3f tip;
        Vec3f attach;
        float radius;
        float rise;
        float az;
        float seed;
      };
      const int limb_count = 6;
      std::vector<SubCrown> limbs;
      const float base_az = rng.range (0.0f, two_pi);
      for (int i = 0; i < limb_count; ++i) {
        const float frac = float (i) / float (limb_count - 1);
        const float az =
          base_az + golden_angle * float (i) + rng.range (-0.28f, 0.28f);
        const float attach_t = lerpf (0.48f, 0.84f, frac);
        const Vec3f attach = trunk_at (attach_t);
        const float env =
          std::pow (std::sin (pi * (0.14f + 0.78f * frac)), 0.7f);
        const float reach = R * env * rng.range (0.42f, 0.82f);
        const float rise_m = H * lerpf (0.12f, 0.24f, 1.0f - frac * 0.4f);
        const Vec3f tip { attach.x + std::cos (az) * reach,
                          std::min (attach.y + rise_m, H * 0.94f),
                          attach.z + std::sin (az) * reach };
        limbs.push_back (
          { tip,
            attach,
            R * lerpf (0.36f, 0.26f, frac) * rng.range (0.88f, 1.16f),
            frac,
            az,
            rng.next () });

        if (tier >= 1) {
          const int b_sides = tier == 2 ? 4 : 3;
          const Vec3f mid =
            mix3 (attach, tip, 0.52f) + Vec3f { std::cos (az) * reach * 0.08f,
                                                rise_m * 0.12f,
                                                std::sin (az) * reach * 0.08f };
          const Vec3f end = mix3 (attach, tip, 0.88f);
          std::vector<TubeNode> bnodes {
            { { attach.x * 0.25f, attach.y, attach.z * 0.25f },
              r0 * lerpf (0.42f, 0.26f, frac),
              0.12f,
              0.68f },
            { mid, r0 * lerpf (0.24f, 0.14f, frac), 0.24f, 0.76f },
            { end, r0 * 0.08f, 0.36f, 0.82f },
          };
          w.add_tube (bnodes, b_sides, 1.0f, false, false, tube_rng);
        }
      }

      // Build ordered foliage clusters: first 7 define the whole birch crown
      // silhouette; remaining 17 enrich LOD1 and LOD0 with airy weeping puffs.
      struct PuffSpec {
        Vec3f pos;
        float rx, ry, rz;
        float seed;
        float ao;
        float flutter;
      };
      std::vector<PuffSpec> puffs;
      const Vec3f crown_core { 0.0f, lerpf (crown_y0, crown_y1, 0.48f), 0.0f };

      // Apex tuft + 6 main limb sub-crowns = 7 core silhouette puffs.
      puffs.push_back ({ { 0.0f, H * 0.90f, 0.0f },
                         R * 0.36f,
                         H * 0.11f,
                         R * 0.34f,
                         rng.next (),
                         0.98f,
                         0.92f });
      for (const SubCrown& sc : limbs) {
        const float dist_frac =
          clampf (len3 (sc.tip - crown_core) / std::max (R, 0.1f), 0.0f, 1.0f);
        puffs.push_back (
          { sc.tip,
            sc.radius,
            sc.radius * rng.range (0.95f, 1.25f),
            sc.radius * rng.range (0.85f, 1.12f),
            sc.seed,
            lerpf (0.58f, 0.96f, 0.55f * dist_frac + 0.45f * sc.rise),
            lerpf (0.45f, 0.95f, sc.rise) });
      }
      // Secondary weeping satellite puffs around each limb for LOD1 & LOD0.
      for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < limb_count; ++i) {
          const SubCrown& sc = limbs[i];
          const float sub_az =
            sc.az + float (round) * 1.32f + rng.range (-0.28f, 0.28f);
          const float out = sc.radius * rng.range (0.42f, 1.02f);
          const float dy = sc.radius * (round >= 3 ? rng.range (-0.78f, -0.10f)
                                                   : rng.range (-0.24f, 0.54f));
          const Vec3f p { sc.tip.x + std::cos (sub_az) * out,
                          clampf (sc.tip.y + dy, crown_y0 * 0.92f, H * 0.98f),
                          sc.tip.z + std::sin (sub_az) * out };
          const float rad = sc.radius * (round >= 3 ? rng.range (0.38f, 0.58f)
                                                    : rng.range (0.46f, 0.70f));
          const float dist_frac = clampf (
            len3 (p - crown_core) / std::max (R * 1.15f, 0.1f), 0.0f, 1.0f);
          const float h_frac =
            clampf ((p.y - crown_y0) / std::max (crown_y1 - crown_y0, 0.1f),
                    0.0f,
                    1.0f);
          puffs.push_back (
            { p,
              rad,
              rad * rng.range (0.88f, 1.24f),
              rad * rng.range (0.82f, 1.14f),
              clampf (sc.seed + rng.range (-0.18f, 0.18f), 0.0f, 1.0f),
              lerpf (0.44f, 0.98f, 0.50f * dist_frac + 0.50f * h_frac),
              lerpf (0.55f, 1.0f, dist_frac) });
        }
      }

      const int keep = tier == 2 ? int (puffs.size ()) : (tier == 1 ? 12 : 5);
      // Conservation of foliage coverage across LODs: fewer puffs widen
      // gently to hold projected area steady at transitions.
      const float area_boost = tier == 2 ? 0.92f : (tier == 1 ? 1.22f : 1.54f);
      for (int i = 0; i < keep; ++i) {
        const PuffSpec& ps = puffs[i];
        w.add_organic_puff (ps.pos,
                            ps.rx * area_boost,
                            ps.ry * (1.0f + (area_boost - 1.0f) * 0.85f),
                            ps.rz * area_boost,
                            0.32f, // weeping silver birch skirt
                            crown_core,
                            crown_y0,
                            crown_y1,
                            ps.seed,
                            ps.ao,
                            ps.flutter,
                            tier);
      }
    }

    // -- Species 1: Norway Spruce (Picea abies) -----------------------------
    // Straight tapering mossy-base trunk with tiered spiral whorls of
    // individual 3D drooping, upturned-tip feathered spruce bough fronds
    // leaving genuine sunlit gaps between whorls (no solid cones!).
    void build_spruce (MeshWriter& w, std::uint32_t variant, int tier) {
      Rng rng (0x2002u + variant * 6133u);
      Rng tube_rng (0x82002u + variant * 3571u);
      const float H = w.ref.height;
      const float R = w.ref.crown_radius;
      const float r0 = w.ref.trunk_radius;
      const float crown_y0 = H * rng.range (0.09f, 0.14f);
      const float crown_y1 = H * 1.0f;

      const int trunk_sides = tier == 2 ? 6 : (tier == 1 ? 5 : 3);
      const int trunk_rings = tier == 2 ? 5 : (tier == 1 ? 4 : 3);
      std::vector<TubeNode> trunk;
      for (int i = 0; i < trunk_rings; ++i) {
        const float t = float (i) / float (trunk_rings - 1);
        const float taper =
          i == 0 ? 1.95f : lerpf (1.05f, 0.09f, std::pow (t, 0.9f));
        trunk.push_back ({ { 0.0f, lerpf (-0.025f * H, 0.94f * H, t), 0.0f },
                           r0 * taper,
                           t * t * 0.18f,
                           lerpf (0.58f, 0.82f, t),
                           i == 0 ? 0.45f : 0.0f,
                           0.0f });
      }
      w.add_tube (trunk, trunk_sides, 0.0f, true, true, tube_rng);

      struct BoughSpec {
        Vec3f attach;
        float az;
        float length;
        float width;
        float thickness;
        float droop;
        float upturn;
        float seed;
        float ao;
        float flutter;
      };
      std::vector<BoughSpec> boughs;

      // 8 tiers bottom-to-top, scheduled by rank round so the first 12–14
      // boughs sketch the entire spruce silhouette evenly across every whorl!
      const int whorls = 8;
      const float base_az = rng.range (0.0f, two_pi);
      for (int spoke_round = 0; spoke_round < 5; ++spoke_round) {
        for (int wh = 0; wh < whorls; ++wh) {
          const float wf = float (wh) / float (whorls - 1);
          const int max_spokes = wh >= 6 ? 3 : (wh >= 4 ? 4 : 5);
          if (spoke_round >= max_spokes)
            continue;
          const float y = lerpf (crown_y0, H * 0.89f, std::pow (wf, 0.94f)) +
                          rng.range (-0.018f * H, 0.018f * H);
          // Opposite-azimuth placement per round keeps every LOD symmetric.
          const float az = base_az + float (wh) * 1.18f +
                           two_pi * (float (spoke_round) / float (max_spokes)) +
                           rng.range (-0.22f, 0.22f);
          // Classic concave-flared Norway spruce spire envelope.
          const float spire = std::pow (1.0f - wf * 0.82f, 1.15f) *
                              (wh == 0 ? 0.86f : (wh == 1 ? 1.04f : 1.0f));
          const float length = R * spire * rng.range (0.86f, 1.14f);
          const float width =
            length * lerpf (0.34f, 0.44f, wf) * rng.range (0.88f, 1.15f);
          const float thickness =
            lerpf (0.58f, 0.28f, wf) * (H / 24.0f) * rng.range (0.85f, 1.15f);
          const float droop = length * lerpf (0.36f, 0.08f, wf);
          const float upturn = length * lerpf (0.20f, 0.14f, wf);
          const float ao = lerpf (0.64f, 0.98f, wf);
          boughs.push_back ({ { 0.0f, y, 0.0f },
                              az,
                              length,
                              width,
                              thickness,
                              droop,
                              upturn,
                              rng.next (),
                              ao,
                              lerpf (0.30f, 0.85f, wf) });
        }
      }

      const int keep = tier == 2 ? int (boughs.size ()) : (tier == 1 ? 20 : 9);
      const int stations = tier == 2 ? 3 : (tier == 1 ? 2 : 1);
      // Widen surviving boughs gently at coarser LODs to conserve crown mass.
      const float width_boost = tier == 2 ? 1.0f : (tier == 1 ? 1.26f : 1.72f);
      const float thick_boost = tier == 2 ? 1.0f : (tier == 1 ? 1.20f : 1.48f);
      for (int i = 0; i < keep; ++i) {
        const BoughSpec& b = boughs[i];
        w.add_spruce_frond (b.attach,
                            b.az,
                            b.length,
                            b.width * width_boost,
                            b.thickness * thick_boost,
                            b.droop,
                            b.upturn,
                            crown_y0,
                            crown_y1,
                            b.seed,
                            b.ao,
                            b.flutter,
                            stations);
      }

      // Narrow feathery leader spire at the very top of the spruce.
      const Vec3f spire_core { 0.0f, H * 0.86f, 0.0f };
      w.add_organic_puff ({ 0.0f, H * 0.93f, 0.0f },
                          R * 0.22f * (tier == 0 ? 1.25f : 1.0f),
                          H * 0.09f,
                          R * 0.22f * (tier == 0 ? 1.25f : 1.0f),
                          0.12f,
                          spire_core,
                          crown_y0,
                          crown_y1,
                          rng.next (),
                          0.98f,
                          0.88f,
                          tier == 2 ? 1 : 0);
    }

    // -- Species 2: Scots Pine (Pinus sylvestris) ---------------------------
    // Tall subtly sinuous clear trunk (grey lower bark → warm fox-orange
    // upper bark), gnarled spreading upper limbs, and broad flat-topped
    // asymmetric cushions of blue-green pine needles.
    void build_pine (MeshWriter& w, std::uint32_t variant, int tier) {
      Rng rng (0x3003u + variant * 8291u);
      Rng tube_rng (0x83003u + variant * 3571u);
      const float H = w.ref.height;
      const float R = w.ref.crown_radius;
      const float r0 = w.ref.trunk_radius;
      const float crown_y0 = H * rng.range (0.54f, 0.62f);
      const float crown_y1 = H * 1.0f;

      const float curve_dir = rng.range (0.0f, two_pi);
      const float curve_amp = r0 * rng.range (0.65f, 1.50f);
      auto trunk_at = [&] (float t) -> Vec3f {
        const float bend = std::sin (pi * t * 0.95f) * curve_amp * t;
        return { std::cos (curve_dir) * bend,
                 lerpf (-0.025f * H, 0.82f * H, t),
                 std::sin (curve_dir) * bend };
      };

      const int trunk_sides = tier == 2 ? 7 : (tier == 1 ? 5 : 3);
      const int trunk_rings = tier == 2 ? 6 : (tier == 1 ? 4 : 3);
      std::vector<TubeNode> trunk;
      for (int i = 0; i < trunk_rings; ++i) {
        const float t = float (i) / float (trunk_rings - 1);
        const float taper =
          i == 0 ? 1.80f : lerpf (1.06f, 0.30f, std::pow (t, 0.82f));
        trunk.push_back ({ trunk_at (t),
                           r0 * taper,
                           t * t * 0.24f,
                           lerpf (0.72f, 0.90f, t),
                           i == 0 ? 0.32f : 0.0f,
                           0.0f });
      }
      w.add_tube (trunk, trunk_sides, 0.0f, true, true, tube_rng);

      struct PinePad {
        Vec3f pos;
        float rx, ry, rz;
        float seed;
        float ao;
        float flutter;
      };
      std::vector<PinePad> pads;
      const Vec3f crown_core { 0.0f, lerpf (crown_y0, crown_y1, 0.42f), 0.0f };
      const float base_az = rng.range (0.0f, two_pi);

      // Broad domed crown-top cushion.
      pads.push_back ({ { rng.range (-0.12f, 0.12f) * R,
                          H * 0.91f,
                          rng.range (-0.12f, 0.12f) * R },
                        R * 0.46f,
                        R * 0.27f,
                        R * 0.42f,
                        rng.next (),
                        0.98f,
                        0.82f });

      // 5 gnarled spreading limbs with flat pillowy umbrella pads.
      const int limb_count = 5;
      struct LimbInfo {
        Vec3f tip;
        float az;
        float rad;
        float seed;
      };
      std::vector<LimbInfo> limbs;
      for (int i = 0; i < limb_count; ++i) {
        const float frac = float (i) / float (limb_count - 1);
        const float az =
          base_az + golden_angle * float (i) + rng.range (-0.30f, 0.30f);
        const float attach_t = lerpf (0.68f, 0.96f, frac);
        const Vec3f attach = trunk_at (attach_t);
        const float reach = R * rng.range (0.58f, 0.92f);
        const float lift = H * rng.range (0.06f, 0.16f);
        const Vec3f elbow { attach.x + std::cos (az) * reach * 0.52f,
                            attach.y + lift * 0.35f -
                              H * rng.range (0.0f, 0.025f),
                            attach.z + std::sin (az) * reach * 0.52f };
        const Vec3f tip { attach.x + std::cos (az) * reach,
                          std::min (attach.y + lift, H * 0.93f),
                          attach.z + std::sin (az) * reach };
        const float pad_r = R * rng.range (0.34f, 0.48f);
        const float lseed = rng.next ();
        limbs.push_back ({ tip, az, pad_r, lseed });

        if (tier >= 1) {
          const int b_sides = tier == 2 ? 4 : 3;
          std::vector<TubeNode> bnodes {
            { { attach.x * 0.2f, attach.y, attach.z * 0.2f },
              r0 * lerpf (0.52f, 0.34f, frac),
              0.16f,
              0.74f },
            { elbow, r0 * lerpf (0.34f, 0.20f, frac), 0.26f, 0.80f },
            { mix3 (elbow, tip, 0.85f), r0 * 0.12f, 0.36f, 0.84f },
          };
          w.add_tube (bnodes, b_sides, 1.0f, false, false, tube_rng);
        }

        pads.push_back ({ tip + Vec3f { 0.0f, pad_r * 0.22f, 0.0f },
                          pad_r,
                          pad_r * rng.range (0.50f, 0.64f),
                          pad_r * rng.range (0.86f, 1.12f),
                          lseed,
                          lerpf (0.68f, 0.96f, frac),
                          lerpf (0.55f, 0.90f, frac) });
      }

      // Secondary billowing cloud cushions perched around and atop each bough
      // so Scots pine crowns never read as flat discs.
      for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < limb_count; ++i) {
          const LimbInfo& li = limbs[i];
          const float sub_az =
            li.az + float (round) * 1.28f + rng.range (-0.28f, 0.28f);
          const float out = li.rad * (round >= 3 ? rng.range (0.15f, 0.55f)
                                                 : rng.range (0.36f, 0.84f));
          const float dy = li.rad * (round >= 3 ? rng.range (0.18f, 0.48f)
                                                : rng.range (-0.12f, 0.36f));
          const Vec3f p { li.tip.x + std::cos (sub_az) * out,
                          clampf (li.tip.y + dy, crown_y0, H * 0.99f),
                          li.tip.z + std::sin (sub_az) * out };
          const float r = li.rad * (round >= 3 ? rng.range (0.40f, 0.62f)
                                               : rng.range (0.48f, 0.74f));
          const float h_frac =
            clampf ((p.y - crown_y0) / std::max (crown_y1 - crown_y0, 0.1f),
                    0.0f,
                    1.0f);
          pads.push_back (
            { p,
              r,
              r * rng.range (0.54f, 0.70f),
              r * rng.range (0.84f, 1.12f),
              clampf (li.seed + rng.range (-0.15f, 0.15f), 0.0f, 1.0f),
              lerpf (0.50f, 0.98f, h_frac),
              lerpf (0.50f, 0.92f, h_frac) });
        }
      }

      const int keep = tier == 2 ? int (pads.size ()) : (tier == 1 ? 11 : 5);
      const float area_boost = tier == 2 ? 0.92f : (tier == 1 ? 1.22f : 1.54f);
      for (int i = 0; i < keep; ++i) {
        const PinePad& pd = pads[i];
        w.add_organic_puff (pd.pos,
                            pd.rx * area_boost,
                            pd.ry * (1.0f + (area_boost - 1.0f) * 0.80f),
                            pd.rz * area_boost,
                            0.08f,
                            crown_core,
                            crown_y0,
                            crown_y1,
                            pd.seed,
                            pd.ao,
                            pd.flutter,
                            tier);
      }
    }

    // -- Species 3: Mountain / Fell Birch (Betula pubescens czerepanovii) ---
    // Low, wind-sculpted treeline birch: 4–5 crooked white-barked stems
    // springing from one shared rootstock, sweeping outward under winter
    // snowpack, bending up at gnarled knees, with airy tufted crowns.
    void build_fell_birch (MeshWriter& w, std::uint32_t variant, int tier) {
      Rng rng (0x4004u + variant * 5381u);
      Rng tube_rng (0x84004u + variant * 3571u);
      const float H = w.ref.height;
      const float R = w.ref.crown_radius;
      const float r0 = w.ref.trunk_radius;
      const float crown_y0 = H * 0.24f;
      const float crown_y1 = H * 1.0f;

      const int stem_count = 5;
      const int draw_stems = tier == 2 ? 5 : (tier == 1 ? 4 : 3);
      const float base_az = rng.range (0.0f, two_pi);

      struct TuftSpec {
        Vec3f pos;
        float rx, ry, rz;
        float seed;
        float ao;
        float flutter;
      };
      std::vector<TuftSpec> tufts;
      const Vec3f crown_core { 0.0f, H * 0.52f, 0.0f };

      struct StemInfo {
        Vec3f knee, top, twig_tip;
        float az;
        float seed;
      };
      std::vector<StemInfo> stems;

      for (int i = 0; i < stem_count; ++i) {
        const float az =
          base_az + golden_angle * float (i) + rng.range (-0.30f, 0.30f);
        const float reach = R * rng.range (0.56f, 0.96f);
        const float top_y = H * rng.range (0.64f, 0.96f);
        const Vec3f p0 { std::cos (az) * r0 * 0.8f,
                         -0.03f * H,
                         std::sin (az) * r0 * 0.8f };
        const Vec3f p1 { std::cos (az) * reach * 0.34f,
                         H * rng.range (0.08f, 0.16f),
                         std::sin (az) * reach * 0.34f };
        const float kink_az = az + rng.range (-0.42f, 0.42f);
        const Vec3f knee { std::cos (az) * reach * 0.62f,
                           H * rng.range (0.20f, 0.34f),
                           std::sin (az) * reach * 0.62f };
        const Vec3f p3 { knee.x + std::cos (kink_az) * reach * 0.26f,
                         lerpf (knee.y, top_y, 0.56f),
                         knee.z + std::sin (kink_az) * reach * 0.26f };
        const Vec3f top { knee.x + std::cos (kink_az) * reach * 0.42f,
                          top_y,
                          knee.z + std::sin (kink_az) * reach * 0.42f };

        const float tw_az = kink_az + (rng.next () > 0.5f ? 1.0f : -1.0f) *
                                        rng.range (0.5f, 0.9f);
        const Vec3f tw_start = mix3 (knee, p3, 0.55f);
        const Vec3f tw_tip {
          tw_start.x + std::cos (tw_az) * R * rng.range (0.28f, 0.46f),
          clampf (
            tw_start.y + H * rng.range (0.18f, 0.34f), H * 0.38f, H * 0.92f),
          tw_start.z + std::sin (tw_az) * R * rng.range (0.28f, 0.46f)
        };
        const float sseed = rng.next ();
        stems.push_back ({ knee, top, tw_tip, kink_az, sseed });

        if (i < draw_stems) {
          const int s_sides = tier == 2 ? 5 : (tier == 1 ? 4 : 3);
          const float girth = r0 * rng.range (0.85f, 1.18f);
          std::vector<TubeNode> snodes;
          snodes.push_back ({ p0, girth * 1.45f, 0.0f, 0.66f, 0.25f, 0.0f });
          if (tier == 2)
            snodes.push_back ({ p1, girth * 0.96f, 0.05f, 0.72f });
          snodes.push_back ({ knee, girth * 0.78f, 0.14f, 0.76f });
          if (tier >= 1)
            snodes.push_back ({ p3, girth * 0.46f, 0.28f, 0.82f });
          snodes.push_back ({ top, girth * 0.20f, 0.42f, 0.86f });
          // Fell birch stems sweep outward across crown_radius!
          w.add_tube (snodes, s_sides, 0.0f, false, false, tube_rng);

          if (tier == 2) {
            std::vector<TubeNode> tnodes {
              { tw_start, girth * 0.38f, 0.20f, 0.78f },
              { mix3 (tw_start, tw_tip, 0.55f) +
                  Vec3f { 0.0f, H * 0.04f, 0.0f },
                girth * 0.22f,
                0.34f,
                0.82f },
              { tw_tip, girth * 0.10f, 0.46f, 0.86f },
            };
            w.add_tube (tnodes, 3, 1.0f, false, false, tube_rng);
          }
        }

        // Core tuft at stem top (first 5 define the wide silhouette).
        const float tr = R * rng.range (0.26f, 0.35f);
        tufts.push_back ({ top,
                           tr,
                           tr * rng.range (0.72f, 0.95f),
                           tr * rng.range (0.85f, 1.12f),
                           sseed,
                           lerpf (0.68f, 0.98f, top.y / H),
                           0.80f });
      }

      // Secondary tufts on forked twigs and outer shoots.
      for (const StemInfo& st : stems) {
        const float tr = R * rng.range (0.22f, 0.30f);
        tufts.push_back (
          { st.twig_tip,
            tr,
            tr * rng.range (0.72f, 0.95f),
            tr * rng.range (0.85f, 1.12f),
            clampf (st.seed + rng.range (-0.15f, 0.15f), 0.0f, 1.0f),
            lerpf (0.62f, 0.96f, st.twig_tip.y / H),
            0.85f });
      }
      for (int round = 0; round < 2; ++round) {
        for (const StemInfo& st : stems) {
          const float sub_az =
            st.az + (round == 0 ? 0.7f : -0.7f) + rng.range (-0.3f, 0.3f);
          const float out = R * rng.range (0.14f, 0.28f);
          const Vec3f base_p = round == 0 ? st.top : st.twig_tip;
          const Vec3f p { base_p.x + std::cos (sub_az) * out,
                          clampf (base_p.y + H * rng.range (-0.10f, 0.12f),
                                  crown_y0,
                                  H * 0.98f),
                          base_p.z + std::sin (sub_az) * out };
          const float tr = R * rng.range (0.18f, 0.25f);
          tufts.push_back (
            { p,
              tr,
              tr * rng.range (0.70f, 0.92f),
              tr * rng.range (0.84f, 1.12f),
              clampf (st.seed + rng.range (-0.2f, 0.2f), 0.0f, 1.0f),
              lerpf (0.55f, 0.98f, p.y / H),
              0.92f });
        }
      }

      const int keep = tier == 2 ? int (tufts.size ()) : (tier == 1 ? 10 : 5);
      const float area_boost = tier == 2 ? 1.0f : (tier == 1 ? 1.26f : 1.56f);
      for (int i = 0; i < keep; ++i) {
        const TuftSpec& tf = tufts[i];
        w.add_organic_puff (tf.pos,
                            tf.rx * area_boost,
                            tf.ry * (1.0f + (area_boost - 1.0f) * 0.85f),
                            tf.rz * area_boost,
                            0.22f,
                            crown_core,
                            crown_y0,
                            crown_y1,
                            tf.seed,
                            tf.ao,
                            tf.flutter,
                            tier);
      }
    }

    // -- Species 4: Standing Deadwood Snag ----------------------------------
    // Weathered silver-grey, charred-foot fire-killed trunk with jagged
    // splintered crown and gnarled broken skeletal bough stubs (no foliage).
    void build_snag (MeshWriter& w, std::uint32_t variant, int tier) {
      Rng rng (0x5005u + variant * 4421u);
      const float H = w.ref.height;
      const float R = w.ref.crown_radius;
      const float r0 = w.ref.trunk_radius;

      const float curve_dir = rng.range (0.0f, two_pi);
      const float curve_amp = r0 * rng.range (0.55f, 1.35f);
      auto trunk_at = [&] (float t) -> Vec3f {
        const float bend = std::sin (pi * t * 0.85f) * curve_amp * t;
        return { std::cos (curve_dir) * bend,
                 lerpf (-0.025f * H, 0.96f * H, t),
                 std::sin (curve_dir) * bend };
      };

      const int trunk_sides = tier == 2 ? 7 : (tier == 1 ? 5 : 3);
      const int trunk_rings = tier == 2 ? 6 : (tier == 1 ? 4 : 3);
      std::vector<TubeNode> trunk;
      for (int i = 0; i < trunk_rings; ++i) {
        const float t = float (i) / float (trunk_rings - 1);
        const float taper =
          i == 0 ? 1.85f : lerpf (1.04f, 0.36f, std::pow (t, 0.8f));
        trunk.push_back ({ trunk_at (t),
                           r0 * taper,
                           t * t * 0.14f,
                           lerpf (0.64f, 0.90f, t),
                           i == 0 ? 0.40f : 0.0f,
                           i + 1 == trunk_rings ? H * 0.045f : 0.0f });
      }
      w.add_tube (trunk, trunk_sides, 0.0f, true, true, rng);

      const int stub_count = tier == 2 ? 7 : (tier == 1 ? 5 : 3);
      const float base_az = rng.range (0.0f, two_pi);
      for (int i = 0; i < stub_count; ++i) {
        const float frac = float (i) / float (std::max (stub_count - 1, 1));
        const float az =
          base_az + golden_angle * float (i) + rng.range (-0.35f, 0.35f);
        const float attach_t = lerpf (0.36f, 0.88f, frac);
        const Vec3f attach = trunk_at (attach_t);
        const float reach = R * rng.range (0.36f, 0.78f);
        const float lift = H * rng.range (-0.03f, 0.09f);
        const Vec3f mid { attach.x + std::cos (az) * reach * 0.55f,
                          attach.y + lift * 0.45f,
                          attach.z + std::sin (az) * reach * 0.55f };
        const Vec3f tip { attach.x + std::cos (az) * reach,
                          attach.y + lift,
                          attach.z + std::sin (az) * reach };
        const int b_sides = tier == 2 ? 4 : 3;
        std::vector<TubeNode> bnodes {
          { { attach.x * 0.25f, attach.y, attach.z * 0.25f },
            r0 * rng.range (0.30f, 0.44f),
            0.08f,
            0.72f },
          { mid, r0 * rng.range (0.18f, 0.26f), 0.14f, 0.80f },
          { tip, r0 * 0.09f, 0.20f, 0.86f, 0.0f, r0 * 0.5f },
        };
        w.add_tube (bnodes, b_sides, 1.0f, false, true, rng);
      }
    }
  }

  TreeArchetypeLibrary build_tree_archetypes (std::uint32_t species_count,
                                              std::uint32_t variants,
                                              std::uint32_t tiers) {
    TreeArchetypeLibrary lib;
    lib.vertices.reserve (10000);
    lib.indices.reserve (52000);
    lib.slices.resize (std::size_t (species_count) * variants * tiers);

    for (std::uint32_t sp = 0; sp < species_count; ++sp) {
      const SpeciesRef ref = species_ref (sp);
      for (std::uint32_t v = 0; v < variants; ++v) {
        for (std::uint32_t t = 0; t < tiers; ++t) {
          const std::size_t cls = (std::size_t (sp) * variants + v) * tiers + t;
          const std::uint32_t first = std::uint32_t (lib.indices.size ());
          MeshWriter writer { lib, ref, {} };
          // Keep raw_pos aligned with lib.vertices indices.
          writer.raw_pos.resize (lib.vertices.size ());
          const int detail = tiers <= 1 ? 2 : int (t * 2u / (tiers - 1u));
          switch (sp) {
          case 0:
            build_birch (writer, v, detail);
            break;
          case 1:
            build_spruce (writer, v, detail);
            break;
          case 2:
            build_pine (writer, v, detail);
            break;
          case 3:
            build_fell_birch (writer, v, detail);
            break;
          default:
            build_snag (writer, v, detail);
            break;
          }
          lib.slices[cls] = { first,
                              std::uint32_t (lib.indices.size ()) - first };
        }
      }
    }
    return lib;
  }
}
