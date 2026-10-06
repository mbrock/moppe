#include <moppe/game/cairn.hh>

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
                      float radius,
                      float thickness,
                      float yaw,
                      const Vec3& tilt) {
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
                    const Vec3& base,
                    const Vec3& along,
                    std::uint32_t seed,
                    const std::function<float (float x, float z)>& ground) {
    CairnRandom random (seed * 2654435761u + 0x9e3779b9u);
    Vec3 forward = along;
    forward[1] = 0.0f;
    forward = length2 (forward) > 1e-6f ? normalized (forward) : Vec3 (0, 0, 1);
    const Vec3 side = cross (Vec3 (0, 1, 0), forward);

    // It stands at the trail's edge, not on it.
    Vec3 foot = base + side * 1.9f;
    foot[1] = ground (foot[0], foot[2]) - 0.08f;

    render::DrawState solid;
    solid.cull = false;
    list.state (solid);
    list.lit (true);
    list.fogged (true);
    list.begin (render::Prim::Triangles);

    // A course of rounded stones on the ground, then a column of flatter
    // ones narrowing as it rises, each set a little off the last, the way
    // a cairn is built by hand.
    float course = 0.0f;
    for (int i = 0; i < 6; ++i) {
      const float angle = 2.0f * PI * (i + random.between (-0.2f, 0.2f)) / 6;
      const float out = i == 5 ? 0.0f : random.between (0.34f, 0.42f);
      Vec3 at =
        foot + (side * std::cos (angle) + forward * std::sin (angle)) * out;
      at[1] =
        std::min (static_cast<float> (at[1]), ground (at[0], at[2]) - 0.06f);
      const float radius = random.between (0.3f, 0.4f);
      const float thickness = radius * random.between (0.6f, 0.85f);
      course = std::max (course, thickness);
      cairn_stone_colour (list, random);
      cairn_stone (list,
                   random,
                   at,
                   radius,
                   thickness,
                   random.between (0.0f, 2.0f * PI),
                   Vec3 (random.between (-0.12f, 0.12f),
                         0.0f,
                         random.between (-0.12f, 0.12f)));
    }
    constexpr std::array<float, 5> radii { 0.5f, 0.41f, 0.35f, 0.27f, 0.19f };
    Vec3 at = foot + Vec3 (0, course * 0.8f, 0);
    for (const float radius : radii) {
      const float thickness = radius * random.between (0.42f, 0.8f);
      const Vec3 tilt (
        random.between (-0.1f, 0.1f), 0.0f, random.between (-0.1f, 0.1f));
      cairn_stone_colour (list, random);
      cairn_stone (list,
                   random,
                   at,
                   radius,
                   thickness,
                   random.between (0.0f, 2.0f * PI),
                   tilt);
      at += Vec3 (0, thickness * 0.88f, 0) +
            side * (radius * random.between (-0.16f, 0.16f)) +
            forward * (radius * random.between (-0.16f, 0.16f));
    }

    // A few stones left lying at its foot.
    for (int i = 0; i < 4; ++i) {
      const float angle = random.between (0.0f, 2.0f * PI);
      Vec3 loose =
        foot + (side * std::cos (angle) + forward * std::sin (angle)) *
                 random.between (0.8f, 1.3f);
      loose[1] = ground (loose[0], loose[2]) - 0.04f;
      const float radius = random.between (0.12f, 0.24f);
      cairn_stone_colour (list, random);
      cairn_stone (list,
                   random,
                   loose,
                   radius,
                   radius * random.between (0.45f, 0.7f),
                   random.between (0.0f, 2.0f * PI),
                   Vec3 (random.between (-0.15f, 0.15f),
                         0.0f,
                         random.between (-0.15f, 0.15f)));
    }
    list.end ();
    list.state (render::DrawState ());
  }
}
