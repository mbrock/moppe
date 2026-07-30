#include <moppe/game/forest.hh>
#include <moppe/gfx/signal.hh>

#include <moppe/profile.hh>
#include <moppe/render/draw.hh>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace moppe::game {
  namespace {
    using namespace mp_units;

    constexpr int forest_chunks_per_side = 16;
    // Where crown geometry hands over to the terrain's filtered canopy. The
    // outer figure is not a draw-distance budget: past it an individual tree
    // is smaller than the pixel it lands in, so drawing one can only add
    // noise to a mass the ground material already carries.
    constexpr meters_t forest_geometry_reach = 950.0f * u::m;
    // Where an organism becomes a volume. Close enough for a stem and limbs
    // to be worth their triangles is a much shorter distance than it looks,
    // and keeping the band short is exactly what pays for the detail inside
    // it: the area to cover falls with the square.
    constexpr meters_t forest_detail_reach = 260.0f * u::m;

    position_t sample_position (meters_t x, meters_t z) {
      return position (
        Vec3 (x.numerical_value_in (u::m), 0, z.numerical_value_in (u::m)));
    }

    terrain::SurfaceElevation
    elevation_at (const map::SurfaceGeometry& surface, meters_t x, meters_t z) {
      return spatial::sample<terrain::surface_elevation> (
        surface, sample_position (x, z));
    }

    terrain::TerrainNormal
    normal_at (const map::SurfaceGeometry& surface, meters_t x, meters_t z) {
      return spatial::sample<terrain::terrain_normal> (surface,
                                                       sample_position (x, z));
    }

    map::ForestCover
    cover_at (const map::SurfaceReadings& readings, meters_t x, meters_t z) {
      return spatial::sample<map::forest_cover> (readings,
                                                 sample_position (x, z));
    }

    map::SurfaceMoisture
    moisture_at (const map::SurfaceReadings& readings, meters_t x, meters_t z) {
      return spatial::sample<map::surface_moisture> (readings,
                                                     sample_position (x, z));
    }

    position_t forest_position (meters_t x,
                                terrain::SurfaceElevation elevation,
                                meters_t z) {
      return position (
        Vec3 (x.numerical_value_in (u::m),
              elevation.quantity_from_zero ().numerical_value_in (u::m),
              z.numerical_value_in (u::m)));
    }

    meters_t position_component (position_t value, std::size_t component) {
      return position_value (value)[component] * u::m;
    }

    meters_t extent_component (const spatial_extent_t& value,
                               std::size_t component) {
      return extent_value (value)[component] * u::m;
    }

    bool visible_in_view (const displacement_t& delta,
                          meters_t radius,
                          const ForestView& view) {
      const Vec3& offset = displacement_value (delta);
      const meters_t forward = dot (offset, view.forward) * u::m;
      if (forward < -radius)
        return false;

      const float vertical_tangent =
        moppe::tan (view.vertical_field_of_view / 2.0f);
      const float horizontal_tangent =
        vertical_tangent * scalar_value (view.aspect_ratio);
      const meters_t side = std::abs (dot (offset, view.right)) * u::m;
      const meters_t height = std::abs (dot (offset, view.up)) * u::m;
      const float side_plane_length =
        std::sqrt (1.0f + horizontal_tangent * horizontal_tangent);
      const float top_plane_length =
        std::sqrt (1.0f + vertical_tangent * vertical_tangent);
      return side <=
               forward * horizontal_tangent + radius * side_plane_length &&
             height <= forward * vertical_tangent + radius * top_plane_length;
    }

    bool periodic_world (const spatial_extent_t& period) {
      return extent_component (period, 0) > 0.0f * u::m &&
             extent_component (period, 2) > 0.0f * u::m;
    }

    // The world is a torus, so one baked chunk can be in view several times
    // over, at different offsets. This walks the images of a chunk that fall
    // within reach of the camera and hands each one its world offset and how
    // far away it is. Residency and drawing ask the same question of the same
    // lattice, and asking it in one place is what stops them disagreeing
    // about which chunks are near.
    template <typename Visit>
    void visit_chunk_images (const position_t& center,
                             const spatial_extent_t& period,
                             const ForestView& view,
                             meters_t reach,
                             Visit&& visit) {
      const auto tile_range = [&] (std::size_t axis) {
        const meters_t span = extent_component (period, axis);
        const meters_t from = position_component (view.position, axis) - reach -
                              position_component (center, axis);
        const meters_t to = position_component (view.position, axis) + reach -
                            position_component (center, axis);
        return std::pair {
          static_cast<int> (std::ceil ((from / span).numerical_value_in (one))),
          static_cast<int> (std::floor ((to / span).numerical_value_in (one)))
        };
      };
      const meters_t period_x = extent_component (period, 0);
      const meters_t period_z = extent_component (period, 2);
      const auto [minimum_x, maximum_x] = tile_range (0);
      const auto [minimum_z, maximum_z] = tile_range (2);
      for (int tile_z = minimum_z; tile_z <= maximum_z; ++tile_z)
        for (int tile_x = minimum_x; tile_x <= maximum_x; ++tile_x) {
          const displacement_t offset =
            displacement (Vec3 ((tile_x * period_x).numerical_value_in (u::m),
                                0,
                                (tile_z * period_z).numerical_value_in (u::m)));
          const displacement_t delta = center + offset - view.position;
          visit (offset, mp_units::sqrt (dot (delta, delta)));
        }
    }

    // ---- Individual trees ------------------------------------------
    //
    // Every tree in the world population is built from the same handful of
    // triangles it always was. What changed is what those triangles say: the
    // crown lights as a volume rather than as facets, and it carries the
    // stand's hydrology, the species, and the individual in its colour ramp.

    // The frame a tree stands in. It grows mostly along its own vertical and
    // only partly along the slope it roots into, which is why a hillside
    // reads as a hillside rather than as combed fur.
    struct TreeAxes {
      Vec3 up;
      Vec3 across;
      Vec3 forward;
    };

    TreeAxes tree_axes (const ForestSite& site, float ground_share) {
      const Vec3 surface_normal = site.normal.numerical_value_in (one);
      const Vec3 up = normalized (Vec3 (0, 1, 0) * (1.0f - ground_share) +
                                  surface_normal * ground_share);
      const float heading = PI2 * hash_lane (site.seed, 3);
      Vec3 forward (std::sin (heading), 0.0f, std::cos (heading));
      forward = normalized (forward - up * dot (forward, up));
      return { up, normalized (cross (up, forward)), forward };
    }

    // One tree resolved once: where it stands, how big it is, which way it
    // leans away from its own axis, and the two colour ramps its surfaces
    // interpolate along.
    struct TreeForm {
      TreeAxes axes;
      Vec3 root;
      meters_t height;
      meters_t crown_radius;
      Vec3 lean;
      FoliagePalette crown;
      FoliagePalette bark;
      std::uint32_t seed;
    };

    TreeForm resolve_tree (const ForestSite& site, float ground_share) {
      const TreeAxes axes = tree_axes (site, ground_share);
      const float cover = site.cover.numerical_value_in (one);
      const float moisture = site.moisture.numerical_value_in (one);
      const float size = site.size.numerical_value_in (one);
      const bool conifer = site.form == ForestForm::conifer;
      // Conifers hold a narrow column; broadleaves spread. Moisture buys
      // height in both, which is what makes a valley floor tower over the
      // same species on the ridge above it.
      const meters_t height = size * (conifer ? 12.2f : 10.8f) *
                              (0.82f + 0.30f * cover + 0.26f * moisture) * u::m;
      const meters_t radius = (conifer ? 0.205f : 0.250f) * height;
      const Vec3 lean = (axes.across * (hash_lane (site.seed, 51) - 0.5f) +
                         axes.forward * (hash_lane (site.seed, 52) - 0.5f)) *
                        (0.42f * radius.numerical_value_in (u::m));
      return { .axes = axes,
               .root = position_value (site.position),
               .height = height,
               .crown_radius = radius,
               .lean = lean,
               .crown = crown_palette (site.form,
                                       { .moisture = moisture, .cover = cover },
                                       site.seed),
               .bark = bark_palette (site.form, site.seed),
               .seed = site.seed };
    }

    // How far up its own tree a point sits, as a fraction of tree height.
    // Every wind weight and every exposure in this file is a function of it,
    // which is the reason a crown never has to be told where its top is.
    float rise_of (const TreeForm& tree, const Vec3& point) {
      return dot (point - tree.root, tree.axes.up) /
             tree.height.numerical_value_in (u::m);
    }

    // A length of wood: a tapered tube whose vertices carry radial normals,
    // so five sides read as round rather than as a prism, and whose colour
    // darkens toward the ground the way a stem in its own litter does.
    void append_wood (render::DrawList& draw,
                      const TreeForm& tree,
                      const Vec3& from,
                      const Vec3& to,
                      float from_radius,
                      float to_radius,
                      int sides) {
      const Vec3 axis = normalized (to - from);
      const Vec3 anchor = std::abs (dot (axis, tree.axes.up)) > 0.94f
                            ? tree.axes.forward
                            : tree.axes.up;
      const Vec3 across = normalized (cross (axis, anchor));
      const Vec3 around = normalized (cross (axis, across));
      const float from_rise = rise_of (tree, from);
      const float to_rise = rise_of (tree, to);
      const auto ring = [&] (const Vec3& centre,
                             const Vec3& outward,
                             float radius,
                             float rise) {
        return FoliageVertex { centre + outward * radius,
                               outward,
                               std::clamp (0.26f + 0.90f * rise, 0.0f, 1.0f),
                               std::clamp (0.14f + 0.86f * rise, 0.0f, 1.0f) *
                                 proportion[one],
                               // Wood leans with the gust; it does not shake.
                               0.0f * proportion[one] };
      };
      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const float a0 = PI2 * side / sides;
        const float a1 = PI2 * (side + 1) / sides;
        const Vec3 n0 = across * std::cos (a0) + around * std::sin (a0);
        const Vec3 n1 = across * std::cos (a1) + around * std::sin (a1);
        foliage_quad (draw,
                      tree.bark,
                      ring (from, n0, from_radius, from_rise),
                      ring (from, n1, from_radius, from_rise),
                      ring (to, n1, to_radius, to_rise),
                      ring (to, n0, to_radius, to_rise));
      }
      draw.end ();
    }

    // The stations a stem passes on its way up. The flare belongs in the
    // first tenth, where the trunk is really the top of its own root plate;
    // spread it any further and the tree becomes a tent peg. Above that it is
    // a long taper with a bend in it, and the bend accumulates with height,
    // because that is where a stem has had room to wander.
    void append_stem (render::DrawList& draw,
                      const TreeForm& tree,
                      float top_rise,
                      float radius_share,
                      int sides) {
      constexpr int stations = 4;
      constexpr float station_rise[stations] = { 0.0f, 0.10f, 0.55f, 1.0f };
      constexpr float station_girth[stations] = { 1.85f, 1.0f, 0.70f, 0.42f };
      const float height_m = tree.height.numerical_value_in (u::m);
      const float radius_m = radius_share * height_m;
      const auto station = [&] (int index) {
        const float rise = station_rise[index];
        return tree.root + tree.axes.up * (rise * top_rise * height_m) +
               tree.lean * (0.40f * rise * rise);
      };
      for (int index = 0; index + 1 < stations; ++index)
        append_wood (draw,
                     tree,
                     station (index),
                     station (index + 1),
                     radius_m * station_girth[index],
                     radius_m * station_girth[index + 1],
                     sides);
    }

    // One rounded mass of leaves: a ring of irregular skirts between a stem
    // attachment below and a tip above. Exposure follows height inside the
    // mass, because that is what sky occlusion is, and the outward normal
    // comes from the mass's own centre, which is what turns a handful of flat
    // panels into something with a shaded underside.
    void append_crown_lobe (render::DrawList& draw,
                            const TreeForm& tree,
                            const Vec3& base,
                            const Vec3& tip,
                            meters_t radius,
                            int sides,
                            std::uint32_t lane,
                            float shelter) {
      constexpr int most_sides = 6;
      const float radius_m = radius.numerical_value_in (u::m);
      const Vec3 axis = tip - base;
      const Vec3 centre = base + axis * 0.55f;
      const float base_rise = rise_of (tree, base);
      const float span = std::max (0.08f, rise_of (tree, tip) - base_rise);
      const float turn = PI2 * hash_lane (tree.seed, lane);
      const auto leaf = [&] (const Vec3& point, float flutter, float grain) {
        const float rise = rise_of (tree, point);
        return FoliageVertex {
          point,
          normalized (point - centre),
          shelter * (0.06f + 1.06f * (rise - base_rise) / span) + grain,
          std::clamp (0.20f + 0.86f * rise, 0.0f, 1.0f) * proportion[one],
          flutter * proportion[one]
        };
      };

      Vec3 lower[most_sides];
      Vec3 upper[most_sides];
      float grain[most_sides];
      for (int side = 0; side < sides; ++side) {
        const float angle = turn + PI2 * side / sides;
        const Vec3 radial = tree.axes.across * std::cos (angle) +
                            tree.axes.forward * std::sin (angle);
        // Both rings wander in radius and in height. A crown whose outline is
        // a regular polygon reads as a lampshade from any distance at which
        // its silhouette is resolvable at all.
        const float wide = 0.78f + 0.46f * hash_lane (tree.seed, lane + side);
        const float narrow =
          0.62f + 0.30f * hash_lane (tree.seed, lane + 8 + side);
        lower[side] =
          base +
          axis * (0.28f + 0.18f * hash_lane (tree.seed, lane + 16 + side)) +
          radial * (radius_m * wide);
        upper[side] =
          base +
          axis * (0.74f + 0.16f * hash_lane (tree.seed, lane + 24 + side)) +
          radial * (radius_m * narrow);
        grain[side] = 0.17f * (hash_lane (tree.seed, lane + 32 + side) - 0.5f);
      }

      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const int next = (side + 1) % sides;
        const FoliageVertex stem_foot = leaf (base, 0.10f, -0.06f);
        const FoliageVertex crown_tip = leaf (tip, 0.58f, 0.10f);
        const FoliageVertex low_a = leaf (lower[side], 0.94f, grain[side]);
        const FoliageVertex low_b = leaf (lower[next], 0.94f, grain[next]);
        const FoliageVertex high_a = leaf (upper[side], 1.0f, grain[side]);
        const FoliageVertex high_b = leaf (upper[next], 1.0f, grain[next]);
        foliage_triangle (draw, tree.crown, stem_foot, low_b, low_a);
        foliage_quad (draw, tree.crown, low_a, low_b, high_b, high_a);
        foliage_triangle (draw, tree.crown, crown_tip, high_a, high_b);
      }
      draw.end ();
    }

    // Near enough to ride past, a tree stops being a mass and becomes an
    // organism. The stem bends and you can see limbs leave it; the crown is
    // two or three separate masses with sky between them rather than one
    // blob. None of that survives two hundred metres, which is exactly why
    // it is affordable: the band that gets it is small.
    void append_near_broadleaf (render::DrawList& draw, const TreeForm& tree) {
      const float height_m = tree.height.numerical_value_in (u::m);
      const float radius_m = tree.crown_radius.numerical_value_in (u::m);
      const Vec3& up = tree.axes.up;
      append_stem (draw, tree, 0.58f, 0.019f, 5);
      append_crown_lobe (draw,
                         tree,
                         tree.root + up * (0.44f * height_m),
                         tree.root + up * height_m + tree.lean,
                         tree.crown_radius * 0.94f,
                         6,
                         29,
                         1.0f);
      for (int limb = 0; limb < 2; ++limb) {
        const float turn = PI2 * hash_lane (tree.seed, 91 + limb);
        const Vec3 outward = tree.axes.across * std::cos (turn) +
                             tree.axes.forward * std::sin (turn);
        const Vec3 fork =
          tree.root +
          up * ((0.46f + 0.12f * static_cast<float> (limb)) * height_m);
        const float reach =
          radius_m * (0.46f + 0.30f * hash_lane (tree.seed, 95 + limb));
        const Vec3 shoulder =
          fork + outward * (reach * 0.66f) + up * (0.12f * height_m);
        const Vec3 crest = fork + outward * reach + up * (0.32f * height_m);
        append_wood (
          draw, tree, fork, shoulder, 0.015f * height_m, 0.008f * height_m, 3);
        append_crown_lobe (draw,
                           tree,
                           shoulder,
                           crest,
                           tree.crown_radius * 0.44f,
                           4,
                           101 + 48 * static_cast<std::uint32_t> (limb),
                           0.84f);
      }
    }

    // A spruce is a stack of drooping whorls under a leader, not a cone. The
    // skirts overlap, each turns on its own, and each hangs below where it
    // leaves the stem, so the silhouette breaks into steps instead of being
    // one clean triangle repeated across a hillside.
    void append_conifer_crown (render::DrawList& draw,
                               const TreeForm& tree,
                               int tiers,
                               int sides) {
      const float height_m = tree.height.numerical_value_in (u::m);
      const float radius_m = tree.crown_radius.numerical_value_in (u::m);
      const auto needle = [&] (const Vec3& point,
                               const Vec3& outward,
                               float flutter,
                               float grain) {
        const float rise = rise_of (tree, point);
        return FoliageVertex {
          point,
          outward,
          std::clamp (0.10f + 0.98f * rise + grain, 0.0f, 1.0f),
          std::clamp (0.18f + 0.90f * rise, 0.0f, 1.0f) * proportion[one],
          flutter * proportion[one]
        };
      };

      draw.begin (render::Prim::Triangles);
      for (int tier = 0; tier < tiers; ++tier) {
        const float share = static_cast<float> (tier) / (tiers - 1);
        const Vec3 drift = tree.lean * share;
        const float wobble = 0.86f + 0.28f * hash_lane (tree.seed, 61 + tier);
        const float foot_rise = 0.18f + 0.58f * share;
        const Vec3 foot =
          tree.root + tree.axes.up * (foot_rise * height_m) + drift;
        const Vec3 crest =
          tree.root +
          tree.axes.up * ((foot_rise + (0.40f - 0.16f * share)) * height_m) +
          drift;
        const float tier_radius = radius_m * (1.0f - 0.70f * share) * wobble;
        const float droop = 0.05f * height_m * (1.0f - share);
        const float turn = PI2 * hash_lane (tree.seed, 71 + tier);
        for (int side = 0; side < sides; ++side) {
          const float a0 = turn + PI2 * side / sides;
          const float a1 = turn + PI2 * (side + 1) / sides;
          const Vec3 r0 = tree.axes.across * std::cos (a0) +
                          tree.axes.forward * std::sin (a0);
          const Vec3 r1 = tree.axes.across * std::cos (a1) +
                          tree.axes.forward * std::sin (a1);
          const auto stretch = [&] (int index) {
            return tier_radius *
                   (0.80f + 0.38f * hash_lane (tree.seed,
                                               81 + 8 * tier + index % sides));
          };
          // A whorl hangs below the stem it leaves, so the outward direction
          // of the foliage volume points out and up, never straight out.
          const Vec3 skirt0 = foot + r0 * stretch (side) - tree.axes.up * droop;
          const Vec3 skirt1 =
            foot + r1 * stretch (side + 1) - tree.axes.up * droop;
          const Vec3 out0 = normalized (r0 * 0.78f + tree.axes.up * 0.62f);
          const Vec3 out1 = normalized (r1 * 0.78f + tree.axes.up * 0.62f);
          foliage_triangle (draw,
                            tree.crown,
                            needle (crest, tree.axes.up, 0.42f, 0.06f),
                            needle (skirt0, out0, 0.88f, -0.10f),
                            needle (skirt1, out1, 0.88f, -0.10f));
        }
      }
      draw.end ();
    }

    void append_tree (render::DrawList& draw, const ForestSite& site) {
      const TreeForm tree = resolve_tree (site, 0.20f);
      if (site.form == ForestForm::conifer) {
        append_stem (draw, tree, 0.94f, 0.016f, 5);
        append_conifer_crown (draw, tree, 6, 5);
      } else {
        append_near_broadleaf (draw, tree);
      }
    }

    // Beyond the near band a tree is a few pixels of canopy, and the only
    // thing worth spending them on is its mass. What distinguishes one facet
    // from the next arrives at this range as sparkle and nothing else, so the
    // outward normals lift toward the sky, where the light comes from, and
    // the whole crown lights as one soft body. Its colour needs no separate
    // treatment: the scene shader is already converging foliage on the
    // canopy tone as distance grows.
    void append_distant_tree (render::DrawList& draw, const ForestSite& site) {
      const TreeForm tree = resolve_tree (site, 0.14f);
      constexpr int sides = 4;
      const bool conifer = site.form == ForestForm::conifer;
      const float height_m = tree.height.numerical_value_in (u::m);
      const float radius_m = tree.crown_radius.numerical_value_in (u::m);
      const Vec3& up = tree.axes.up;
      const Vec3 foot = tree.root + up * ((conifer ? 0.10f : 0.30f) * height_m);
      const Vec3 waist =
        tree.root + up * ((conifer ? 0.26f : 0.62f) * height_m);
      const Vec3 crest = tree.root + up * height_m + tree.lean * 0.5f;
      const auto mass = [&] (const Vec3& point, const Vec3& outward) {
        const float rise = rise_of (tree, point);
        return FoliageVertex {
          point,
          normalized (outward * 0.86f + up * 0.52f),
          std::clamp (0.22f + 0.62f * rise, 0.0f, 1.0f),
          std::clamp (0.20f + 0.80f * rise, 0.0f, 1.0f) * proportion[one],
          (rise > 0.34f ? 0.62f : 0.0f) * proportion[one]
        };
      };

      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const float a0 = PI2 * side / sides;
        const float a1 = PI2 * (side + 1) / sides;
        const Vec3 r0 =
          tree.axes.across * std::cos (a0) + tree.axes.forward * std::sin (a0);
        const Vec3 r1 =
          tree.axes.across * std::cos (a1) + tree.axes.forward * std::sin (a1);
        const Vec3 p0 = waist + r0 * radius_m;
        const Vec3 p1 = waist + r1 * radius_m;
        if (!conifer)
          foliage_triangle (
            draw, tree.crown, mass (foot, -up), mass (p1, r1), mass (p0, r0));
        foliage_triangle (
          draw, tree.crown, mass (crest, up), mass (p0, r0), mass (p1, r1));
      }
      draw.end ();
    }
  }

  ForestPlan plan_global_forest (const map::SurfaceGeometry& surface,
                                 const map::SurfaceReadings& readings,
                                 std::uint32_t seed,
                                 meters_t spacing) {
    if (spacing <= 0.0f * u::m)
      throw std::invalid_argument ("Forest spacing must be positive");
    const terrain::TerrainDomain& domain = surface.domain ();
    ForestPlan plan;
    const meters_t width = domain.period_x ();
    const meters_t depth = domain.period_z ();
    plan.period = spatial_extent_in_metres (Vec3 (
      width.numerical_value_in (u::m), 0, depth.numerical_value_in (u::m)));
    const std::uint32_t columns =
      std::max (1U,
                static_cast<std::uint32_t> (
                  std::ceil ((width / spacing).numerical_value_in (one))));
    const std::uint32_t rows =
      std::max (1U,
                static_cast<std::uint32_t> (
                  std::ceil ((depth / spacing).numerical_value_in (one))));
    const meters_t cell_x = width / static_cast<float> (columns);
    const meters_t cell_z = depth / static_cast<float> (rows);
    plan.sites.reserve (static_cast<std::size_t> (columns) * rows / 4);

    for (std::uint32_t row = 0; row < rows; ++row)
      for (std::uint32_t column = 0; column < columns; ++column) {
        const std::uint32_t identity = lattice_hash (column, row, seed);
        const meters_t x = (static_cast<float> (column) + 0.12f +
                            0.76f * hash_lane (identity, 0)) *
                           cell_x;
        const meters_t z =
          (static_cast<float> (row) + 0.12f + 0.76f * hash_lane (identity, 1)) *
          cell_z;
        const map::ForestCover cover = cover_at (readings, x, z);
        const proportion_t population = band (0.08f * map::forest_cover[one],
                                              0.62f * map::forest_cover[one],
                                              cover);
        if (cover < 0.06f * map::forest_cover[one] ||
            hash_lane (identity, 2) >
              population.numerical_value_in (one) * 0.96f)
          continue;
        const terrain::SurfaceElevation elevation =
          elevation_at (surface, x, z);
        const proportion_t high_ground =
          band (terrain::surface_elevation_point (115.0f * u::m),
                terrain::surface_elevation_point (195.0f * u::m),
                elevation);
        const float conifer_chance =
          0.12f + 0.58f * high_ground.numerical_value_in (one);
        plan.sites.push_back (
          { .position = forest_position (x, elevation, z),
            .normal = normal_at (surface, x, z),
            .cover = cover,
            .moisture = moisture_at (readings, x, z),
            .size =
              (0.78f + 0.50f * hash_lane (identity, 4)) * tree_size_factor[one],
            .seed = identity,
            .form = hash_lane (identity, 5) < conifer_chance
                      ? ForestForm::conifer
                      : ForestForm::broadleaf });
      }
    return plan;
  }

  void ForestLandscape::rebuild (render::Renderer& renderer,
                                 const map::SurfaceGeometry& surface,
                                 const map::SurfaceReadings& readings,
                                 std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("ForestLandscape::rebuild");
    ForestPlan plan = plan_global_forest (surface, readings, seed);
    m_period = plan.period;
    m_tree_count = plan.sites.size ();
    m_epoch = 0;
    m_chunks_x = forest_chunks_per_side;
    m_chunks_z = forest_chunks_per_side;
    const meters_t chunk_width =
      extent_component (m_period, 0) / static_cast<float> (m_chunks_x);
    const meters_t chunk_depth =
      extent_component (m_period, 2) / static_cast<float> (m_chunks_z);

    m_chunks.assign (static_cast<std::size_t> (m_chunks_x) * m_chunks_z,
                     Chunk {});
    for (ForestSite& site : plan.sites) {
      const int x = std::clamp (
        static_cast<int> ((position_component (site.position, 0) / chunk_width)
                            .numerical_value_in (one)),
        0,
        m_chunks_x - 1);
      const int z = std::clamp (
        static_cast<int> ((position_component (site.position, 2) / chunk_depth)
                            .numerical_value_in (one)),
        0,
        m_chunks_z - 1);
      m_chunks[static_cast<std::size_t> (z) * m_chunks_x + x].sites.push_back (
        site);
    }

    for (int z = 0; z < m_chunks_z; ++z)
      for (int x = 0; x < m_chunks_x; ++x) {
        Chunk& chunk = m_chunks[static_cast<std::size_t> (z) * m_chunks_x + x];
        chunk.center = position (
          Vec3 (((x + 0.5f) * chunk_width).numerical_value_in (u::m),
                110.0f,
                ((z + 0.5f) * chunk_depth).numerical_value_in (u::m)));
        chunk.radius = 0.5f * mp_units::sqrt (chunk_width * chunk_width +
                                              chunk_depth * chunk_depth) +
                       190.0f * u::m;
        if (chunk.sites.empty ())
          continue;
        render::DrawList far_trees;
        far_trees.state ().cull = false;
        for (const ForestSite& site : chunk.sites)
          append_distant_tree (far_trees, site);
        chunk.far_bytes =
          far_trees.vertices ().size () * sizeof (render::Vertex);
        chunk.far_mesh = renderer.create_mesh (far_trees);
      }
    std::erase_if (m_chunks,
                   [] (const Chunk& chunk) { return chunk.sites.empty (); });
  }

  void ForestLandscape::prepare (render::Renderer& renderer,
                                 const ForestView& view) {
    MOPPE_PROFILE_ZONE ("ForestLandscape::prepare");
    if (!periodic_world (m_period))
      return;
    ++m_epoch;
    // A chunk that has just left the near band is very likely to come back:
    // a rider turns around, a glider circles. Holding its mesh for a couple
    // of seconds is far cheaper than rebuilding it, and it is what keeps the
    // boundary from thrashing when somebody rides along it.
    constexpr std::uint64_t residency_grace = 150;
    constexpr int builds_per_frame = 2;

    int built = 0;
    for (Chunk& chunk : m_chunks) {
      const meters_t reach = forest_detail_reach + chunk.radius;
      bool wanted = false;
      visit_chunk_images (chunk.center,
                          m_period,
                          view,
                          reach,
                          [&] (const displacement_t&, meters_t distance) {
                            wanted = wanted || distance <= reach;
                          });
      if (!wanted)
        continue;
      chunk.wanted_epoch = m_epoch;
      if (chunk.near_mesh || built == builds_per_frame)
        continue;
      render::DrawList near_trees;
      near_trees.state ().cull = false;
      for (const ForestSite& site : chunk.sites)
        append_tree (near_trees, site);
      chunk.near_bytes =
        near_trees.vertices ().size () * sizeof (render::Vertex);
      chunk.near_mesh = renderer.create_mesh (near_trees);
      ++built;
    }

    for (Chunk& chunk : m_chunks)
      if (chunk.near_mesh && chunk.wanted_epoch + residency_grace < m_epoch) {
        chunk.near_mesh.reset ();
        chunk.near_bytes = 0;
      }
  }

  std::size_t ForestLandscape::resident_bytes () const noexcept {
    std::size_t bytes = 0;
    for (const Chunk& chunk : m_chunks)
      bytes += chunk.near_bytes + chunk.far_bytes;
    return bytes;
  }

  std::size_t ForestLandscape::resident_chunk_count () const noexcept {
    return static_cast<std::size_t> (
      std::ranges::count_if (m_chunks, [] (const Chunk& chunk) {
        return chunk.near_mesh != nullptr;
      }));
  }

  void ForestLandscape::draw (render::Renderer& renderer,
                              const ForestView& view) const {
    if (!periodic_world (m_period))
      return;
    for (const Chunk& chunk : m_chunks) {
      const meters_t reach = forest_geometry_reach + chunk.radius;
      const meters_t near_reach = forest_detail_reach + chunk.radius;
      visit_chunk_images (
        chunk.center,
        m_period,
        view,
        reach,
        [&] (const displacement_t& offset, meters_t distance) {
          if (distance > reach)
            return;
          if (!visible_in_view (
                chunk.center + offset - view.position, chunk.radius, view))
            return;
          const render::MeshPtr& mesh =
            chunk.near_mesh && distance <= near_reach ? chunk.near_mesh
                                                      : chunk.far_mesh;
          if (mesh)
            renderer.draw_mesh (
              *mesh, Mat4::translation (displacement_value (offset)));
        });
    }
  }
}
