#include <moppe/game/mushrooms.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <span>

namespace moppe::game {
  namespace {
    constexpr float mushroom_tau = 6.2831853f;
    // Mushrooms are drawn larger than life, so a child walking through the
    // woods spots them over the moss as an eager forager would.
    constexpr float storybook_scale = 2.4f;
    // The share of trees with a group of mushrooms around them, and how
    // much likelier one is near the trailhead.
    constexpr float host_odds = 0.03f;
    constexpr float trailhead_bonus = 4.0f;
    constexpr float trailhead_reach = 220.0f;
    // No mushroom grows in the tread a rider follows.
    constexpr float mushroom_trail_tread = 0.5f;
    // The tiles the ground's mushrooms are meshed in, the distance they are
    // drawn to, and the distance past which a tile's mesh is let go.
    constexpr float tile_metres = 16.0f;
    constexpr float draw_reach = 75.0f;
    constexpr float keep_reach = 130.0f;
    // No mushroom stands closer than this to a tree's axis.
    constexpr float trunk_clearance = 1.1f;
    constexpr float trunk_cell_metres = 4.0f;
    // A walker reaches this far, horizontally, to pick.
    constexpr float pick_reach = 2.0f;

    // Each kind's ordinary height in metres at scale one.
    float kind_height (MushroomKind kind) {
      switch (kind) {
      case MushroomKind::chanterelle:
        return 0.11f;
      case MushroomKind::funnel_chanterelle:
        return 0.09f;
      case MushroomKind::porcini:
        return 0.17f;
      case MushroomKind::fly_agaric:
        return 0.24f;
      }
      return 0.1f;
    }

    // A point of a mushroom's profile: its distance from the axis and
    // height, in units of the mushroom's height, and the colour there.
    struct ProfilePoint {
      float radius;
      float height;
      DisplayColor colour;
    };

    // The body's frame: where the stem meets the ground, its axis, and two
    // directions across it.
    struct MushroomFrame {
      Vec3 base;
      Vec3 up;
      Vec3 east;
      Vec3 north;
      float height;
    };

    MushroomFrame mushroom_frame (const MushroomSite& site) {
      const Vec3 tilt (std::cos (site.lean_toward) * std::sin (site.lean),
                       std::cos (site.lean),
                       std::sin (site.lean_toward) * std::sin (site.lean));
      const Vec3 up = normalized (tilt);
      const Vec3 across (std::cos (site.yaw), 0.0f, std::sin (site.yaw));
      const Vec3 north = normalized (cross (across, up));
      const Vec3 east = cross (up, north);
      return { .base = site.base,
               .up = up,
               .east = east,
               .north = north,
               .height = kind_height (site.kind) * site.scale *
                         storybook_scale };
    }

    // The outward normal of a profile segment, in (radius, height).
    std::array<float, 2> segment_normal (const ProfilePoint& a,
                                         const ProfilePoint& b) {
      const float dr = b.radius - a.radius;
      const float dh = b.height - a.height;
      const float l = std::max (std::hypot (dr, dh), 1e-6f);
      return { dh / l, -dr / l };
    }

    // Sweeps `profile` around the frame's axis. `wobble` gives each
    // point's radius a factor by angle, for a wavy rim.
    template <typename Wobble>
    void revolve (render::DrawList& list,
                  const MushroomFrame& frame,
                  std::span<const ProfilePoint> profile,
                  int sides,
                  Wobble wobble) {
      const std::size_t count = profile.size ();
      std::vector<std::array<float, 2>> normals (count);
      for (std::size_t i = 0; i < count; ++i) {
        std::array<float, 2> n { 0, 0 };
        if (i > 0) {
          const auto s = segment_normal (profile[i - 1], profile[i]);
          n[0] += s[0];
          n[1] += s[1];
        }
        if (i + 1 < count) {
          const auto s = segment_normal (profile[i], profile[i + 1]);
          n[0] += s[0];
          n[1] += s[1];
        }
        normals[i] = n;
      }
      const auto point = [&] (std::size_t i, int side) {
        const float angle = mushroom_tau * static_cast<float> (side) /
                            static_cast<float> (sides);
        const Vec3 way =
          frame.east * std::cos (angle) + frame.north * std::sin (angle);
        const float r = profile[i].radius * wobble (i, angle);
        return frame.base + way * (r * frame.height) +
               frame.up * (profile[i].height * frame.height);
      };
      const auto normal = [&] (std::size_t i, int side) {
        const float angle = mushroom_tau * static_cast<float> (side) /
                            static_cast<float> (sides);
        const Vec3 way =
          frame.east * std::cos (angle) + frame.north * std::sin (angle);
        const Vec3 n = way * normals[i][0] + frame.up * normals[i][1];
        return length2 (n) > 1e-8f ? normalized (n) : frame.up;
      };
      const auto emit = [&] (std::size_t i, int side) {
        list.color (profile[i].colour);
        list.normal (normal (i, side));
        list.vertex (point (i, side));
      };
      for (std::size_t i = 0; i + 1 < count; ++i)
        for (int side = 0; side < sides; ++side) {
          const int next = side + 1;
          emit (i, side);
          emit (i + 1, side);
          emit (i + 1, next);
          emit (i, side);
          emit (i + 1, next);
          emit (i, next);
        }
    }

    const auto circular = [] (std::size_t, float) { return 1.0f; };

    void draw_chanterelle (render::DrawList& list,
                           const MushroomFrame& frame,
                           std::uint32_t seed) {
      // Egg-yolk gold all over: a stem flaring into a shallow funnel
      // with a wavy, rolled rim, the ridges beneath a little paler.
      const DisplayColor gold (0.96f, 0.66f, 0.12f);
      const DisplayColor pale (0.98f, 0.76f, 0.30f);
      const DisplayColor deep (0.90f, 0.56f, 0.08f);
      const ProfilePoint profile[] = {
        { 0.10f, -0.05f, pale },  { 0.10f, 0.25f, pale },
        { 0.15f, 0.52f, pale },   { 0.30f, 0.80f, pale },
        { 0.50f, 0.93f, gold },   { 0.54f, 0.88f, deep },
        { 0.50f, 0.97f, gold },   { 0.30f, 0.99f, gold },
        { 0.00f, 0.90f, deep },
      };
      const float phase = mushroom_tau * hash_lane (seed, 11);
      const float lobes = 4.0f + std::floor (3.0f * hash_lane (seed, 12));
      revolve (list, frame, profile, 14, [&] (std::size_t i, float angle) {
        return i >= 3 ? 1.0f + 0.13f * std::sin (lobes * angle + phase)
                      : 1.0f;
      });
    }

    void draw_funnel_chanterelle (render::DrawList& list,
                                  const MushroomFrame& frame,
                                  std::uint32_t seed) {
      // A thin, ochre stem opening into a dark brown trumpet, grey
      // beneath, hollow down the middle.
      const DisplayColor stem (0.80f, 0.64f, 0.30f);
      const DisplayColor under (0.52f, 0.48f, 0.42f);
      const DisplayColor brown (0.26f, 0.18f, 0.11f);
      const ProfilePoint profile[] = {
        { 0.07f, -0.05f, stem }, { 0.07f, 0.55f, stem },
        { 0.17f, 0.80f, under }, { 0.38f, 0.97f, under },
        { 0.42f, 0.96f, brown }, { 0.34f, 0.99f, brown },
        { 0.12f, 0.80f, brown }, { 0.00f, 0.60f, brown },
      };
      const float phase = mushroom_tau * hash_lane (seed, 11);
      revolve (list, frame, profile, 10, [&] (std::size_t i, float angle) {
        return i >= 3 ? 1.0f + 0.16f * std::sin (3.0f * angle + phase) : 1.0f;
      });
    }

    void draw_porcini (render::DrawList& list,
                       const MushroomFrame& frame,
                       std::uint32_t seed) {
      // A fat, pale club of a stem under a glossy brown bun with a
      // yellowish sponge beneath.
      const float tint = 0.08f * (hash_lane (seed, 11) - 0.5f);
      const DisplayColor stem (0.86f, 0.78f, 0.62f);
      const DisplayColor sponge (0.86f, 0.80f, 0.52f);
      const DisplayColor cap (0.46f + tint, 0.28f + tint, 0.13f);
      const DisplayColor crown (0.40f + tint, 0.23f + tint, 0.10f);
      const ProfilePoint profile[] = {
        { 0.17f, -0.05f, stem },  { 0.24f, 0.18f, stem },
        { 0.22f, 0.42f, stem },   { 0.17f, 0.60f, stem },
        { 0.20f, 0.62f, sponge }, { 0.42f, 0.65f, sponge },
        { 0.46f, 0.71f, cap },    { 0.44f, 0.84f, cap },
        { 0.33f, 0.96f, crown },  { 0.17f, 1.02f, crown },
        { 0.00f, 1.04f, crown },
      };
      revolve (list, frame, profile, 14, circular);
    }

    void draw_fly_agaric (render::DrawList& list,
                          const MushroomFrame& frame,
                          std::uint32_t seed) {
      // White stem rising from a bulb, a skirt below the cap, white gills,
      // and a red cap scattered with white warts.
      const DisplayColor white (0.94f, 0.92f, 0.86f);
      const DisplayColor red (0.86f, 0.10f, 0.05f);
      const DisplayColor orange (0.90f, 0.30f, 0.06f);
      const ProfilePoint profile[] = {
        { 0.13f, -0.04f, white }, { 0.15f, 0.06f, white },
        { 0.09f, 0.15f, white },  { 0.07f, 0.50f, white },
        { 0.075f, 0.64f, white }, { 0.13f, 0.58f, white },
        { 0.08f, 0.68f, white },  { 0.065f, 0.78f, white },
        { 0.10f, 0.79f, white },  { 0.40f, 0.80f, white },
        { 0.43f, 0.84f, orange }, { 0.38f, 0.93f, red },
        { 0.25f, 1.00f, red },    { 0.00f, 1.03f, red },
      };
      revolve (list, frame, profile, 16, circular);

      // The warts: little white flecks lying on the cap.
      const int warts = 10 + static_cast<int> (8.0f * hash_lane (seed, 13));
      for (int k = 0; k < warts; ++k) {
        const std::uint32_t wart = lattice_hash (seed, 0x77a7u, k);
        const float angle = mushroom_tau * hash_lane (wart, 0);
        // Along the cap from crown to rim, sampled between its profile
        // points.
        const float along = std::sqrt (hash_lane (wart, 1));
        const float r = 0.40f * along;
        const float h = 1.03f - 0.17f * along * along;
        const Vec3 way =
          frame.east * std::cos (angle) + frame.north * std::sin (angle);
        const Vec3 normal = normalized (frame.up * (1.0f - 0.6f * along) +
                                        way * (0.9f * along));
        const Vec3 centre = frame.base + way * (r * frame.height) +
                            frame.up * (h * frame.height) +
                            normal * (0.006f * frame.height);
        const Vec3 tangent = normalized (cross (normal, way));
        const Vec3 bitangent = cross (tangent, normal);
        const float size = frame.height * (0.025f + 0.025f * hash_lane (wart, 2));
        list.color (white);
        list.normal (normal);
        constexpr int corners = 5;
        for (int c = 0; c < corners; ++c) {
          const float a0 = mushroom_tau * c / corners;
          const float a1 = mushroom_tau * (c + 1) / corners;
          list.vertex (centre + normal * (0.3f * size));
          list.vertex (centre + tangent * (size * std::cos (a0)) +
                       bitangent * (size * std::sin (a0)));
          list.vertex (centre + tangent * (size * std::cos (a1)) +
                       bitangent * (size * std::sin (a1)));
        }
      }
    }

    // Which kind a tree's group is, by the tree.
    MushroomKind kind_for (ForestForm form, float draw) {
      if (form == ForestForm::conifer) {
        if (draw < 0.40f)
          return MushroomKind::chanterelle;
        if (draw < 0.70f)
          return MushroomKind::funnel_chanterelle;
        return MushroomKind::porcini;
      }
      if (draw < 0.45f)
        return MushroomKind::fly_agaric;
      if (draw < 0.80f)
        return MushroomKind::porcini;
      return MushroomKind::chanterelle;
    }

    // How many mushrooms a group holds: funnel chanterelles come in
    // crowds, porcini alone or in twos.
    int group_size (MushroomKind kind, float draw) {
      switch (kind) {
      case MushroomKind::chanterelle:
        return 3 + static_cast<int> (5.0f * draw);
      case MushroomKind::funnel_chanterelle:
        return 6 + static_cast<int> (8.0f * draw);
      case MushroomKind::porcini:
        return 1 + static_cast<int> (2.6f * draw);
      case MushroomKind::fly_agaric:
        return 1 + static_cast<int> (3.6f * draw);
      }
      return 1;
    }

    position_t probe (float x, float z) {
      return position (Vec3 (x, 0.0f, z));
    }
  }

  const char* mushroom_name (MushroomKind kind) {
    switch (kind) {
    case MushroomKind::chanterelle:
      return "chanterelle";
    case MushroomKind::funnel_chanterelle:
      return "funnel chanterelle";
    case MushroomKind::porcini:
      return "porcini";
    case MushroomKind::fly_agaric:
      return "fly agaric";
    }
    return "mushroom";
  }

  const char* mushroom_plural (MushroomKind kind) {
    switch (kind) {
    case MushroomKind::chanterelle:
      return "chanterelles";
    case MushroomKind::funnel_chanterelle:
      return "funnel chanterelles";
    case MushroomKind::porcini:
      return "porcini";
    case MushroomKind::fly_agaric:
      return "fly agarics";
    }
    return "mushrooms";
  }

  bool mushroom_edible (MushroomKind kind) {
    return kind != MushroomKind::fly_agaric;
  }

  MushroomPlan plan_mushrooms (const ForestPlan& forest,
                               const map::SurfaceGeometry& surface,
                               const map::SurfaceReadings& readings,
                               const terrain::WaterSheets& water,
                               std::uint32_t seed,
                               meters_t sea_level,
                               const Vec3& trailhead) {
    MOPPE_PROFILE_ZONE ("plan_mushrooms");
    const float sea = sea_level.numerical_value_in (u::m);
    const Vec3 period = extent_value (forest.period);
    // Every trunk, by the cell it stands in, so no mushroom grows through
    // a neighbouring tree.
    const auto cell_key = [] (float x, float z) {
      return (static_cast<std::uint64_t> (static_cast<std::uint32_t> (
                static_cast<int> (std::floor (x / trunk_cell_metres))))
              << 32) |
             static_cast<std::uint32_t> (
               static_cast<int> (std::floor (z / trunk_cell_metres)));
    };
    std::unordered_map<std::uint64_t, std::vector<Vec3>> trunks;
    for (const ForestSite& tree : forest.sites) {
      const Vec3 at = position_value (tree.position);
      trunks[cell_key (at[0], at[2])].push_back (at);
    }
    const auto clear_of_trunks = [&] (float x, float z) {
      for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
          const auto found =
            trunks.find (cell_key (x + dx * trunk_cell_metres,
                                   z + dz * trunk_cell_metres));
          if (found == trunks.end ())
            continue;
          for (const Vec3& trunk : found->second)
            if (std::hypot (trunk[0] - x, trunk[2] - z) < trunk_clearance)
              return false;
        }
      return true;
    };

    MushroomPlan plan;
    for (const ForestSite& tree : forest.sites) {
      if (tree.age == ForestAge::sapling)
        continue;
      const std::uint32_t identity = lattice_hash (tree.seed, 0x6d75u, seed);
      const Vec3 at = position_value (tree.position);
      const float from_trailhead =
        std::hypot (at[0] - trailhead[0], at[2] - trailhead[2]);
      const float odds =
        host_odds *
        (1.0f + (trailhead_bonus - 1.0f) *
                  (1.0f - smoothstep (0.4f * trailhead_reach,
                                      trailhead_reach,
                                      from_trailhead)));
      if (hash_lane (identity, 0) >= odds)
        continue;

      const MushroomKind kind = kind_for (tree.form, hash_lane (identity, 1));
      const int members = group_size (kind, hash_lane (identity, 2));
      // An arc around the trunk, as the fungus spreads out under the
      // tree's crown.
      const float middle = mushroom_tau * hash_lane (identity, 3);
      const float spread = 0.6f + 1.6f * hash_lane (identity, 4);
      const float distance = 1.3f + 2.2f * hash_lane (identity, 5);
      const float group_scale = 0.8f + 0.4f * hash_lane (identity, 6);
      for (int k = 0; k < members; ++k) {
        const std::uint32_t one_seed = lattice_hash (
          identity, static_cast<std::uint32_t> (k + 1), seed ^ 0x5b0e7u);
        const float place =
          members > 1 ? static_cast<float> (k) / (members - 1) - 0.5f : 0.0f;
        const float angle =
          middle + spread * place + 0.25f * (hash_lane (one_seed, 0) - 0.5f);
        const float reach = distance + 0.7f * (hash_lane (one_seed, 1) - 0.5f);
        float x = at[0] + reach * std::cos (angle);
        float z = at[2] + reach * std::sin (angle);
        x -= std::floor (x / period[0]) * period[0];
        z -= std::floor (z / period[2]) * period[2];
        if (!clear_of_trunks (x, z))
          continue;
        const position_t where = probe (x, z);
        const float ground = terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (surface, where));
        const float level = terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (water, where));
        const auto read = [&] (auto quantity) {
          return spatial::sample<quantity> (readings, where)
            .numerical_value_in (one);
        };
        if (ground < sea || level > ground ||
            read (map::trail_influence) > mushroom_trail_tread ||
            read (map::home_base_influence) > 0.02f)
          continue;
        MushroomSite site;
        site.kind = kind;
        site.seed = one_seed;
        site.scale = group_scale * (0.7f + 0.55f * hash_lane (one_seed, 2));
        site.yaw = mushroom_tau * hash_lane (one_seed, 3);
        site.lean = 0.18f * hash_lane (one_seed, 4);
        site.lean_toward = mushroom_tau * hash_lane (one_seed, 5);
        site.base = Vec3 (x, ground - 0.01f, z);
        plan.sites.push_back (site);
      }
    }
    std::cerr << "moppe: mushrooms: " << plan.sites.size () << " growing"
              << std::endl;
    return plan;
  }

  void draw_mushroom (render::DrawList& list, const MushroomSite& site) {
    const MushroomFrame frame = mushroom_frame (site);
    switch (site.kind) {
    case MushroomKind::chanterelle:
      draw_chanterelle (list, frame, site.seed);
      break;
    case MushroomKind::funnel_chanterelle:
      draw_funnel_chanterelle (list, frame, site.seed);
      break;
    case MushroomKind::porcini:
      draw_porcini (list, frame, site.seed);
      break;
    case MushroomKind::fly_agaric:
      draw_fly_agaric (list, frame, site.seed);
      break;
    }
  }

  std::uint64_t MushroomPatch::tile_key (int x, int z) {
    return (static_cast<std::uint64_t> (static_cast<std::uint32_t> (x))
            << 32) |
           static_cast<std::uint32_t> (z);
  }

  MushroomPatch::Tile* MushroomPatch::tile_at (const Vec3& at) {
    const auto found =
      m_tiles.find (tile_key (static_cast<int> (std::floor (at[0] / tile_metres)),
                              static_cast<int> (std::floor (at[2] / tile_metres))));
    return found == m_tiles.end () ? nullptr : &found->second;
  }

  void MushroomPatch::rebuild (MushroomPlan plan) {
    m_plan = std::move (plan);
    m_picked.assign (m_plan.sites.size (), false);
    m_followed = 0;
    m_tiles.clear ();
    for (std::uint32_t i = 0; i < m_plan.sites.size (); ++i) {
      const Vec3& at = m_plan.sites[i].base;
      m_tiles[tile_key (static_cast<int> (std::floor (at[0] / tile_metres)),
                        static_cast<int> (std::floor (at[2] / tile_metres)))]
        .sites.push_back (i);
    }
  }

  std::optional<std::uint32_t>
  MushroomPatch::within_reach (const Vec3& feet, const Vec3& heading) const {
    Vec3 facing = heading;
    facing[1] = 0.0f;
    facing = length2 (facing) > 1e-6f ? normalized (facing) : Vec3 (0, 0, 1);
    const int tx = static_cast<int> (std::floor (feet[0] / tile_metres));
    const int tz = static_cast<int> (std::floor (feet[2] / tile_metres));
    std::optional<std::uint32_t> best;
    float best_score = 1e9f;
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        const auto found = m_tiles.find (tile_key (tx + dx, tz + dz));
        if (found == m_tiles.end ())
          continue;
        for (const std::uint32_t index : found->second.sites) {
          if (m_picked[index])
            continue;
          const Vec3 offset = m_plan.sites[index].base - feet;
          if (std::abs (offset[1]) > 1.5f)
            continue;
          const float distance = std::hypot (offset[0], offset[2]);
          if (distance > pick_reach)
            continue;
          const float ahead =
            distance > 1e-3f
              ? (offset[0] * facing[0] + offset[2] * facing[2]) / distance
              : 1.0f;
          // Close at hand, or in front of the walker.
          if (ahead < -0.2f && distance > 0.8f)
            continue;
          const float score = distance - 0.8f * ahead;
          if (score < best_score) {
            best_score = score;
            best = index;
          }
        }
      }
    return best;
  }

  std::optional<std::uint32_t>
  MushroomPatch::nearest (const Vec3& at,
                          float within,
                          std::span<const std::uint32_t> skip) const {
    const int tx = static_cast<int> (std::floor (at[0] / tile_metres));
    const int tz = static_cast<int> (std::floor (at[2] / tile_metres));
    const int span = static_cast<int> (std::ceil (within / tile_metres));
    std::optional<std::uint32_t> best;
    float best_distance = within;
    for (int dz = -span; dz <= span; ++dz)
      for (int dx = -span; dx <= span; ++dx) {
        const auto found = m_tiles.find (tile_key (tx + dx, tz + dz));
        if (found == m_tiles.end ())
          continue;
        for (const std::uint32_t index : found->second.sites) {
          if (m_picked[index] ||
              std::find (skip.begin (), skip.end (), index) != skip.end ())
            continue;
          const Vec3& base = m_plan.sites[index].base;
          const float distance = std::hypot (base[0] - at[0], base[2] - at[2]);
          if (distance < best_distance) {
            best_distance = distance;
            best = index;
          }
        }
      }
    return best;
  }

  void MushroomPatch::follow (const Basket& basket) {
    if (basket.picked.size () == m_followed)
      return;
    if (basket.picked.size () < m_followed) {
      // An earlier basket came back: everything grows again first.
      for (std::uint32_t i = 0; i < m_picked.size (); ++i)
        if (m_picked[i]) {
          m_picked[i] = false;
          if (Tile* tile = tile_at (m_plan.sites[i].base))
            tile->dirty = true;
        }
      m_followed = 0;
    }
    for (; m_followed < basket.picked.size (); ++m_followed) {
      const std::uint32_t index = basket.picked[m_followed];
      if (index >= m_picked.size ())
        continue;
      m_picked[index] = true;
      if (Tile* tile = tile_at (m_plan.sites[index].base))
        tile->dirty = true;
    }
  }

  void MushroomPatch::draw (render::Renderer& renderer, const Vec3& camera) {
    if (m_tiles.empty ())
      return;
    const int tx = static_cast<int> (std::floor (camera[0] / tile_metres));
    const int tz = static_cast<int> (std::floor (camera[2] / tile_metres));
    const int span = static_cast<int> (std::ceil (draw_reach / tile_metres));
    const auto tile_distance = [&] (int x, int z) {
      const float cx = (static_cast<float> (x) + 0.5f) * tile_metres;
      const float cz = (static_cast<float> (z) + 0.5f) * tile_metres;
      return std::hypot (cx - camera[0], cz - camera[2]) -
             0.7072f * tile_metres;
    };

    // Let go of meshes long out of sight.
    for (auto& [key, tile] : m_tiles) {
      if (!tile.mesh)
        continue;
      const int x = static_cast<int> (static_cast<std::uint32_t> (key >> 32));
      const int z = static_cast<int> (static_cast<std::uint32_t> (key));
      if (tile_distance (x, z) > keep_reach) {
        tile.mesh.reset ();
        tile.dirty = true;
      }
    }

    render::DrawList list;
    for (int dz = -span; dz <= span; ++dz)
      for (int dx = -span; dx <= span; ++dx) {
        if (tile_distance (tx + dx, tz + dz) > draw_reach)
          continue;
        const auto found = m_tiles.find (tile_key (tx + dx, tz + dz));
        if (found == m_tiles.end ())
          continue;
        Tile& tile = found->second;
        if (tile.dirty) {
          list.clear ();
          render::DrawState solid;
          solid.cull = false;
          list.state (solid);
          list.lit (true);
          list.fogged (true);
          list.begin (render::Prim::Triangles);
          for (const std::uint32_t index : tile.sites)
            if (!m_picked[index])
              draw_mushroom (list, m_plan.sites[index]);
          list.end ();
          tile.mesh = list.empty () ? nullptr : renderer.create_mesh (list);
          tile.dirty = false;
        }
        if (tile.mesh)
          renderer.draw_mesh (*tile.mesh, Mat4 ());
      }
  }
}
