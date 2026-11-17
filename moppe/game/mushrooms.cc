#include <moppe/game/mushrooms.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <span>

namespace moppe::game {
  namespace {
    constexpr radians_t mushroom_tau = 6.2831853f * u::rad;
    // Mushrooms are drawn larger than life, so a child walking through the
    // woods spots them over the moss and the long grass as an eager
    // forager would.
    constexpr scale_factor_t storybook_scale = 3.4f * scale_factor[one];
    // The share of trees with a group of mushrooms around them, and how
    // much likelier one is near the trailhead: a good autumn, so that a
    // walk from it finds some.
    constexpr probability_t host_odds = 0.09f * probability[one];
    constexpr scale_factor_t trailhead_bonus = 5.0f * scale_factor[one];
    constexpr meters_t trailhead_reach = 350.0f * u::m;
    // Nor in the trailhead's own clearing, though they come up close by.
    constexpr proportion_t mushroom_home_base_clearing =
      0.35f * proportion[one];
    // No mushroom grows in the tread a rider follows.
    constexpr proportion_t mushroom_trail_tread = 0.5f * proportion[one];
    // The tiles the ground's mushrooms are meshed in, the distance they are
    // drawn to, and the distance past which a tile's mesh is let go.
    constexpr meters_t tile_metres = 16.0f * u::m;
    constexpr meters_t draw_reach = 95.0f * u::m;
    constexpr meters_t keep_reach = 150.0f * u::m;
    // No mushroom stands closer than this to a tree's axis.
    constexpr meters_t trunk_clearance = 1.1f * u::m;
    constexpr meters_t trunk_cell_metres = 4.0f * u::m;
    // A walker reaches this far, horizontally, to pick.
    constexpr meters_t pick_reach = 2.0f * u::m;

    struct MushroomFrame;

    // What each kind is (mushroom_kinds, after the drawing below).
    struct MushroomKindRow {
      // What a walker calls it, singular and plural.
      const char* name;
      const char* plural;
      // Fly agaric is admired, not picked.
      bool edible;
      // Its ordinary height at scale one.
      meters_t height;
      // How many a group holds: `least`, and up to `more` again as the
      // group's draw rises.
      int least;
      float more;
      void (*draw) (render::DrawList&, const MushroomFrame&, std::uint32_t);
    };
    const MushroomKindRow& row (MushroomKind kind);

    // A point of a mushroom's profile: its distance from the axis and
    // height, in units of the mushroom's height, and the colour there.
    struct ProfilePoint {
      ProfilePoint (float r, float h, DisplayColor c)
          : radius (r * scale_factor[one]), height (h * scale_factor[one]),
            colour (c) {}
      scale_factor_t radius;
      // Below zero, the stem's foot under the ground.
      scale_factor_t height;
      DisplayColor colour;
    };

    // The body's frame: where the stem meets the ground, its axis, and two
    // directions across it.
    struct MushroomFrame {
      position_t base;
      direction_t up;
      direction_t east;
      direction_t north;
      meters_t height;
      doneness_t cooked;
    };

    // A colour as the pan leaves it: browned, then charred.
    DisplayColor seared (const DisplayColor& raw, doneness_t doneness) {
      const DisplayColor brown (0.46f, 0.27f, 0.11f);
      const DisplayColor char_black (0.06f, 0.05f, 0.045f);
      const float cooked = doneness.numerical_value_in (one);
      if (cooked <= 0.0f)
        return raw;
      if (cooked <= 1.0f)
        return mix_display (raw, brown, 0.72f * cooked);
      return mix_display (mix_display (raw, brown, 0.72f),
                          char_black,
                          std::min (1.0f, cooked - 1.0f));
    }

    MushroomFrame mushroom_frame (const MushroomSite& site) {
      const float lean = radians_value (site.lean);
      const float lean_toward = radians_value (site.lean_toward);
      const float yaw = radians_value (site.yaw);
      const Vec3 tilt (std::cos (lean_toward) * std::sin (lean),
                       std::cos (lean),
                       std::sin (lean_toward) * std::sin (lean));
      const Vec3 up = normalized (tilt);
      const Vec3 across (std::cos (yaw), 0.0f, std::sin (yaw));
      const Vec3 north = normalized (cross (across, up));
      const Vec3 east = cross (up, north);
      return { .base = site.base,
               .up = unit_direction (up),
               .east = unit_direction (east),
               .north = unit_direction (north),
               .height = quantity_cast<isq::length> (
                 row (site.kind).height * site.scale * storybook_scale),
               .cooked = site.cooked };
    }

    // The outward normal of a profile segment, in (radius, height).
    std::array<float, 2> segment_normal (const ProfilePoint& a,
                                         const ProfilePoint& b) {
      const float dr = (b.radius - a.radius).numerical_value_in (one);
      const float dh = (b.height - a.height).numerical_value_in (one);
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
      // The vertices are written in the frame's metres.
      const Vec3& base = position_value (frame.base);
      const Vec3& up = direction_value (frame.up);
      const Vec3& east = direction_value (frame.east);
      const Vec3& north = direction_value (frame.north);
      const float height = meters_value (frame.height);
      const auto point = [&] (std::size_t i, int side) {
        const radians_t angle =
          mushroom_tau * static_cast<float> (side) / static_cast<float> (sides);
        const float a = radians_value (angle);
        const Vec3 way = east * std::cos (a) + north * std::sin (a);
        const float r =
          (profile[i].radius * wobble (i, angle)).numerical_value_in (one);
        return base + way * (r * height) +
               up * (profile[i].height.numerical_value_in (one) * height);
      };
      const auto normal = [&] (std::size_t i, int side) {
        const radians_t angle =
          mushroom_tau * static_cast<float> (side) / static_cast<float> (sides);
        const float a = radians_value (angle);
        const Vec3 way = east * std::cos (a) + north * std::sin (a);
        const Vec3 n = way * normals[i][0] + up * normals[i][1];
        return length2 (n) > 1e-8f ? normalized (n) : up;
      };
      const auto emit = [&] (std::size_t i, int side) {
        list.color (seared (profile[i].colour, frame.cooked));
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

    const auto circular = [] (std::size_t, radians_t) {
      return 1.0f * scale_factor[one];
    };

    void draw_chanterelle (render::DrawList& list,
                           const MushroomFrame& frame,
                           std::uint32_t seed) {
      // Egg-yolk gold all over: a stem flaring into a shallow funnel
      // with a wavy, rolled rim, the ridges beneath a little paler.
      const DisplayColor gold (0.96f, 0.66f, 0.12f);
      const DisplayColor pale (0.98f, 0.76f, 0.30f);
      const DisplayColor deep (0.90f, 0.56f, 0.08f);
      const ProfilePoint profile[] = {
        { 0.10f, -0.05f, pale }, { 0.10f, 0.25f, pale }, { 0.15f, 0.52f, pale },
        { 0.30f, 0.80f, pale },  { 0.50f, 0.93f, gold }, { 0.54f, 0.88f, deep },
        { 0.50f, 0.97f, gold },  { 0.30f, 0.99f, gold }, { 0.00f, 0.90f, deep },
      };
      const radians_t phase = mushroom_tau * hash_lane (seed, 11);
      // Waves round the rim.
      const float lobes = 4.0f + std::floor (3.0f * hash_lane (seed, 12));
      revolve (list, frame, profile, 14, [&] (std::size_t i, radians_t angle) {
        return (i >= 3 ? 1.0f + 0.13f * std::sin (
                                          radians_value (lobes * angle + phase))
                       : 1.0f) *
               scale_factor[one];
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
      const radians_t phase = mushroom_tau * hash_lane (seed, 11);
      revolve (list, frame, profile, 10, [&] (std::size_t i, radians_t angle) {
        return (i >= 3 ? 1.0f + 0.16f * std::sin (
                                          radians_value (3.0f * angle + phase))
                       : 1.0f) *
               scale_factor[one];
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
      // The flecks are written in the frame's metres.
      const Vec3& base = position_value (frame.base);
      const Vec3& up = direction_value (frame.up);
      const Vec3& east = direction_value (frame.east);
      const Vec3& north = direction_value (frame.north);
      const float height = meters_value (frame.height);
      for (int k = 0; k < warts; ++k) {
        const std::uint32_t wart = lattice_hash (seed, 0x77a7u, k);
        const float angle = radians_value (mushroom_tau * hash_lane (wart, 0));
        // Along the cap from crown to rim, sampled between its profile
        // points.
        const float along = std::sqrt (hash_lane (wart, 1));
        const float r = 0.40f * along;
        const float h = 1.03f - 0.17f * along * along;
        const Vec3 way = east * std::cos (angle) + north * std::sin (angle);
        const Vec3 normal =
          normalized (up * (1.0f - 0.6f * along) + way * (0.9f * along));
        const Vec3 centre = base + way * (r * height) + up * (h * height) +
                            normal * (0.006f * height);
        const Vec3 tangent = normalized (cross (normal, way));
        const Vec3 bitangent = cross (tangent, normal);
        const float size = height * (0.025f + 0.025f * hash_lane (wart, 2));
        list.color (white);
        list.normal (normal);
        constexpr int corners = 5;
        for (int c = 0; c < corners; ++c) {
          const float a0 = radians_value (mushroom_tau * c / corners);
          const float a1 = radians_value (mushroom_tau * (c + 1) / corners);
          list.vertex (centre + normal * (0.3f * size));
          list.vertex (centre + tangent * (size * std::cos (a0)) +
                       bitangent * (size * std::sin (a0)));
          list.vertex (centre + tangent * (size * std::cos (a1)) +
                       bitangent * (size * std::sin (a1)));
        }
      }
    }

    // By MushroomKind. Funnel chanterelles come in crowds, porcini
    // alone or in twos.
    constexpr MushroomKindRow mushroom_kinds[] = {
      { "chanterelle",
        "chanterelles",
        true,
        0.11f * u::m,
        3,
        5.0f,
        draw_chanterelle },
      { "funnel chanterelle",
        "funnel chanterelles",
        true,
        0.09f * u::m,
        6,
        8.0f,
        draw_funnel_chanterelle },
      { "porcini", "porcini", true, 0.17f * u::m, 1, 2.6f, draw_porcini },
      { "fly agaric",
        "fly agarics",
        false,
        0.24f * u::m,
        1,
        3.6f,
        draw_fly_agaric },
    };
    static_assert (std::size (mushroom_kinds) == mushroom_kind_count);

    const MushroomKindRow& row (MushroomKind kind) {
      return mushroom_kinds[static_cast<std::size_t> (kind)];
    }

    // Which kind a tree's group is, by the tree.
    // `draw` falls below each kind's threshold by its chance.
    MushroomKind kind_for (ForestForm form, probability_t draw) {
      constexpr auto p = probability[one];
      if (form == ForestForm::Conifer || form == ForestForm::Pine) {
        if (draw < 0.40f * p)
          return MushroomKind::chanterelle;
        if (draw < 0.70f * p)
          return MushroomKind::funnel_chanterelle;
        return MushroomKind::porcini;
      }
      if (draw < 0.45f * p)
        return MushroomKind::fly_agaric;
      if (draw < 0.80f * p)
        return MushroomKind::porcini;
      return MushroomKind::chanterelle;
    }

    // How many mushrooms a group holds: `draw` is how far toward its most.
    int group_size (MushroomKind kind, proportion_t draw) {
      return row (kind).least +
             static_cast<int> (row (kind).more * draw.numerical_value_in (one));
    }
  }

  const char* mushroom_name (MushroomKind kind) {
    return row (kind).name;
  }

  const char* mushroom_plural (MushroomKind kind) {
    return row (kind).plural;
  }

  bool mushroom_edible (MushroomKind kind) {
    return row (kind).edible;
  }

  MushroomPlan plan_mushrooms (const ForestPlan& forest,
                               const map::SurfaceGeometry& surface,
                               const map::SurfaceReadings& readings,
                               const terrain::WaterSheets& water,
                               std::uint32_t seed,
                               meters_t sea_level,
                               const position_t& trailhead) {
    MOPPE_PROFILE_ZONE ("plan_mushrooms");
    const terrain::SurfaceElevation sea =
      terrain::surface_elevation_point (sea_level);
    const Vec3& extent = extent_value (forest.period);
    const plan_extent_t period =
      plan_extent (extent[0] * u::m, extent[2] * u::m);
    // Every trunk, by the cell it stands in, so no mushroom grows through
    // a neighbouring tree.
    const auto cell_key = [] (meters_t x, meters_t z) {
      const float cell = meters_value (trunk_cell_metres);
      return (static_cast<std::uint64_t> (static_cast<std::uint32_t> (
                static_cast<int> (std::floor (meters_value (x) / cell))))
              << 32) |
             static_cast<std::uint32_t> (
               static_cast<int> (std::floor (meters_value (z) / cell)));
    };
    std::unordered_map<std::uint64_t, std::vector<plan_position_t>> trunks;
    for (const ForestSite& tree : forest.sites) {
      const plan_position_t at = plan (tree.position);
      trunks[cell_key (x_of (at), z_of (at))].push_back (at);
    }
    const auto clear_of_trunks = [&] (const plan_position_t& spot) {
      const meters_t x = x_of (spot);
      const meters_t z = z_of (spot);
      const Vec2& p = plan_value (spot);
      for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
          const auto found = trunks.find (
            cell_key (x + dx * trunk_cell_metres, z + dz * trunk_cell_metres));
          if (found == trunks.end ())
            continue;
          for (const plan_position_t& trunk : found->second) {
            const Vec2& t = plan_value (trunk);
            if (std::hypot (t.x () - p.x (), t.z () - p.z ()) * u::m <
                trunk_clearance)
              return false;
          }
        }
      return true;
    };

    MushroomPlan plan;
    plan.period = forest.period;
    for (const ForestSite& tree : forest.sites) {
      if (tree.age == ForestAge::Sapling)
        continue;
      const std::uint32_t identity = lattice_hash (tree.seed, 0x6d75u, seed);
      const plan_position_t at = moppe::plan (tree.position);
      const plan_displacement_t off = at - moppe::plan (trailhead);
      const meters_t from_trailhead =
        std::hypot (plan_value (off).x (), plan_value (off).z ()) * u::m;
      const proportion_t far_from_trailhead =
        1.0f * proportion[one] -
        band (0.4f * trailhead_reach, trailhead_reach, from_trailhead);
      const probability_t odds = quantity_cast<probability> (
        host_odds *
        (1.0f * scale_factor[one] +
         quantity_cast<scale_factor> (
           (trailhead_bonus - 1.0f * scale_factor[one]) * far_from_trailhead)));
      if (hash_lane (identity, 0) * probability[one] >= odds)
        continue;

      const MushroomKind kind =
        kind_for (tree.form, hash_lane (identity, 1) * probability[one]);
      const int members =
        group_size (kind, hash_lane (identity, 2) * proportion[one]);
      // An arc around the trunk, as the fungus spreads out under the
      // tree's crown.
      const radians_t middle = mushroom_tau * hash_lane (identity, 3);
      const radians_t spread = (0.6f + 1.6f * hash_lane (identity, 4)) * u::rad;
      const meters_t distance = (1.3f + 2.2f * hash_lane (identity, 5)) * u::m;
      const scale_factor_t group_scale =
        (0.8f + 0.4f * hash_lane (identity, 6)) * scale_factor[one];
      for (int k = 0; k < members; ++k) {
        const std::uint32_t one_seed = lattice_hash (
          identity, static_cast<std::uint32_t> (k + 1), seed ^ 0x5b0e7u);
        // From one end of the arc, -0.5, to the other, 0.5.
        const float place =
          members > 1 ? static_cast<float> (k) / (members - 1) - 0.5f : 0.0f;
        const float angle =
          radians_value (middle + spread * place +
                         0.25f * (hash_lane (one_seed, 0) - 0.5f) * u::rad);
        const meters_t reach =
          distance + 0.7f * (hash_lane (one_seed, 1) - 0.5f) * u::m;
        const plan_position_t spot =
          terrain::wrap (at + plan_displacement (reach * std::cos (angle),
                                                 reach * std::sin (angle)),
                         period);
        if (!clear_of_trunks (spot))
          continue;
        const position_t where = at_height (spot, 0.0f * u::m);
        const terrain::SurfaceElevation ground =
          spatial::sample<terrain::surface_elevation> (surface, where);
        const terrain::SurfaceElevation level =
          spatial::sample<terrain::surface_elevation> (water, where);
        if (ground < sea || level > ground ||
            spatial::sample<map::trail_influence> (readings, where) >
              mushroom_trail_tread ||
            spatial::sample<map::home_base_influence> (readings, where) >
              mushroom_home_base_clearing)
          continue;
        MushroomSite site;
        site.kind = kind;
        site.seed = one_seed;
        site.scale = group_scale * (0.7f + 0.55f * hash_lane (one_seed, 2));
        site.yaw =
          quantity_cast<heading> (mushroom_tau * hash_lane (one_seed, 3));
        site.lean = 0.18f * hash_lane (one_seed, 4) * inclination[u::rad];
        site.lean_toward =
          quantity_cast<heading> (mushroom_tau * hash_lane (one_seed, 5));
        site.base = terrain::at_elevation (spot, ground - 0.01f * u::m);
        plan.sites.push_back (site);
      }
    }
    std::cerr << "moppe: mushrooms: " << plan.sites.size () << " growing"
              << std::endl;
    return plan;
  }

  void draw_mushroom (render::DrawList& list, const MushroomSite& site) {
    row (site.kind).draw (list, mushroom_frame (site), site.seed);
  }

  std::uint64_t MushroomPatch::tile_key (int x, int z) const {
    // Tiles are numbered round the torus, so those past the seam are the
    // ones at its other edge.
    if (m_tiles_x > 0)
      x = terrain::wrap_index (x, m_tiles_x);
    if (m_tiles_z > 0)
      z = terrain::wrap_index (z, m_tiles_z);
    return (static_cast<std::uint64_t> (static_cast<std::uint32_t> (x)) << 32) |
           static_cast<std::uint32_t> (z);
  }

  int MushroomPatch::tile_x (meters_t x) const {
    return static_cast<int> (
      std::floor (meters_value (x) / meters_value (m_tile_x)));
  }

  int MushroomPatch::tile_z (meters_t z) const {
    return static_cast<int> (
      std::floor (meters_value (z) / meters_value (m_tile_z)));
  }

  MushroomPatch::TileMesh* MushroomPatch::mesh_at (const position_t& at) {
    const plan_position_t spot = plan (at);
    const auto found =
      m_meshes.find (tile_key (tile_x (x_of (spot)), tile_z (z_of (spot))));
    return found == m_meshes.end () ? nullptr : &found->second;
  }

  position_t MushroomPatch::base_near (std::uint32_t index,
                                       const position_t& reference) const {
    position_t near = m_plan.sites[index].base;
    Vec3& base = position_value (near);
    const Vec3& to = position_value (reference);
    const Vec3& period = extent_value (m_plan.period);
    base[0] += terrain::whole_periods (to[0] - base[0], period[0]);
    base[2] += terrain::whole_periods (to[2] - base[2], period[2]);
    return near;
  }

  void MushroomPatch::rebuild (MushroomPlan plan) {
    m_plan = std::move (plan);
    m_picked.assign (m_plan.sites.size (), false);
    m_followed.clear ();
    m_meshes.clear ();
    m_tiles.clear ();
    // Tiles of about `tile_metres`, a whole number of them to the world.
    const auto fit = [] (meters_t period, meters_t& size, int& count) {
      count =
        period > 0.0f * u::m
          ? std::max (1,
                      static_cast<int> (std::ceil (meters_value (period) /
                                                   meters_value (tile_metres))))
          : 0;
      size = count > 0 ? period / static_cast<float> (count) : tile_metres;
    };
    const Vec3& period = extent_value (m_plan.period);
    fit (period[0] * u::m, m_tile_x, m_tiles_x);
    fit (period[2] * u::m, m_tile_z, m_tiles_z);
    for (std::uint32_t i = 0; i < m_plan.sites.size (); ++i) {
      const plan_position_t at = moppe::plan (m_plan.sites[i].base);
      m_tiles[tile_key (tile_x (x_of (at)), tile_z (z_of (at)))].push_back (i);
    }
  }

  std::optional<std::uint32_t>
  MushroomPatch::within_reach (const position_t& feet,
                               const direction_t& heading) const {
    Vec3 facing = direction_value (heading);
    facing[1] = 0.0f;
    facing = length2 (facing) > 1e-6f ? normalized (facing) : Vec3 (0, 0, 1);
    const plan_position_t stand = plan (feet);
    const int tx = tile_x (x_of (stand));
    const int tz = tile_z (z_of (stand));
    std::optional<std::uint32_t> best;
    meters_t best_score = 1e9f * u::m;
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        const auto found = m_tiles.find (tile_key (tx + dx, tz + dz));
        if (found == m_tiles.end ())
          continue;
        for (const std::uint32_t index : found->second) {
          if (m_picked[index])
            continue;
          const Vec3 offset =
            position_value (base_near (index, feet)) - position_value (feet);
          if (std::abs (offset[1]) > 1.5f)
            continue;
          const meters_t distance = std::hypot (offset[0], offset[2]) * u::m;
          if (distance > pick_reach)
            continue;
          // The cosine between the facing and the way to it.
          const float ahead =
            distance > 1e-3f * u::m
              ? (offset[0] * facing[0] + offset[2] * facing[2]) /
                  meters_value (distance)
              : 1.0f;
          // Close at hand, or in front of the walker.
          if (ahead < -0.2f && distance > 0.8f * u::m)
            continue;
          const meters_t score = distance - 0.8f * ahead * u::m;
          if (score < best_score) {
            best_score = score;
            best = index;
          }
        }
      }
    return best;
  }

  std::optional<std::uint32_t>
  MushroomPatch::nearest (const position_t& at,
                          meters_t within,
                          std::span<const std::uint32_t> skip) const {
    const plan_position_t from = plan (at);
    const int tx = tile_x (x_of (from));
    const int tz = tile_z (z_of (from));
    const int span_x = static_cast<int> (
      std::ceil (meters_value (within) / meters_value (m_tile_x)));
    const int span_z = static_cast<int> (
      std::ceil (meters_value (within) / meters_value (m_tile_z)));
    std::optional<std::uint32_t> best;
    meters_t best_distance = within;
    for (int dz = -span_z; dz <= span_z; ++dz)
      for (int dx = -span_x; dx <= span_x; ++dx) {
        const auto found = m_tiles.find (tile_key (tx + dx, tz + dz));
        if (found == m_tiles.end ())
          continue;
        for (const std::uint32_t index : found->second) {
          if (m_picked[index] ||
              std::find (skip.begin (), skip.end (), index) != skip.end ())
            continue;
          const plan_displacement_t off = plan (base_near (index, at)) - from;
          const meters_t distance =
            std::hypot (plan_value (off).x (), plan_value (off).z ()) * u::m;
          if (distance < best_distance) {
            best_distance = distance;
            best = index;
          }
        }
      }
    return best;
  }

  std::vector<std::uint32_t> MushroomPatch::within (const position_t& at,
                                                    meters_t within) const {
    const plan_position_t from = plan (at);
    const int tx = tile_x (x_of (from));
    const int tz = tile_z (z_of (from));
    const int span_x = static_cast<int> (
      std::ceil (meters_value (within) / meters_value (m_tile_x)));
    const int span_z = static_cast<int> (
      std::ceil (meters_value (within) / meters_value (m_tile_z)));
    std::vector<std::uint32_t> found;
    for (int dz = -span_z; dz <= span_z; ++dz)
      for (int dx = -span_x; dx <= span_x; ++dx) {
        const auto tile = m_tiles.find (tile_key (tx + dx, tz + dz));
        if (tile == m_tiles.end ())
          continue;
        for (const std::uint32_t index : tile->second) {
          const plan_displacement_t off = plan (base_near (index, at)) - from;
          if (!m_picked[index] &&
              std::hypot (plan_value (off).x (), plan_value (off).z ()) *
                  u::m <=
                within)
            found.push_back (index);
        }
      }
    return found;
  }

  void MushroomPatch::take (std::uint32_t index) {
    if (index >= m_picked.size () || m_picked[index])
      return;
    m_picked[index] = true;
    if (TileMesh* tile = mesh_at (m_plan.sites[index].base))
      tile->dirty = true;
  }

  void MushroomPatch::follow (const Basket& basket) {
    const std::vector<std::uint32_t>& picked = basket.picked;
    if (picked.size () < m_followed.size () ||
        !std::equal (m_followed.begin (), m_followed.end (), picked.begin ())) {
      // Another basket came back, as when a checkpoint is restored:
      // everything grows again first.
      for (std::uint32_t i = 0; i < m_picked.size (); ++i)
        if (m_picked[i]) {
          m_picked[i] = false;
          if (TileMesh* tile = mesh_at (m_plan.sites[i].base))
            tile->dirty = true;
        }
      m_followed.clear ();
    }
    for (std::size_t i = m_followed.size (); i < picked.size (); ++i) {
      const std::uint32_t index = picked[i];
      m_followed.push_back (index);
      if (index >= m_picked.size ())
        continue;
      m_picked[index] = true;
      if (TileMesh* tile = mesh_at (m_plan.sites[index].base))
        tile->dirty = true;
    }
  }

  void MushroomPatch::draw (render::Renderer& renderer,
                            const position_t& camera) {
    if (m_tiles.empty ())
      return;
    const plan_position_t eye = plan (camera);
    const int tx = tile_x (x_of (eye));
    const int tz = tile_z (z_of (eye));
    const int span_x = static_cast<int> (
      std::ceil (meters_value (draw_reach) / meters_value (m_tile_x)));
    const int span_z = static_cast<int> (
      std::ceil (meters_value (draw_reach) / meters_value (m_tile_z)));
    const meters_t period_x = extent_value (m_plan.period)[0] * u::m;
    const meters_t period_z = extent_value (m_plan.period)[2] * u::m;
    // How far a tile, by the shortest way round the world, is from the
    // camera.
    const auto tile_distance = [&] (int x, int z) {
      const meters_t cx = (static_cast<float> (x) + 0.5f) * m_tile_x;
      const meters_t cz = (static_cast<float> (z) + 0.5f) * m_tile_z;
      return std::hypot (meters_value (terrain::minimum_image_delta (
                           cx - x_of (eye), period_x)),
                         meters_value (terrain::minimum_image_delta (
                           cz - z_of (eye), period_z))) *
               u::m -
             0.7072f * std::max (m_tile_x, m_tile_z);
    };

    // Let go of meshes long out of sight, visiting only cached tiles.
    std::erase_if (m_meshes, [&] (const auto& entry) {
      const std::uint64_t key = entry.first;
      const int x = static_cast<int> (static_cast<std::uint32_t> (key >> 32));
      const int z = static_cast<int> (static_cast<std::uint32_t> (key));
      return tile_distance (x, z) > keep_reach;
    });

    render::DrawList list;
    for (int dz = -span_z; dz <= span_z; ++dz)
      for (int dx = -span_x; dx <= span_x; ++dx) {
        if (tile_distance (tx + dx, tz + dz) > draw_reach)
          continue;
        const auto found = m_tiles.find (tile_key (tx + dx, tz + dz));
        if (found == m_tiles.end ())
          continue;
        TileMesh& tile = m_meshes[found->first];
        if (tile.dirty) {
          list.clear ();
          render::DrawState solid;
          solid.cull = false;
          list.state (solid);
          list.lit (true);
          list.fogged (true);
          list.begin (render::Prim::Triangles);
          for (const std::uint32_t index : found->second)
            if (!m_picked[index])
              draw_mushroom (list, m_plan.sites[index]);
          list.end ();
          tile.mesh = list.empty () ? nullptr : renderer.create_mesh (list);
          tile.dirty = false;
        }
        if (!tile.mesh)
          continue;
        // The tile is built where it lies in the world; past the seam it
        // stands a whole number of laps along.
        const meters_t lap_x =
          m_tiles_x > 0
            ? static_cast<float> (
                (tx + dx - terrain::wrap_index (tx + dx, m_tiles_x)) /
                m_tiles_x) *
                period_x
            : 0.0f * u::m;
        const meters_t lap_z =
          m_tiles_z > 0
            ? static_cast<float> (
                (tz + dz - terrain::wrap_index (tz + dz, m_tiles_z)) /
                m_tiles_z) *
                period_z
            : 0.0f * u::m;
        renderer.draw_mesh (
          *tile.mesh,
          Mat4::translation (
            Vec3 (meters_value (lap_x), 0.0f, meters_value (lap_z))));
      }
  }
}
