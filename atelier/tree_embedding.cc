#include "atelier/tree_embedding.hh"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>

namespace atelier {
  using moppe::spatial::get;
  using namespace si::unit_symbols;

  namespace {
    Vec3 edge_direction (const TreeEdgeForm& form) {
      const Real elevation = in_radians (form.elevation);
      const Real azimuth = in_radians (form.azimuth);
      const Real horizontal = std::cos (elevation);
      return { horizontal * std::sin (azimuth),
               std::sin (elevation),
               horizontal * std::cos (azimuth) };
    }

    Vec3 branch_normal (const Vec3& direction) {
      const Vec3 anchor =
        std::abs (scalar_product (direction, up)) > 0.92f ? east : up;
      const Vec3 across = vector_product (direction, anchor).unit ();
      return vector_product (across, direction).unit ();
    }

    Matrix bud_placement (const Point& at, const Vec3& facing, Length radius) {
      Matrix place = rotation (up, facing);
      const Real scale = in_metres (radius);
      place.columns[0] *= scale;
      place.columns[1] *= 0.24f * scale;
      place.columns[2] *= scale;
      place.columns[3] = simd_make_float4 (in_metres (at), 1.0f);
      return place;
    }

    EmbeddedTreeBranch branch_between (const Tree& tree,
                                       TreeEdgeId id,
                                       const std::vector<Point>& positions,
                                       Real bend) {
      const TreeEdge& edge = tree.topology ().edge (id);
      const Vec3 direction =
        atelier::direction (positions[edge.child] - positions[edge.parent]);
      const auto& radius = get<branch_radius> (tree.edges ());
      const auto& xylem = get<xylem_flux> (tree.edges ());
      const auto& phloem = get<phloem_flux> (tree.edges ());
      return {
        .start = in_metres (positions[edge.parent]),
        .end = in_metres (positions[edge.child]),
        .start_normal = as_simd (branch_normal (direction)),
        .end_normal = as_simd (branch_normal (direction)),
        .strain = 0.0f,
        .bend = bend,
        .radius = radius[id].numerical_value_in (m) * m,
        .material_seed = tree.form (id).material_seed,
        .xylem = xylem[id].numerical_value_in (mp_units::one / s),
        .phloem = phloem[id].numerical_value_in (mp_units::one / s),
      };
    }

    std::vector<Real> diagram_abscissae (const Tree& tree, TreeOrgan organ) {
      const DirectedTreeTopology& topology = tree.topology ();
      std::vector<Real> x (topology.vertex_count (), 0.0f);
      Real next_tip = 0.0f;
      for (TreeVertexId vertex = topology.vertex_count (); vertex-- > 0;) {
        if (vertex == 0)
          continue;
        const TreeEdge& parent = topology.edge (topology.parent_edge (vertex));
        if (parent.organ != organ)
          continue;
        const auto children = topology.child_edges (vertex);
        const bool has_child =
          std::ranges::any_of (children, [&] (TreeEdgeId edge) {
            return topology.edge (edge).organ == organ;
          });
        if (!has_child) {
          x[vertex] = next_tip;
          next_tip += 1.0f;
          continue;
        }
        Real total = 0.0f;
        Real count = 0.0f;
        for (TreeEdgeId edge : children)
          if (topology.edge (edge).organ == organ) {
            total += x[topology.edge (edge).child];
            count += 1.0f;
          }
        x[vertex] = total / count;
      }
      Real center = 0.0f;
      Real root_branch_count = 0.0f;
      for (TreeEdgeId edge : topology.child_edges (0))
        if (topology.edge (edge).organ == organ) {
          center += x[topology.edge (edge).child];
          root_branch_count += 1.0f;
        }
      center /= std::max (1.0f, root_branch_count);
      for (TreeVertexId vertex = 1; vertex < topology.vertex_count (); ++vertex)
        if (topology.edge (topology.parent_edge (vertex)).organ == organ)
          x[vertex] -= center;
      return x;
    }

    EmbeddedTree diagram_embedding (const Tree& tree, Real aspect_ratio) {
      const DirectedTreeTopology& topology = tree.topology ();
      const std::vector<Real> shoot_x =
        diagram_abscissae (tree, TreeOrgan::shoot);
      const std::vector<Real> root_x =
        diagram_abscissae (tree, TreeOrgan::root);
      std::vector<Point> positions (topology.vertex_count (),
                                    scene + up * (0.0f * m));
      constexpr Length horizontal_pitch = 0.28f * m;
      constexpr Length vertical_pitch = 0.44f * m;
      for (TreeVertexId vertex = 1; vertex < topology.vertex_count ();
           ++vertex) {
        const TreeEdge& parent = topology.edge (topology.parent_edge (vertex));
        const Real direction = parent.organ == TreeOrgan::shoot ? 1.0f : -1.0f;
        const Real abscissa =
          parent.organ == TreeOrgan::shoot ? shoot_x[vertex] : root_x[vertex];
        positions[vertex] =
          scene + east * (abscissa * horizontal_pitch) +
          up * (direction * Real (topology.vertex (vertex).generation) *
                vertical_pitch);
      }

      const simd_float3 origin = in_metres (positions[0]);
      simd_float2 lower { 0.0f, 0.0f };
      simd_float2 upper { 0.0f, 0.0f };
      for (const Point& position : positions) {
        const simd_float3 local = in_metres (position) - origin;
        lower = simd_min (lower, simd_float2 { local.x, local.y });
        upper = simd_max (upper, simd_float2 { local.x, local.y });
      }
      constexpr Real margin = 0.55f;
      const Real content_width = upper.x - lower.x + 2.0f * margin;
      const Real content_height = upper.y - lower.y + 2.0f * margin;
      const Length width =
        std::max (content_width, content_height * aspect_ratio) * m;
      const Point target = scene + east * (0.5f * (lower.x + upper.x) * m) +
                           up * (0.5f * (lower.y + upper.y) * m);
      const Point eye = target + north * (22.0f * m);
      EmbeddedTree result {
        .world_to_clip =
          simd_mul (orthographic (width, aspect_ratio, 0.05f * m, 80.0f * m),
                    look_at (eye, target)),
        .eye = eye,
        .buds = {},
        .branches = {},
      };
      result.branches.reserve (topology.edge_count ());
      for (TreeEdgeId edge = 0; edge < topology.edge_count (); ++edge)
        result.branches.push_back (
          branch_between (tree, edge, positions, 0.0f));

      const auto& vigor = get<bud_vigor> (tree.vertices ());
      for (TreeVertexId vertex = 0; vertex < topology.vertex_count (); ++vertex)
        if (topology.is_tip (vertex)) {
          const TreeEdgeId parent = topology.parent_edge (vertex);
          const Real seed = tree.form (parent).material_seed;
          result.buds.push_back (
            { .place_in_world =
                bud_placement (positions[vertex], north, 0.13f * m),
              .vigor = vigor[vertex].numerical_value_in (mp_units::one),
              .material_seed = seed,
              .organ = topology.edge (parent).organ });
        }
      return result;
    }

    EmbeddedTree
    wind_embedding (const Tree& tree, Duration elapsed, Real aspect_ratio) {
      const DirectedTreeTopology& topology = tree.topology ();
      const auto& length = get<branch_rest_length> (tree.edges ());
      const auto& flexibility = get<branch_flexibility> (tree.edges ());
      std::vector<Point> positions (topology.vertex_count (),
                                    scene + up * (0.0f * m));
      std::vector<Vec3> directions (topology.vertex_count (), up);
      std::vector<Real> bends (topology.edge_count (), 0.0f);
      const Real seconds = elapsed.numerical_value_in (s);
      Real maximum_generation = 1.0f;
      for (TreeVertexId vertex = 0; vertex < topology.vertex_count (); ++vertex)
        maximum_generation = std::max (
          maximum_generation, Real (topology.vertex (vertex).generation));

      for (TreeVertexId vertex = 1; vertex < topology.vertex_count ();
           ++vertex) {
        const TreeEdgeId id = topology.parent_edge (vertex);
        const TreeEdge& edge = topology.edge (id);
        const Real flex = flexibility[id].numerical_value_in (mp_units::one);
        const Real height = Real (topology.vertex (vertex).generation) /
                            std::max (1.0f, maximum_generation);
        const Real gust = std::sin (0.92f * seconds + 0.37f * Real (vertex) +
                                    4.0f * tree.form (id).material_seed);
        Vec3 direction = edge_direction (tree.form (id));
        const Real exposure = edge.organ == TreeOrgan::shoot ? 1.0f : 0.08f;
        const Vec3 wind =
          east * (exposure * (0.10f + 0.13f * gust) * flex * height) +
          north * (exposure * 0.045f *
                   std::sin (0.57f * seconds + Real (vertex)) * flex);
        direction += wind;
        direction[1] -= 0.045f * Real (edge.branch_order) * flex;
        direction = direction.unit ();
        directions[vertex] = direction;
        bends[id] = scalar_product (wind, east);
        const Length physical_length = length[id].numerical_value_in (m) * m;
        positions[vertex] =
          positions[edge.parent] + direction * physical_length;
      }

      const Point target = scene + up * (2.6f * m);
      const Point eye =
        target + east * (11.0f * m) + up * (5.0f * m) + north * (15.8f * m);
      EmbeddedTree result {
        .world_to_clip = simd_mul (
          perspective (
            44.0f * angular::degree, aspect_ratio, 0.05f * m, 80.0f * m),
          look_at (eye, target)),
        .eye = eye,
        .buds = {},
        .branches = {},
      };
      result.branches.reserve (topology.edge_count ());
      for (TreeEdgeId edge = 0; edge < topology.edge_count (); ++edge)
        result.branches.push_back (
          branch_between (tree, edge, positions, bends[edge]));

      const auto& vigor = get<bud_vigor> (tree.vertices ());
      result.buds.reserve (6 * tree.tip_count ());
      for (TreeVertexId vertex = 0; vertex < topology.vertex_count (); ++vertex)
        if (vigor[vertex] > 0.0f * mp_units::one) {
          const TreeEdgeId parent = topology.parent_edge (vertex);
          const Real seed = tree.form (parent).material_seed;
          for (int leaf = -1; leaf <= 1; ++leaf) {
            const Angle turn =
              (Real (leaf) * 0.78f + seed * 0.31f) * angular::radian;
            Vec3 facing =
              (directions[vertex] * 0.72f + radial (turn) * 0.48f).unit ();
            const Point at =
              positions[vertex] + facing * ((0.09f + 0.025f * leaf) * m);
            result.buds.push_back (
              { .place_in_world =
                  bud_placement (at, facing, (0.16f + 0.02f * leaf) * m),
                .vigor = vigor[vertex].numerical_value_in (mp_units::one),
                .material_seed = seed + 0.17f * Real (leaf),
                .organ = TreeOrgan::shoot });
          }
        }
      return result;
    }
  }

  EmbeddedTree embed_tree (const Tree& tree,
                           TreeEmbeddingKind kind,
                           Duration elapsed,
                           Real aspect_ratio) {
    switch (kind) {
    case TreeEmbeddingKind::diagram:
      return diagram_embedding (tree, aspect_ratio);
    case TreeEmbeddingKind::wind_bent:
      return wind_embedding (tree, elapsed, aspect_ratio);
    }
  }
}
