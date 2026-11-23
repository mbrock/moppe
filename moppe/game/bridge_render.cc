#include <moppe/game/bridge_render.hh>

#include <moppe/gfx/mat4.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace moppe::game {
  namespace {
    // Weathered timber, tarred stringers, and the abutments' granite.
    constexpr DisplayColor plank { 0.47f, 0.40f, 0.32f };
    constexpr DisplayColor tarred { 0.20f, 0.17f, 0.14f };
    constexpr DisplayColor rail { 0.52f, 0.46f, 0.38f };
    constexpr DisplayColor stone { 0.50f, 0.49f, 0.46f };

    // A hash of `k` spread evenly over 0..1.
    proportion_t jitter (std::uint32_t k) {
      k ^= k >> 16;
      k *= 0x7feb352dU;
      k ^= k >> 15;
      k *= 0x846ca68bU;
      k ^= k >> 16;
      return static_cast<float> (k & 0xffffU) / 65535.0f * proportion[one];
    }

    // A shade of a colour: `base` plus `swing` of a jitter.
    scale_factor_t shade_of (float base, float swing, proportion_t jitter) {
      return (base + swing * jitter.numerical_value_in (one)) *
             scale_factor[one];
    }

    // `at` brought into the world's one copy, [0, period): the floor
    // wrap these meshes were always placed by (unlike
    // terrain::wrap_coordinate, it lets the period itself through).
    plan_position_t into_period (const plan_position_t& at,
                                 const plan_extent_t& period) {
      const Vec2& p = plan_value (at);
      const Vec2& n = plan_value (period);
      return plan_position (p.x () - n.x () * std::floor (p.x () / n.x ()),
                            p.z () - n.z () * std::floor (p.z () / n.z ()));
    }

    // The point `t` of the way from `a` to `b`.
    displacement_t between (const displacement_t& a,
                            const displacement_t& b,
                            blend_weight_t t) {
      return a + (b - a) * t.numerical_value_in (one);
    }

    // A box about `centre` with the given unit axes and half sizes.
    void box (render::DrawList& dl,
              const displacement_t& centre,
              const direction_t& u,
              const direction_t& v,
              const direction_t& w,
              meters_t hu,
              meters_t hv,
              meters_t hw,
              DisplayColor colour,
              scale_factor_t shade = 1.0f * scale_factor[one]) {
      dl.color (scale_display (colour, shade));
      // Each face is wound to face outward whichever hand the axes are:
      // the way across a deck is given to the left of the way along, a
      // beam's to its right.
      const auto face =
        [&] (const displacement_t& n, displacement_t a, displacement_t b) {
          if (dot (cross (displacement_value (a), displacement_value (b)),
                   displacement_value (n)) < 0.0f)
            std::swap (a, b);
          const displacement_t c = centre + n;
          dl.normal (direction_of (n));
          dl.vertex (c - a - b);
          dl.vertex (c + a - b);
          dl.vertex (c + a + b);
          dl.vertex (c - a - b);
          dl.vertex (c + a + b);
          dl.vertex (c - a + b);
        };
      face (along (u, hu), along (v, hv), along (w, hw));
      face (along (u, -hu), along (w, hw), along (v, hv));
      face (along (v, hv), along (w, hw), along (u, hu));
      face (along (v, -hv), along (u, hu), along (w, hw));
      face (along (w, hw), along (u, hu), along (v, hv));
      face (along (w, -hw), along (v, hv), along (u, hu));
    }

    // A quad facing `facing`, whichever way its corners go round.
    void quad (render::DrawList& dl,
               displacement_t p0,
               displacement_t p1,
               displacement_t p2,
               displacement_t p3,
               const direction_t& facing) {
      // The corners' cross product, square metres: its way is the normal.
      Vec3 n =
        cross (displacement_value (p1 - p0), displacement_value (p3 - p0));
      if (dot (n, direction_value (facing)) < 0.0f) {
        std::swap (p1, p3);
        n = -n;
      }
      if (length2 (n) < 1e-12f)
        return;
      dl.normal (unit_direction (normalized (n)));
      dl.vertex (p0);
      dl.vertex (p1);
      dl.vertex (p2);
      dl.vertex (p0);
      dl.vertex (p2);
      dl.vertex (p3);
    }

    // A beam from `a` to `b`, `half` across and `depth` high.
    void beam (render::DrawList& dl,
               const displacement_t& a,
               const displacement_t& b,
               meters_t half,
               meters_t depth,
               DisplayColor colour,
               scale_factor_t shade = 1.0f * scale_factor[one]) {
      const displacement_t run = b - a;
      const meters_t length = std::max (
        moppe::length (displacement_value (run)) * u::m, 1e-3f * u::m);
      const direction_t u =
        unit_direction (displacement_value (run) / meters_value (length));
      const direction_t side = unit_direction (
        normalized (cross (Vec3 (0.0f, 1.0f, 0.0f), direction_value (u))));
      const direction_t up =
        unit_direction (cross (direction_value (u), direction_value (side)));
      box (dl,
           (a + b) * 0.5f,
           u,
           side,
           up,
           0.5f * length,
           half,
           0.5f * depth,
           colour,
           shade);
    }
  }

  void BridgeSurface::rebuild (render::Renderer& renderer,
                               const map::SurfaceGeometry& surface,
                               const terrain::RoadNetwork& roads,
                               const terrain::RoadSurface& road_surface) {
    m_bridges.clear ();
    const terrain::RoadFormation& formation = road_surface.formation ();
    m_period = plan_extent (surface.domain ().period_x (),
                            surface.domain ().period_z ());
    // The ground's elevation under a plan point, read modulo the period.
    const auto ground = [&] (const plan_position_t& at) {
      return terrain::surface_elevation_value (
               spatial::sample<terrain::surface_elevation> (
                 surface,
                 at_height (into_period (at, m_period), 0.0f * u::m))) *
             u::m;
    };
    const meters_t half = formation.deck_half_width;
    const direction_t up = unit_direction (Vec3 (0.0f, 1.0f, 0.0f));
    std::uint32_t seed = 0;
    for (const terrain::Road& road : roads.roads)
      for (const terrain::RoadBridge& bridge : road.bridges) {
        const std::size_t first = bridge.first;
        const std::size_t last = bridge.last;
        if (last <= first || last >= road.points.size () ||
            road.heights.size () != road.points.size ())
          continue;
        // The road's points are raw storage (roads.hh): each becomes a
        // place here.
        const auto at = [&] (std::size_t k) {
          return terrain::at_elevation (road.points[k], road.heights[k]);
        };
        const position_t origin = at (first);
        // Unwrapped, the line runs on past the period; the mesh is built
        // about its first point and placed by its image inside the period.
        const plan_position_t centre = into_period (plan (origin), m_period);
        // A point of the mesh: its plan offset from the origin, at its own
        // height.
        const auto local = [&] (const position_t& p) {
          const Vec3& v = position_value (p);
          const Vec3& o = position_value (origin);
          return displacement (Vec3 (v[0] - o[0], v[1], v[2] - o[2]));
        };
        // The way across at each point, left of the way along.
        const auto side_at = [&] (std::size_t k) {
          const position_t a = at (k > first ? k - 1 : k);
          const position_t b = at (k < last ? k + 1 : k);
          const Vec2 d = plan_value (plan (b)) - plan_value (plan (a));
          return unit_direction (normalized (Vec3 (-d.z (), 0.0f, d.x ())));
        };

        render::DrawList dl;
        dl.lit (true);
        dl.fogged (true);
        dl.material (0.0f * proportion[one],
                     0.9f * proportion[one],
                     0.0f * proportion[one],
                     0.0f * proportion[one]);
        dl.begin (render::Prim::Triangles);
        meters_t walked = 0.0f * u::m;
        meters_t next_post = 0.0f * u::m;
        meters_t next_trestle = 12.0f * u::m;
        const meters_t span = [&] {
          meters_t s = 0.0f * u::m;
          for (std::size_t k = first + 1; k <= last; ++k)
            s += length (
                   displacement_value (local (at (k)) - local (at (k - 1)))) *
                 u::m;
          return s;
        }();
        for (std::size_t k = first + 1; k <= last; ++k) {
          const displacement_t a = local (at (k - 1));
          const displacement_t b = local (at (k));
          const direction_t sa = side_at (k - 1);
          const direction_t sb = side_at (k);
          const meters_t run =
            moppe::length (displacement_value (b - a)) * u::m;
          const direction_t u = unit_direction (
            displacement_value (b - a) / std::max (meters_value (run), 1e-3f));
          const direction_t side = unit_direction (
            normalized (cross (direction_value (u), direction_value (up))));
          const direction_t deck_up = unit_direction (
            cross (direction_value (side), direction_value (u)));
          // Planks across the way, a little more than a third of a metre
          // each, laid on the stringers; their top is the deck the
          // physics drives on.
          const int planks =
            std::max (1, static_cast<int> (meters_value (run) / 0.36f));
          for (int p = 0; p < planks; ++p) {
            const blend_weight_t t =
              (static_cast<float> (p) + 0.5f) / planks * blend_weight[one];
            const displacement_t c =
              between (a, b, t) - along (deck_up, 0.04f * u::m);
            box (dl,
                 c,
                 u,
                 side,
                 deck_up,
                 0.5f * run / static_cast<float> (planks) - 0.015f * u::m,
                 half + 0.15f * u::m,
                 0.04f * u::m,
                 plank,
                 shade_of (0.82f, 0.3f, jitter (++seed)));
          }
          // Long timbers share a cross section at each station. Adjacent
          // spans meet on the bend, with end grain only at the two banks.
          const auto timber = [&] (meters_t offset,
                                   meters_t y,
                                   meters_t width,
                                   meters_t depth,
                                   DisplayColor colour,
                                   scale_factor_t shade) {
            const auto corner = [&] (bool end, int x, int z) {
              return (end ? b : a) +
                     along (end ? sb : sa, offset + float (x) * width) +
                     along (up, y + float (z) * depth);
            };
            dl.color (scale_display (colour, shade));
            for (const int sign : { -1, 1 }) {
              quad (dl,
                    corner (false, -1, sign),
                    corner (true, -1, sign),
                    corner (true, 1, sign),
                    corner (false, 1, sign),
                    sign > 0 ? up : -up);
              quad (dl,
                    corner (false, sign, -1),
                    corner (true, sign, -1),
                    corner (true, sign, 1),
                    corner (false, sign, 1),
                    sign > 0 ? side : -side);
            }
            for (const bool end : { false, true }) {
              if (end ? k != last : k != first + 1)
                continue;
              quad (dl,
                    corner (end, -1, -1),
                    corner (end, -1, 1),
                    corner (end, 1, 1),
                    corner (end, 1, -1),
                    end ? u : -u);
            }
          };
          for (const int sign : { -1, 1 }) {
            timber (sign * 1.5f * u::m,
                    -0.38f * u::m,
                    0.16f * u::m,
                    0.30f * u::m,
                    tarred,
                    1.0f * scale_factor[one]);
            timber (float (sign) * (half + 0.12f * u::m),
                    0.1f * u::m,
                    0.12f * u::m,
                    0.1f * u::m,
                    rail,
                    0.9f * scale_factor[one]);
            const meters_t edge = float (sign) * (half + 0.3f * u::m);
            timber (edge,
                    1.05f * u::m,
                    0.05f * u::m,
                    0.06f * u::m,
                    rail,
                    1.0f * scale_factor[one]);
            timber (edge,
                    0.55f * u::m,
                    0.04f * u::m,
                    0.045f * u::m,
                    rail,
                    0.92f * scale_factor[one]);
          }
          // Posts every two metres or so.
          while (next_post <= walked + run) {
            const blend_weight_t t =
              std::clamp (meters_value (next_post - walked) /
                            meters_value (run),
                          0.0f,
                          1.0f) *
              blend_weight[one];
            const displacement_t c = between (a, b, t);
            for (const int sign : { -1, 1 })
              box (dl,
                   c +
                     along (side,
                            static_cast<float> (sign) * (half + 0.3f * u::m)) +
                     along (up, 0.5f * u::m),
                   u,
                   side,
                   up,
                   0.06f * u::m,
                   0.06f * u::m,
                   0.6f * u::m,
                   rail,
                   0.85f * scale_factor[one]);
            next_post +=
              span / std::max (1.0f, std::round (meters_value (span) / 2.2f));
          }
          // A trestle standing in the river: two raked legs down to the
          // bed under a cap beam.
          while (next_trestle < std::min (walked + run, span - 6.0f * u::m)) {
            const blend_weight_t t = meters_value (next_trestle - walked) /
                                     meters_value (run) * blend_weight[one];
            const displacement_t c = between (a, b, t);
            const displacement_t cap = c - along (up, 0.85f * u::m);
            beam (dl,
                  cap - along (side, half),
                  cap + along (side, half),
                  0.18f * u::m,
                  0.3f * u::m,
                  tarred);
            for (const int sign : { -1, 1 }) {
              const float s = static_cast<float> (sign);
              const displacement_t top =
                cap + along (side, s * (half - 0.7f * u::m));
              const displacement_t foot_xz =
                c + along (side, s * (half - 0.1f * u::m));
              const meters_t bed =
                ground (plan (origin) + plan (foot_xz)) - 0.6f * u::m;
              const meters_t top_y = displacement_value (top)[1] * u::m;
              const Vec3& foot = displacement_value (foot_xz);
              beam (dl,
                    top,
                    displacement (
                      Vec3 (foot[0],
                            meters_value (std::min (bed, top_y - 0.5f * u::m)),
                            foot[2])),
                    0.16f * u::m,
                    0.32f * u::m,
                    tarred,
                    0.9f * scale_factor[one]);
            }
            next_trestle +=
              span / std::max (1.0f, std::round (meters_value (span) / 12.0f));
          }
          walked += run;
        }
        // The abutments: granite blocks at each end, from the deck down
        // into the bank.
        for (const std::size_t k : { first, last }) {
          const displacement_t c = local (at (k));
          const Vec3& cv = displacement_value (c);
          const direction_t inward =
            k == first ? direction_of (local (at (first + 1)) - c)
                       : direction_of (local (at (last - 1)) - c);
          const Vec3& in = direction_value (inward);
          const direction_t flat =
            unit_direction (normalized (Vec3 (in[0], 0.0f, in[2])));
          const direction_t side = side_at (k);
          const meters_t bank = ground (plan (at (k))) - 1.5f * u::m;
          const meters_t c_y = cv[1] * u::m;
          const meters_t bottom = std::min (bank, c_y - 1.0f * u::m);
          const meters_t height = c_y - 0.14f * u::m - bottom;
          box (dl,
               displacement (
                 Vec3 (cv[0], meters_value (bottom + 0.5f * height), cv[2])) +
                 along (flat, 0.8f * u::m),
               flat,
               side,
               up,
               1.0f * u::m,
               half + 0.6f * u::m,
               0.5f * height,
               stone);
        }
        dl.end ();
        m_bridges.push_back ({ renderer.create_mesh (dl), centre });
      }
    add_guardrails (renderer, road_surface);
  }

  void BridgeSurface::add_guardrails (render::Renderer& renderer,
                                      const terrain::RoadSurface& roads) {
    // Galvanized steel weathered dull, the posts a little darker.
    constexpr DisplayColor steel { 0.60f, 0.62f, 0.63f };
    constexpr DisplayColor post_steel { 0.48f, 0.50f, 0.51f };
    // The W-beam's section, bottom to top: its height over its middle,
    // and how far it stands out toward the road from the posts' line.
    struct Fold {
      meters_t rise;
      meters_t out;
    };
    constexpr Fold section[] = {
      { -0.155f * u::m, 0.0f * u::m },   { -0.105f * u::m, 0.083f * u::m },
      { -0.045f * u::m, 0.083f * u::m }, { 0.0f * u::m, 0.035f * u::m },
      { 0.045f * u::m, 0.083f * u::m },  { 0.105f * u::m, 0.083f * u::m },
      { 0.155f * u::m, 0.0f * u::m },
    };
    constexpr std::size_t spans_a_mesh = 40;
    const direction_t up = unit_direction (Vec3 (0.0f, 1.0f, 0.0f));
    // The beam from one line point to the next, both faces: the road's
    // side bulging toward it, the back a sheet's thickness behind.
    const auto beam_span = [&] (render::DrawList& dl,
                                const displacement_t& a,
                                const direction_t& oa,
                                const displacement_t& b,
                                const direction_t& ob,
                                scale_factor_t shade) {
      dl.color (scale_display (steel, shade));
      // Toward the road, twice over (not a unit vector).
      const Vec3 toward = -(direction_value (oa) + direction_value (ob));
      for (std::size_t j = 0; j + 1 < std::size (section); ++j) {
        const auto [h0, d0] = section[j];
        const auto [h1, d1] = section[j + 1];
        // The facet's own normal in the section, toward the road.
        const direction_t facing = unit_direction (
          normalized (toward * meters_value (h1 - h0) -
                      direction_value (up) * meters_value (d1 - d0) * 2.0f));
        quad (dl,
              a - along (oa, d0) + along (up, h0),
              b - along (ob, d0) + along (up, h0),
              b - along (ob, d1) + along (up, h1),
              a - along (oa, d1) + along (up, h1),
              facing);
        const meters_t sheet = 0.004f * u::m;
        quad (dl,
              a - along (oa, d0 - sheet) + along (up, h0),
              b - along (ob, d0 - sheet) + along (up, h0),
              b - along (ob, d1 - sheet) + along (up, h1),
              a - along (oa, d1 - sheet) + along (up, h1),
              -facing);
      }
    };
    std::uint32_t seed = 0x67726c73U;
    for (const terrain::Guardrail& run : roads.guardrails ()) {
      const std::size_t n = run.posts.size ();
      // The posts are raw storage (roads.hh): each becomes a point of the
      // mesh, its plan offset from `origin` at the beam's height, and the
      // way away from the road.
      const auto line = [&] (std::size_t i, const plan_position_t& origin) {
        const terrain::Guardrail::Post& p = run.posts[i];
        const Vec2& at = plan_value (p.at);
        const Vec2& o = plan_value (origin);
        return displacement (Vec3 (at.x () - o.x (),
                                   terrain::surface_elevation_value (p.rail),
                                   at.z () - o.z ()));
      };
      const auto out = [&] (std::size_t i) {
        const Vec2& o = plan_value (run.posts[i].out);
        return unit_direction (Vec3 (o.x (), 0.0f, o.z ()));
      };
      for (std::size_t first = 0; first + 1 < n; first += spans_a_mesh) {
        const std::size_t last = std::min (n - 1, first + spans_a_mesh);
        const plan_position_t origin = run.posts[first].at;
        render::DrawList dl;
        dl.lit (true);
        dl.fogged (true);
        dl.material (0.75f * proportion[one],
                     0.45f * proportion[one],
                     0.0f * proportion[one],
                     0.0f * proportion[one]);
        dl.begin (render::Prim::Triangles);
        for (std::size_t i = first + 1; i <= last; ++i)
          beam_span (dl,
                     line (i - 1, origin),
                     out (i - 1),
                     line (i, origin),
                     out (i),
                     shade_of (0.9f, 0.12f, jitter (++seed)));
        // The ends turn down into the ground and away from the road, so
        // that nothing meets a beam's end square on.
        for (const std::size_t end : { std::size_t (0), n - 1 }) {
          if (end < first || end > last)
            continue;
          const std::size_t inner = end == 0 ? 1 : n - 2;
          const displacement_t p = line (end, origin);
          const direction_t o = out (end);
          const Vec3 back = displacement_value (p - line (inner, origin));
          const direction_t away =
            unit_direction (normalized (Vec3 (back[0], 0.0f, back[2])));
          const meters_t buried =
            terrain::surface_elevation_value (run.posts[end].foot) * u::m -
            0.12f * u::m;
          const meters_t p_y = displacement_value (p)[1] * u::m;
          const displacement_t mid = p + along (away, 2.0f * u::m) +
                                     along (o, 0.15f * u::m) +
                                     along (up, 0.6f * (buried - p_y));
          const Vec3 reach = displacement_value (p + along (away, 4.0f * u::m) +
                                                 along (o, 0.45f * u::m));
          const displacement_t tip =
            displacement (Vec3 (reach[0], meters_value (buried), reach[2]));
          beam_span (dl, p, o, mid, o, 0.92f * scale_factor[one]);
          beam_span (dl, mid, o, tip, o, 0.92f * scale_factor[one]);
        }
        // The posts behind the beam on spacers, down into the ground.
        const std::size_t first_post = first == 0 ? 0 : first + 1;
        for (std::size_t i = first_post; i <= last; ++i) {
          const displacement_t p = line (i, origin);
          const direction_t o = out (i);
          const direction_t t =
            unit_direction (cross (direction_value (up), direction_value (o)));
          const meters_t foot =
            terrain::surface_elevation_value (run.posts[i].foot) * u::m -
            0.4f * u::m;
          const meters_t top = displacement_value (p)[1] * u::m + 0.12f * u::m;
          const Vec3 base = displacement_value (p + along (o, 0.17f * u::m));
          box (dl,
               displacement (
                 Vec3 (base[0], meters_value (0.5f * (foot + top)), base[2])),
               o,
               t,
               up,
               0.05f * u::m,
               0.06f * u::m,
               0.5f * (top - foot),
               post_steel,
               shade_of (0.9f, 0.15f, jitter (++seed)));
          box (dl,
               p + along (o, 0.065f * u::m),
               o,
               t,
               up,
               0.055f * u::m,
               0.04f * u::m,
               0.1f * u::m,
               post_steel,
               0.8f * scale_factor[one]);
        }
        dl.end ();
        m_bridges.push_back ({ renderer.create_mesh (dl),
                               into_period (origin, m_period),
                               900.0f * u::m });
      }
    }
  }

  void BridgeSurface::clear () {
    m_bridges.clear ();
  }

  void BridgeSurface::draw (render::Renderer& renderer,
                            const position_t& camera) const {
    const Vec3& eye = position_value (camera);
    const Vec2& period = plan_value (m_period);
    for (const Bridge& bridge : m_bridges) {
      // The mesh's image nearest the camera.
      const Vec2& centre = plan_value (bridge.centre);
      const plan_position_t image = plan_position (
        centre.x () +
          terrain::whole_periods (eye[0] - centre.x (), period.x ()),
        centre.z () +
          terrain::whole_periods (eye[2] - centre.z (), period.z ()));
      const Vec2& at = plan_value (image);
      if (std::hypot (at.x () - eye[0], at.z () - eye[2]) * u::m > bridge.reach)
        continue;
      renderer.draw_mesh (
        *bridge.mesh,
        Mat4::translation (position_value (at_height (image, 0.0f * u::m))),
        0);
    }
  }
}
