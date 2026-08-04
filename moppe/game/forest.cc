#include <moppe/game/forest.hh>
#include <moppe/gfx/signal.hh>

#include <moppe/profile.hh>
#include <moppe/render/draw.hh>

#include <algorithm>
#include <array>
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
      const meters_t height = size * (conifer ? 15.0f : 13.4f) *
                              (0.82f + 0.30f * cover + 0.26f * moisture) * u::m;
      const meters_t radius = (conifer ? 0.175f : 0.220f) * height;
      const Vec3 lean = (axes.across * (hash_lane (site.seed, 51) - 0.5f) +
                         axes.forward * (hash_lane (site.seed, 52) - 0.5f)) *
                        (0.84f * radius.numerical_value_in (u::m));
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
      constexpr int most_sides = 8;
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
      Vec3 middle[most_sides];
      Vec3 upper[most_sides];
      float grain[most_sides];
      for (int side = 0; side < sides; ++side) {
        const float angle = turn + PI2 * side / sides;
        const Vec3 radial = tree.axes.across * std::cos (angle) +
                            tree.axes.forward * std::sin (angle);
        // Every ring wanders in radius and in height. A crown whose outline is
        // a regular polygon reads as a lampshade from any distance at which
        // its silhouette is resolvable at all.
        const float wide = 0.78f + 0.46f * hash_lane (tree.seed, lane + side);
        const float narrow =
          0.62f + 0.30f * hash_lane (tree.seed, lane + 8 + side);
        lower[side] =
          base +
          axis * (0.20f + 0.12f * hash_lane (tree.seed, lane + 16 + side)) +
          radial * (radius_m * 0.66f * wide);
        middle[side] =
          base +
          axis * (0.52f + 0.12f * hash_lane (tree.seed, lane + 40 + side)) +
          radial * (radius_m *
                    (0.88f + 0.24f * hash_lane (tree.seed, lane + 48 + side)));
        upper[side] =
          base +
          axis * (0.78f + 0.12f * hash_lane (tree.seed, lane + 24 + side)) +
          radial * (radius_m * 0.72f * narrow);
        grain[side] = 0.17f * (hash_lane (tree.seed, lane + 32 + side) - 0.5f);
      }

      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const int next = (side + 1) % sides;
        const FoliageVertex stem_foot = leaf (base, 0.10f, -0.06f);
        const FoliageVertex crown_tip = leaf (tip, 0.58f, 0.10f);
        const FoliageVertex low_a = leaf (lower[side], 0.94f, grain[side]);
        const FoliageVertex low_b = leaf (lower[next], 0.94f, grain[next]);
        const FoliageVertex mid_a = leaf (middle[side], 1.0f, grain[side]);
        const FoliageVertex mid_b = leaf (middle[next], 1.0f, grain[next]);
        const FoliageVertex high_a = leaf (upper[side], 1.0f, grain[side]);
        const FoliageVertex high_b = leaf (upper[next], 1.0f, grain[next]);
        foliage_triangle (draw, tree.crown, stem_foot, low_b, low_a);
        foliage_quad (draw, tree.crown, low_a, low_b, mid_b, mid_a);
        foliage_quad (draw, tree.crown, mid_a, mid_b, high_b, high_a);
        foliage_triangle (draw, tree.crown, crown_tip, high_a, high_b);
      }
      draw.end ();
    }

    struct LeafCloud {
      Vec3 base;
      Vec3 tip;
      meters_t radius;
      std::uint32_t lane;
      float shelter;
    };

    // Three intersecting, irregular cards carry the small-scale silhouette
    // around one dense crown core. Their texture is coverage only: colour,
    // lighting, wind and crown exposure still come from the organism.
    void append_leaf_cloud (render::DrawList& draw,
                            const TreeForm& tree,
                            const LeafCloud& cloud,
                            const render::Texture* texture) {
      const Vec3 axis = cloud.tip - cloud.base;
      const Vec3 direction = normalized (axis);
      const Vec3 cloud_center = cloud.base + axis * 0.55f;
      const float radius = cloud.radius.numerical_value_in (u::m);
      const float rise = rise_of (tree, cloud_center);
      const float bend = std::clamp (0.18f + 0.84f * rise, 0.0f, 1.0f);
      const float turn = PI2 * hash_lane (tree.seed, cloud.lane + 200);

      draw.set_texture (texture);
      draw.begin (render::Prim::Triangles);
      for (int card = 0; card < 3; ++card) {
        const float angle = turn + PI * card / 3.0f;
        const Vec3 across = tree.axes.across * std::cos (angle) +
                            tree.axes.forward * std::sin (angle);
        Vec3 vertical = direction - across * dot (direction, across);
        if (length2 (vertical) < 1e-4f)
          vertical = tree.axes.up;
        vertical = normalized (vertical);
        const float grain = hash_lane (tree.seed, cloud.lane + 211 + card);
        const float half_width = radius * (0.82f + 0.24f * grain);
        const float half_height = std::max (
          0.58f * radius, length (axis) * (0.30f + 0.08f * (1.0f - grain)));
        const Vec3 center =
          cloud_center + across * (radius * 0.12f * (grain - 0.5f)) +
          vertical * (radius * 0.10f *
                      (hash_lane (tree.seed, cloud.lane + 219 + card) - 0.5f));
        const Vec3 outward = normalized (
          cross (across, vertical) + tree.axes.up * (0.24f + 0.18f * grain));
        const auto leaf =
          [&] (const Vec3& point, float exposure, float u, float v) {
            return FoliageVertex { point,
                                   outward,
                                   cloud.shelter * exposure,
                                   bend * proportion[one],
                                   1.0f * proportion[one],
                                   u,
                                   v };
          };
        foliage_quad (
          draw,
          tree.crown,
          leaf (center - across * half_width - vertical * half_height,
                0.18f,
                0.0f,
                0.0f),
          leaf (center + across * half_width - vertical * half_height,
                0.30f,
                1.0f,
                0.0f),
          leaf (center + across * half_width + vertical * half_height,
                1.0f,
                1.0f,
                1.0f),
          leaf (center - across * half_width + vertical * half_height,
                0.86f,
                0.0f,
                1.0f));
      }
      draw.end ();
    }

    // Near enough to ride past, a tree stops being a mass and becomes an
    // organism. The stem bends and you can see limbs leave it; the crown is
    // several separate masses with sky between them rather than one
    // blob. None of that survives two hundred metres, which is exactly why
    // it is affordable: the band that gets it is small.
    void append_near_broadleaf (render::DrawList& draw,
                                const TreeForm& tree,
                                const render::Texture* leaf_texture) {
      const float height_m = tree.height.numerical_value_in (u::m);
      const float radius_m = tree.crown_radius.numerical_value_in (u::m);
      const Vec3& up = tree.axes.up;
      std::array<LeafCloud, 9> clouds {};
      std::size_t cloud_count = 0;
      const auto append_crown = [&] (const Vec3& base,
                                     const Vec3& tip,
                                     meters_t radius,
                                     int sides,
                                     std::uint32_t lane,
                                     float shelter) {
        clouds[cloud_count++] = { base, tip, radius, lane, shelter };
        const Vec3 center = base + (tip - base) * 0.55f;
        append_crown_lobe (draw,
                           tree,
                           center + (base - center) * 0.70f,
                           center + (tip - center) * 0.70f,
                           radius * 0.64f,
                           sides,
                           lane,
                           shelter * 0.86f);
      };

      append_stem (draw, tree, 0.84f, 0.017f, 6);
      append_crown (tree.root + up * (0.70f * height_m) + tree.lean * 0.44f,
                    tree.root + up * height_m + tree.lean,
                    tree.crown_radius * 0.50f,
                    6,
                    29,
                    1.0f);
      constexpr int limbs = 4;
      for (int limb = 0; limb < limbs; ++limb) {
        const float turn = PI2 * hash_lane (tree.seed, 91) +
                           (2.399963f * limb) +
                           0.38f * (hash_lane (tree.seed, 92 + limb) - 0.5f);
        const Vec3 outward = tree.axes.across * std::cos (turn) +
                             tree.axes.forward * std::sin (turn);
        const float fork_rise =
          0.41f + 0.070f * limb + 0.025f * hash_lane (tree.seed, 96 + limb);
        const Vec3 fork = tree.root + up * (fork_rise * height_m) +
                          tree.lean * (0.20f + 0.10f * limb);
        const float reach =
          radius_m * (0.66f + 0.34f * hash_lane (tree.seed, 101 + limb));
        const Vec3 shoulder =
          fork + outward * (reach * 0.54f) + up * (0.09f * height_m);
        const Vec3 crest =
          fork + outward * reach +
          up *
            ((0.24f + 0.045f * hash_lane (tree.seed, 106 + limb)) * height_m);
        append_wood (draw,
                     tree,
                     fork,
                     shoulder,
                     (0.014f - 0.0013f * limb) * height_m,
                     (0.0080f - 0.0007f * limb) * height_m,
                     4);
        append_wood (draw,
                     tree,
                     shoulder,
                     crest,
                     (0.0080f - 0.0007f * limb) * height_m,
                     0.0035f * height_m,
                     3);
        append_crown (
          crest - up * (0.070f * height_m) - outward * (0.08f * radius_m),
          crest + up * (0.12f * height_m) + outward * (0.10f * radius_m),
          tree.crown_radius *
            (0.31f + 0.08f * hash_lane (tree.seed, 111 + limb)),
          5,
          101 + 48 * static_cast<std::uint32_t> (limb),
          0.76f + 0.05f * limb);
        const Vec3 inner = shoulder + (crest - shoulder) * 0.44f;
        append_crown (inner - up * (0.050f * height_m),
                      inner + up * (0.090f * height_m) +
                        outward * (0.04f * radius_m),
                      tree.crown_radius *
                        (0.23f + 0.06f * hash_lane (tree.seed, 121 + limb)),
                      5,
                      317 + 48 * static_cast<std::uint32_t> (limb),
                      0.69f + 0.04f * limb);
      }
      for (std::size_t index = 0; index < cloud_count; ++index)
        append_leaf_cloud (draw, tree, clouds[index], leaf_texture);
      draw.set_texture (nullptr);
    }

    // A spruce is a stack of drooping whorls under a leader, not a cone. Each
    // whorl is made of separate boughs with sky between them; closing the
    // ring would turn the same few triangles into a Christmas-tree solid.
    void append_conifer_crown (render::DrawList& draw,
                               const TreeForm& tree,
                               int tiers,
                               int sides,
                               const render::Texture* conifer_texture) {
      const float height_m = tree.height.numerical_value_in (u::m);
      const float radius_m = tree.crown_radius.numerical_value_in (u::m);
      const auto needle = [&] (const Vec3& point,
                               const Vec3& outward,
                               float flutter,
                               float grain,
                               float u,
                               float v) {
        const float rise = rise_of (tree, point);
        return FoliageVertex {
          point,
          outward,
          std::clamp (0.10f + 0.98f * rise + grain, 0.0f, 1.0f),
          std::clamp (0.18f + 0.90f * rise, 0.0f, 1.0f) * proportion[one],
          flutter * proportion[one],
          u,
          v
        };
      };

      draw.set_texture (conifer_texture);
      draw.begin (render::Prim::Triangles);
      for (int tier = 0; tier < tiers; ++tier) {
        const float share = static_cast<float> (tier) / (tiers - 1);
        const Vec3 drift = tree.lean * share;
        const float wobble = 0.86f + 0.28f * hash_lane (tree.seed, 61 + tier);
        const float foot_rise = 0.18f + 0.64f * share;
        const Vec3 foot =
          tree.root + tree.axes.up * (foot_rise * height_m) + drift;
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
          const Vec3 radial = normalized (r0 + r1);
          const Vec3 tangent = normalized (r1 - r0);
          const float reach = 0.5f * (stretch (side) + stretch (side + 1));
          const Vec3 inner =
            foot + tree.axes.up * ((0.055f - 0.025f * share) * height_m);
          const Vec3 tip = foot + radial * reach - tree.axes.up * droop;
          const float half_width = (0.19f + 0.05f * (1.0f - share)) * reach;
          const Vec3 out = normalized (radial * 0.76f + tree.axes.up * 0.65f);
          foliage_quad (
            draw,
            tree.crown,
            needle (inner - tangent * half_width,
                    tree.axes.up,
                    0.38f,
                    0.06f,
                    0.0f,
                    0.0f),
            needle (inner + tangent * half_width,
                    tree.axes.up,
                    0.38f,
                    0.06f,
                    1.0f,
                    0.0f),
            needle (tip + tangent * half_width, out, 0.98f, 0.02f, 1.0f, 1.0f),
            needle (tip - tangent * half_width, out, 0.98f, 0.02f, 0.0f, 1.0f));
        }
      }
      draw.end ();
      draw.set_texture (nullptr);
    }

    void append_tree (render::DrawList& draw,
                      const ForestSite& site,
                      const render::Texture* leaf_texture,
                      const render::Texture* conifer_texture) {
      const TreeForm tree = resolve_tree (site, 0.20f);
      if (site.form == ForestForm::conifer) {
        append_stem (draw, tree, 0.98f, 0.014f, 6);
        append_conifer_crown (draw, tree, 8, 6, conifer_texture);
      } else {
        append_near_broadleaf (draw, tree, leaf_texture);
      }
    }

    // A single two-sided ribbon is enough stem at this distance. It matters
    // less as wood than as the clean interval of sky between the ground and
    // the crown; without it every broadleaf becomes a diamond planted point
    // first in the soil.
    void append_distant_stem (render::DrawList& draw,
                              const TreeForm& tree,
                              float top_rise) {
      const float height_m = tree.height.numerical_value_in (u::m);
      const float radius = 0.018f * height_m;
      const Vec3 top = tree.root + tree.axes.up * (top_rise * height_m) +
                       tree.lean * (0.35f * top_rise);
      const auto wood =
        [&] (const Vec3& point, const Vec3& outward, float rise) {
          return FoliageVertex { point,
                                 outward,
                                 0.24f + 0.68f * rise,
                                 (0.12f + 0.72f * rise) * proportion[one],
                                 0.0f * proportion[one] };
        };
      draw.begin (render::Prim::Triangles);
      foliage_quad (
        draw,
        tree.bark,
        wood (tree.root - tree.axes.across * radius, tree.axes.forward, 0.0f),
        wood (tree.root + tree.axes.across * radius, tree.axes.forward, 0.0f),
        wood (top + tree.axes.across * (0.42f * radius),
              tree.axes.forward,
              top_rise),
        wood (top - tree.axes.across * (0.42f * radius),
              tree.axes.forward,
              top_rise));
      draw.end ();
    }

    // Beyond the near band a tree is a few pixels of canopy. Two crossed
    // cards keep a volume from every bearing while filtered coverage carries
    // the species silhouette below the pixel. Trunks and the two crown
    // materials are recorded in separate lists so a whole chunk remains
    // three runs rather than alternating texture state for every organism.
    void append_distant_crown (render::DrawList& draw,
                               const TreeForm& tree,
                               bool conifer) {
      const float height_m = tree.height.numerical_value_in (u::m);
      const float radius_m = tree.crown_radius.numerical_value_in (u::m);
      const Vec3& up = tree.axes.up;
      const Vec3 waist = tree.root + up * (0.40f * height_m);
      const Vec3 crest = tree.root + up * height_m + tree.lean * 0.5f;
      const auto mass =
        [&] (const Vec3& point, const Vec3& outward, float u, float v) {
          const float rise = rise_of (tree, point);
          return FoliageVertex {
            point,
            normalized (outward * 0.86f + up * 0.52f),
            std::clamp (0.22f + 0.62f * rise, 0.0f, 1.0f),
            std::clamp (0.20f + 0.80f * rise, 0.0f, 1.0f) * proportion[one],
            (rise > 0.34f ? 0.62f : 0.0f) * proportion[one],
            u,
            v
          };
        };

      draw.begin (render::Prim::Triangles);
      if (!conifer) {
        for (int lobe = 0; lobe < 2; ++lobe) {
          const float direction = lobe == 0 ? 1.0f : -1.0f;
          const float lower_rise = lobe == 0 ? 0.55f : 0.49f;
          const float upper_rise = lobe == 0 ? 0.98f : 0.88f;
          const float lobe_radius = radius_m * (lobe == 0 ? 0.76f : 0.70f);
          const Vec3 offset =
            tree.axes.across * (direction * 0.34f * radius_m) +
            tree.axes.forward * ((lobe == 0 ? 0.12f : -0.16f) * radius_m);
          const Vec3 base = tree.root + up * (lower_rise * height_m) + offset +
                            tree.lean * (0.28f * lower_rise);
          const Vec3 tip = tree.root + up * (upper_rise * height_m) +
                           offset * 0.70f + tree.lean * (0.52f * upper_rise);
          for (const Vec3& axis :
               std::array { tree.axes.across, tree.axes.forward }) {
            const Vec3 outward = normalized (up * 0.62f + axis * 0.38f);
            foliage_quad (
              draw,
              tree.crown,
              mass (base - axis * lobe_radius, outward, 0.0f, 0.0f),
              mass (base + axis * lobe_radius, outward, 1.0f, 0.0f),
              mass (tip + axis * (0.56f * lobe_radius), outward, 1.0f, 1.0f),
              mass (tip - axis * (0.56f * lobe_radius), outward, 0.0f, 1.0f));
          }
        }
        draw.end ();
        return;
      }
      for (const Vec3& axis :
           std::array { tree.axes.across, tree.axes.forward }) {
        const Vec3 outward = normalized (up * 0.62f + axis * 0.38f);
        foliage_quad (
          draw,
          tree.crown,
          mass (waist - axis * radius_m, outward, 0.0f, 0.0f),
          mass (waist + axis * radius_m, outward, 1.0f, 0.0f),
          mass (crest + axis * (0.06f * radius_m), outward, 1.0f, 1.0f),
          mass (crest - axis * (0.06f * radius_m), outward, 0.0f, 1.0f));
      }
      draw.end ();
    }
  }

  void ForestLandscape::rebuild (render::Renderer& renderer,
                                 const map::SurfaceGeometry& surface,
                                 const map::SurfaceReadings& readings,
                                 std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("ForestLandscape::rebuild");
    rebuild (renderer, plan_global_forest (surface, readings, seed));
  }

  void ForestLandscape::rebuild (render::Renderer& renderer,
                                 const ForestPlan& plan) {
    MOPPE_PROFILE_ZONE ("ForestLandscape::rebuild_plan");
    m_period = plan.period;
    m_tree_count = plan.sites.size ();
    m_epoch = 0;
    m_chunks_x = forest_chunks_per_side;
    m_chunks_z = forest_chunks_per_side;
    const meters_t chunk_width =
      extent_component (m_period, 0) / static_cast<float> (m_chunks_x);
    const meters_t chunk_depth =
      extent_component (m_period, 2) / static_cast<float> (m_chunks_z);

    m_chunks.clear ();
    m_leaf_texture = make_leaf_card_texture (renderer);
    m_conifer_texture = make_conifer_card_texture (renderer);
    m_distant_conifer_texture = make_conifer_crown_texture (renderer);
    m_chunks.resize (static_cast<std::size_t> (m_chunks_x) * m_chunks_z);
    for (const ForestSite& site : plan.sites) {
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
        render::DrawList far_stems;
        render::DrawList far_broadleaf;
        render::DrawList far_conifers;
        far_trees.state ().cull = false;
        far_stems.state ().cull = false;
        far_broadleaf.state ().cull = false;
        far_conifers.state ().cull = false;
        far_broadleaf.set_texture (m_leaf_texture.get ());
        far_conifers.set_texture (m_distant_conifer_texture.get ());
        for (const ForestSite& site : chunk.sites) {
          const TreeForm tree = resolve_tree (site, 0.14f);
          const bool conifer = site.form == ForestForm::conifer;
          append_distant_stem (far_stems, tree, conifer ? 0.82f : 0.70f);
          append_distant_crown (
            conifer ? far_conifers : far_broadleaf, tree, conifer);
        }
        far_trees.append (far_stems);
        far_trees.append (far_broadleaf);
        far_trees.append (far_conifers);
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
    // Recording detailed organisms is CPU work too. Spread it across calls,
    // as well as limiting uploads, so entering an uncached neighbourhood
    // cannot take a whole frame just to finish one or two compound meshes.
    constexpr std::size_t sites_per_frame = 96;
    constexpr int meshes_per_frame = 1;

    std::size_t built_sites = 0;
    int built_meshes = 0;
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
      if (chunk.near_mesh || built_meshes == meshes_per_frame ||
          built_sites == sites_per_frame)
        continue;

      if (!chunk.near_build) {
        chunk.near_build = std::make_unique<render::DrawList> ();
        chunk.near_build->state ().cull = false;
      }
      while (chunk.near_build_site < chunk.sites.size () &&
             built_sites < sites_per_frame) {
        append_tree (*chunk.near_build,
                     chunk.sites[chunk.near_build_site++],
                     m_leaf_texture.get (),
                     m_conifer_texture.get ());
        ++built_sites;
      }
      if (chunk.near_build_site != chunk.sites.size ())
        continue;

      chunk.near_bytes =
        chunk.near_build->vertices ().size () * sizeof (render::Vertex);
      chunk.near_mesh = renderer.create_mesh (*chunk.near_build);
      chunk.near_build.reset ();
      chunk.near_build_site = 0;
      ++built_meshes;
    }

    for (Chunk& chunk : m_chunks) {
      if (chunk.near_mesh && chunk.wanted_epoch + residency_grace < m_epoch) {
        chunk.near_mesh.reset ();
        chunk.near_bytes = 0;
      }
      if (chunk.near_build && chunk.wanted_epoch + residency_grace < m_epoch) {
        chunk.near_build.reset ();
        chunk.near_build_site = 0;
      }
    }
  }

  std::size_t ForestLandscape::resident_bytes () const noexcept {
    std::size_t bytes = 0;
    for (const Chunk& chunk : m_chunks) {
      bytes += chunk.near_bytes + chunk.far_bytes;
      if (chunk.near_build)
        bytes +=
          chunk.near_build->vertices ().size () * sizeof (render::Vertex);
    }
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
