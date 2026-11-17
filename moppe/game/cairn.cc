#include <moppe/game/cairn.hh>

#include <moppe/terrain/domain.hh>

#include <algorithm>
#include <array>
#include <cmath>

namespace moppe::game {
  namespace {
    // A small deterministic stream, so the cairn is the same stack of
    // stones on every launch of a world.
    class CairnRandom {
    public:
      explicit CairnRandom (std::uint32_t seed) : m_state (seed | 1u) {}

      float unit () {
        m_state ^= m_state << 13;
        m_state ^= m_state >> 17;
        m_state ^= m_state << 5;
        return static_cast<float> (m_state & 0xffffffu) / 16777215.0f;
      }
      float between (float low, float high) {
        return low + (high - low) * unit ();
      }
      meters_t between (meters_t low, meters_t high) {
        return between (meters_value (low), meters_value (high)) * u::m;
      }
      radians_t between (radians_t low, radians_t high) {
        return between (radians_value (low), radians_value (high)) * u::rad;
      }

    private:
      std::uint32_t m_state;
    };

    void cairn_facet (render::DrawList& list,
                      const Vec3& a,
                      const Vec3& b,
                      const Vec3& c) {
      Vec3 n = cross (b - a, c - a);
      if (length2 (n) < 1e-12f)
        return;
      n = normalized (n);
      list.normal (n);
      list.vertex (a);
      list.vertex (b);
      list.vertex (c);
    }

    // One flat fieldstone: a jittered lower and upper ring between a
    // slightly domed top and a flat bottom, every face flat-shaded like the
    // world's boulders.  `centre` is the middle of its bottom face.
    void cairn_stone (render::DrawList& list,
                      CairnRandom& random,
                      const Vec3& centre,
                      meters_t radius_m,
                      meters_t thickness_m,
                      radians_t yaw_rad,
                      const Vec3& tilt) {
      // Raw floats from here on: the facets go straight to the draw list.
      const float radius = meters_value (radius_m);
      const float thickness = meters_value (thickness_m);
      const float yaw = radians_value (yaw_rad);
      constexpr int sides = 7;
      std::array<Vec3, sides> lower, upper;
      const Vec3 up = normalized (Vec3 (0, 1, 0) + tilt);
      Vec3 east = cross (up, Vec3 (0, 0, 1));
      east = normalized (east);
      const Vec3 north = cross (east, up);
      const float squash = random.between (0.75f, 1.0f);
      for (int i = 0; i < sides; ++i) {
        const float angle =
          yaw + 2.0f * PI * (i + random.between (-0.18f, 0.18f)) / sides;
        const Vec3 way =
          east * std::cos (angle) + north * (std::sin (angle) * squash);
        lower[i] = centre + way * (radius * random.between (0.88f, 1.06f)) +
                   up * (thickness * random.between (0.1f, 0.3f));
        upper[i] = centre + way * (radius * random.between (0.7f, 0.92f)) +
                   up * (thickness * random.between (0.66f, 0.86f));
      }
      const Vec3 top = centre + up * thickness +
                       east * (radius * random.between (-0.2f, 0.2f)) +
                       north * (radius * random.between (-0.2f, 0.2f));
      const Vec3 bottom = centre;
      for (int i = 0; i < sides; ++i) {
        const int j = (i + 1) % sides;
        cairn_facet (list, top, upper[i], upper[j]);
        cairn_facet (list, upper[i], lower[i], lower[j]);
        cairn_facet (list, upper[i], lower[j], upper[j]);
        cairn_facet (list, lower[i], bottom, lower[j]);
      }
    }

    // Weathered granite, warm in the light and some of it lichened.
    void cairn_stone_colour (render::DrawList& list, CairnRandom& random) {
      const float grey = random.between (0.5f, 0.64f);
      const float lichen =
        random.unit () < 0.35f ? random.between (0.03f, 0.07f) : 0.0f;
      list.color (grey * 1.04f + lichen * 0.5f,
                  grey + lichen,
                  grey * 0.86f - lichen * 0.5f);
    }
  }

  void build_cairn (render::DrawList& list,
                    const position_t& base,
                    const direction_t& along,
                    std::uint32_t seed,
                    const terrain::ElevationField& ground) {
    CairnRandom random (seed * 2654435761u + 0x9e3779b9u);
    Vec3 forward = direction_value (along);
    forward[1] = 0.0f;
    forward = length2 (forward) > 1e-6f ? normalized (forward) : Vec3 (0, 0, 1);
    const Vec3 side = cross (Vec3 (0, 1, 0), forward);
    // The ground's height under a point, for the stones' floats.
    const auto height = [&ground] (const Vec3& at) {
      return terrain::surface_elevation_value (
        ground (plan_position (at[0], at[2])));
    };

    // It stands at the trail's edge, not on it.
    constexpr meters_t off_trail = 1.9f * u::m;
    constexpr meters_t sunk = 0.08f * u::m;
    Vec3 foot = position_value (base) + side * meters_value (off_trail);
    foot[1] = height (foot) - meters_value (sunk);

    render::DrawState solid;
    solid.cull = false;
    list.state (solid);
    list.lit (true);
    list.fogged (true);
    list.begin (render::Prim::Triangles);

    // A course of rounded stones on the ground, then a column of flatter
    // ones narrowing as it rises, each set a little off the last, the way
    // a cairn is built by hand.
    constexpr radians_t turn = 2.0f * PI * u::rad;
    meters_t course = 0.0f * u::m;
    for (int i = 0; i < 6; ++i) {
      const radians_t angle =
        2.0f * PI * (i + random.between (-0.2f, 0.2f)) / 6 * u::rad;
      const meters_t out =
        i == 5 ? 0.0f * u::m : random.between (0.34f * u::m, 0.42f * u::m);
      Vec3 at = foot + (side * std::cos (radians_value (angle)) +
                        forward * std::sin (radians_value (angle))) *
                         meters_value (out);
      at[1] = std::min (static_cast<float> (at[1]), height (at) - 0.06f);
      const meters_t radius = random.between (0.3f * u::m, 0.4f * u::m);
      const meters_t thickness = radius * random.between (0.6f, 0.85f);
      course = std::max (course, thickness);
      cairn_stone_colour (list, random);
      cairn_stone (list,
                   random,
                   at,
                   radius,
                   thickness,
                   random.between (0.0f * u::rad, turn),
                   Vec3 (random.between (-0.12f, 0.12f),
                         0.0f,
                         random.between (-0.12f, 0.12f)));
    }
    constexpr std::array<meters_t, 5> radii {
      0.5f * u::m, 0.41f * u::m, 0.35f * u::m, 0.27f * u::m, 0.19f * u::m
    };
    Vec3 at = foot + Vec3 (0, meters_value (course) * 0.8f, 0);
    for (const meters_t radius : radii) {
      const meters_t thickness = radius * random.between (0.42f, 0.8f);
      const Vec3 tilt (
        random.between (-0.1f, 0.1f), 0.0f, random.between (-0.1f, 0.1f));
      cairn_stone_colour (list, random);
      cairn_stone (list,
                   random,
                   at,
                   radius,
                   thickness,
                   random.between (0.0f * u::rad, turn),
                   tilt);
      const float r = meters_value (radius);
      at += Vec3 (0, meters_value (thickness) * 0.88f, 0) +
            side * (r * random.between (-0.16f, 0.16f)) +
            forward * (r * random.between (-0.16f, 0.16f));
    }

    // A few stones left lying at its foot.
    for (int i = 0; i < 4; ++i) {
      const radians_t angle = random.between (0.0f * u::rad, turn);
      Vec3 loose =
        foot + (side * std::cos (radians_value (angle)) +
                forward * std::sin (radians_value (angle))) *
                 meters_value (random.between (0.8f * u::m, 1.3f * u::m));
      loose[1] = height (loose) - 0.04f;
      const meters_t radius = random.between (0.12f * u::m, 0.24f * u::m);
      cairn_stone_colour (list, random);
      cairn_stone (list,
                   random,
                   loose,
                   radius,
                   radius * random.between (0.45f, 0.7f),
                   random.between (0.0f * u::rad, turn),
                   Vec3 (random.between (-0.15f, 0.15f),
                         0.0f,
                         random.between (-0.15f, 0.15f)));
    }
    list.end ();
    list.state (render::DrawState ());
  }

  void build_fieldstone (render::DrawList& list,
                         std::uint32_t seed,
                         const position_t& centre,
                         meters_t radius,
                         meters_t thickness,
                         radians_t yaw,
                         const Vec3& tilt) {
    CairnRandom random (seed * 2246822519u + 0x165667b1u);
    list.begin (render::Prim::Triangles);
    cairn_stone_colour (list, random);
    cairn_stone (
      list, random, position_value (centre), radius, thickness, yaw, tilt);
    list.end ();
  }
}
