#include <moppe/terrain/fractional_drainage.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/flood.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::terrain {
  namespace {
    inline constexpr struct routing_surface_elevation
        : quantity_spec<mp_units::isq::height, mp_units::is_kind> {
    } routing_surface_elevation;
    using RoutingSurfaceElevation = mp_units::quantity_point<
      routing_surface_elevation[mp_units::si::metre],
      mp_units::default_point_origin (
        routing_surface_elevation[mp_units::si::metre]),
      float>;
    using RoutingSurface =
      spatial::Bundle<TerrainLatticeDomain, RoutingSurfaceElevation>;

    constexpr std::array neighbour_offsets {
      std::array { 1, 0 },   std::array { 1, -1 }, std::array { 0, -1 },
      std::array { -1, -1 }, std::array { -1, 0 }, std::array { -1, 1 },
      std::array { 0, 1 },   std::array { 1, 1 }
    };

    struct FacetOffsets {
      int cardinal_x;
      int cardinal_y;
      int diagonal_x;
      int diagonal_y;
    };

    constexpr std::array facet_offsets {
      FacetOffsets { 1, 0, 1, -1 },   FacetOffsets { 0, -1, 1, -1 },
      FacetOffsets { 0, -1, -1, -1 }, FacetOffsets { -1, 0, -1, -1 },
      FacetOffsets { -1, 0, -1, 1 },  FacetOffsets { 0, 1, -1, 1 },
      FacetOffsets { 0, 1, 1, 1 },    FacetOffsets { 1, 0, 1, 1 }
    };

    float normalized_angle (float radians) {
      constexpr float turn = 2.0f * std::numbers::pi_v<float>;
      radians = std::fmod (radians, turn);
      return radians < 0.0f ? radians + turn : radians;
    }

    meters_t offset_distance (int columns, int rows, const TerrainGrid& grid) {
      return std::hypot (static_cast<float> (columns) * grid.spacing_x_m (),
                         static_cast<float> (rows) * grid.spacing_y_m ()) *
             mp_units::si::metre;
    }

    DrainageDirection
    direction_for_offset (int columns, int rows, const TerrainGrid& grid) {
      const float x = static_cast<float> (columns) * grid.spacing_x_m ();
      const float z = static_cast<float> (rows) * grid.spacing_y_m ();
      return normalized_angle (std::atan2 (z, x)) *
             drainage_direction[mp_units::angular::radian];
    }

    Vec3 direction_vector (DrainageDirection direction) {
      const float radians =
        direction.numerical_value_in (mp_units::angular::radian);
      return Vec3 (std::cos (radians), 0.0f, std::sin (radians));
    }

    struct NeighbourGeometry {
      int columns;
      int rows;
      meters_t distance;
      DrainageDirection direction;
      Vec3 unit_direction;
    };

    struct FacetGeometry {
      FacetOffsets offsets;
      meters_t d1;
      meters_t d2;
      float extent;
      float u1_x;
      float u1_z;
      float u2_x;
      float u2_z;
    };

    struct DInfinityStencil {
      std::array<NeighbourGeometry, neighbour_offsets.size ()> neighbours;
      std::array<FacetGeometry, facet_offsets.size ()> facets;

      explicit DInfinityStencil (const TerrainGrid& grid) {
        for (std::size_t i = 0; i < neighbour_offsets.size (); ++i) {
          const int columns = neighbour_offsets[i][0];
          const int rows = neighbour_offsets[i][1];
          const DrainageDirection direction =
            direction_for_offset (columns, rows, grid);
          neighbours[i] = { .columns = columns,
                            .rows = rows,
                            .distance = offset_distance (columns, rows, grid),
                            .direction = direction,
                            .unit_direction = direction_vector (direction) };
        }
        for (std::size_t i = 0; i < facet_offsets.size (); ++i) {
          const FacetOffsets offsets = facet_offsets[i];
          const meters_t d1 =
            offset_distance (offsets.cardinal_x, offsets.cardinal_y, grid);
          const meters_t d2 =
            offset_distance (offsets.diagonal_x - offsets.cardinal_x,
                             offsets.diagonal_y - offsets.cardinal_y,
                             grid);
          const float d1_m = meters_value (d1);
          const float d2_m = meters_value (d2);
          facets[i] = { .offsets = offsets,
                        .d1 = d1,
                        .d2 = d2,
                        .extent = std::atan2 (d2_m, d1_m),
                        .u1_x = offsets.cardinal_x * grid.spacing_x_m () / d1_m,
                        .u1_z = offsets.cardinal_y * grid.spacing_y_m () / d1_m,
                        .u2_x = (offsets.diagonal_x - offsets.cardinal_x) *
                                grid.spacing_x_m () / d2_m,
                        .u2_z = (offsets.diagonal_y - offsets.cardinal_y) *
                                grid.spacing_y_m () / d2_m };
        }
      }
    };

    float route_score (float slope,
                       DrainageDirection direction,
                       ChannelTangent previous_tangent,
                       ChannelPersistence persistence) {
      const Vec3 previous = previous_tangent.numerical_value_in (mp_units::one);
      const float previous_length_squared = length2 (previous);
      if (previous_length_squared <= 1e-12f)
        return slope;
      const float alignment =
        std::clamp (dot (direction_vector (direction), previous) /
                      std::sqrt (previous_length_squared),
                    -1.0f,
                    1.0f);
      const float memory = persistence.numerical_value_in (mp_units::one);
      return slope * (1.0f + memory * alignment);
    }

    float route_score (float slope,
                       const Vec3& direction,
                       ChannelTangent previous_tangent,
                       ChannelPersistence persistence) {
      const Vec3 previous = previous_tangent.numerical_value_in (mp_units::one);
      const float previous_length_squared = length2 (previous);
      if (previous_length_squared <= 1e-12f)
        return slope;
      const float alignment = std::clamp (dot (direction, previous) /
                                            std::sqrt (previous_length_squared),
                                          -1.0f,
                                          1.0f);
      const float memory = persistence.numerical_value_in (mp_units::one);
      return slope * (1.0f + memory * alignment);
    }

    FractionalFlowRoute single_route (CellIndex receiver,
                                      int columns,
                                      int rows,
                                      const TerrainGrid& grid) {
      FractionalFlowRoute route;
      route.arcs[0] = { .receiver = receiver,
                        .fraction = 1.0f * flow_fraction[mp_units::one] };
      route.arc_count = 1;
      route.run = offset_distance (columns, rows, grid);
      return route;
    }

    std::array<int, 2> receiver_offset (CellIndex cell,
                                        CellIndex receiver,
                                        const TerrainLatticeDomain& domain) {
      const int width = static_cast<int> (domain.width ());
      const int height = static_cast<int> (domain.height ());
      int dx = static_cast<int> (receiver.value % domain.width ()) -
               static_cast<int> (cell.value % domain.width ());
      int dy = static_cast<int> (receiver.value / domain.width ()) -
               static_cast<int> (cell.value / domain.width ());
      if (domain.grid ().topology == Topology::Torus) {
        if (dx > width / 2)
          dx -= width;
        if (dx < -width / 2)
          dx += width;
        if (dy > height / 2)
          dy -= height;
        if (dy < -height / 2)
          dy += height;
      }
      return { dx, dy };
    }

    struct RouteReading {
      FractionalFlowRoute route;
      DrainageDirection direction =
        0.0f * drainage_direction[mp_units::angular::radian];
      slope_t slope = 0.0f * terrain_slope[mp_units::one];
    };

    RouteReading d_infinity_route (const RoutingSurface& surface,
                                   CellIndex cell,
                                   ChannelTangent previous_tangent,
                                   ChannelPersistence persistence,
                                   const DInfinityStencil& stencil) {
      const TerrainLatticeDomain& domain = surface.domain ();
      const TerrainGrid& grid = domain.grid ();
      const auto focus = spatial::BundleFocus (surface, cell);
      const RoutingSurfaceElevation center =
        spatial::get<routing_surface_elevation> (focus);
      RouteReading best;
      float best_score = 0.0f;

      // Single-neighbour candidates cover bounded edges and are also the
      // clamped-to-edge cases of the triangular-facet construction.
      for (const NeighbourGeometry& geometry : stencil.neighbours) {
        const auto receiver =
          domain.neighbour (cell, geometry.columns, geometry.rows);
        if (!receiver)
          continue;
        const auto drop = center - spatial::get<routing_surface_elevation> (
                                     focus.row (*receiver));
        const float slope =
          (drop / geometry.distance).numerical_value_in (mp_units::one);
        const float score = route_score (
          slope, geometry.unit_direction, previous_tangent, persistence);
        if (score > best_score) {
          best_score = score;
          best.route =
            single_route (*receiver, geometry.columns, geometry.rows, grid);
          best.direction = geometry.direction;
          best.slope = slope * terrain_slope[mp_units::one];
        }
      }

      for (const FacetGeometry& geometry : stencil.facets) {
        const FacetOffsets offsets = geometry.offsets;
        const auto cardinal =
          domain.neighbour (cell, offsets.cardinal_x, offsets.cardinal_y);
        const auto diagonal =
          domain.neighbour (cell, offsets.diagonal_x, offsets.diagonal_y);
        if (!cardinal || !diagonal)
          continue;
        const RoutingSurfaceElevation cardinal_height =
          spatial::get<routing_surface_elevation> (focus.row (*cardinal));
        const RoutingSurfaceElevation diagonal_height =
          spatial::get<routing_surface_elevation> (focus.row (*diagonal));
        const float s1 = ((center - cardinal_height) / geometry.d1)
                           .numerical_value_in (mp_units::one);
        const float s2 = ((cardinal_height - diagonal_height) / geometry.d2)
                           .numerical_value_in (mp_units::one);
        if (s1 <= 0.0f || s2 <= 0.0f)
          continue;

        const float relative = std::atan2 (s2, s1);
        if (!(relative > 0.0f && relative < geometry.extent))
          continue;
        const float facet_slope = std::hypot (s1, s2);

        const float direction_x = std::cos (relative) * geometry.u1_x +
                                  std::sin (relative) * geometry.u2_x;
        const float direction_z = std::cos (relative) * geometry.u1_z +
                                  std::sin (relative) * geometry.u2_z;
        const float diagonal_fraction = relative / geometry.extent;
        const float interpolation =
          std::clamp (meters_value (geometry.d1) * std::tan (relative) /
                        meters_value (geometry.d2),
                      0.0f,
                      1.0f);
        const DrainageDirection direction =
          normalized_angle (std::atan2 (direction_z, direction_x)) *
          drainage_direction[mp_units::angular::radian];
        const float score =
          route_score (facet_slope, direction, previous_tangent, persistence);
        if (score <= best_score)
          continue;
        best_score = score;

        best.route.arcs[0] = { .receiver = *cardinal,
                               .fraction = (1.0f - diagonal_fraction) *
                                           flow_fraction[mp_units::one] };
        best.route.arcs[1] = { .receiver = *diagonal,
                               .fraction = diagonal_fraction *
                                           flow_fraction[mp_units::one] };
        best.route.arc_count = 2;
        best.route.receiver_interpolation =
          interpolation * facet_coordinate[mp_units::one];
        best.route.run = meters_value (geometry.d1) / std::cos (relative) *
                         mp_units::si::metre;
        best.direction = direction;
        best.slope = facet_slope * terrain_slope[mp_units::one];
      }
      return best;
    }

    std::vector<CellIndex>
    accumulate (const TerrainLatticeDomain& lattice,
                const std::vector<FractionalFlowRoute>& routes,
                const std::vector<DrainageDirection>& directions,
                std::vector<FractionalContributingArea>& areas,
                std::vector<ChannelTangent>& tangents,
                std::vector<ChannelAreaFlux>& area_fluxes) {
      std::vector<std::uint32_t> donors (lattice.size (), 0);
      for (std::size_t offset = 0; offset < routes.size (); ++offset) {
        const FractionalFlowRoute& route = routes[offset];
        float total_fraction = 0.0f;
        for (std::uint8_t arc = 0; arc < route.arc_count; ++arc) {
          const FractionalFlowArc& flow = route.arcs[arc];
          const float fraction =
            flow.fraction.numerical_value_in (mp_units::one);
          if (flow.receiver == lattice.index (offset) ||
              flow.receiver.value >= lattice.size () ||
              !std::isfinite (fraction) || fraction <= 0.0f)
            throw std::logic_error ("invalid fractional drainage arc");
          total_fraction += fraction;
          ++donors[lattice.offset (flow.receiver)];
        }
        if (route.arc_count != 0 && std::fabs (total_fraction - 1.0f) > 1e-6f)
          throw std::logic_error (
            "fractional drainage route does not conserve flow");
      }

      std::priority_queue<std::uint32_t,
                          std::vector<std::uint32_t>,
                          std::greater<std::uint32_t>>
        ready;
      for (std::uint32_t cell = 0; cell < lattice.size (); ++cell)
        if (donors[cell] == 0)
          ready.push (cell);

      std::vector<CellIndex> order;
      order.reserve (lattice.size ());
      std::vector<Vec3> incoming_area_flux (lattice.size (), Vec3 ());
      while (!ready.empty ()) {
        const CellIndex cell { ready.top () };
        ready.pop ();
        order.push_back (cell);
        const FractionalFlowRoute& route = routes[cell.value];
        const float area_m2 = areas[cell.value].numerical_value_in (
          mp_units::si::metre * mp_units::si::metre);
        Vec3 combined = incoming_area_flux[cell.value];
        if (!route.empty ())
          combined += area_m2 * direction_vector (directions[cell.value]);
        Vec3 tangent;
        if (length2 (combined) > 1e-12f)
          tangent = normalized (combined);
        const Vec3 outgoing_area_flux = area_m2 * tangent;
        tangents[cell.value] = tangent * channel_tangent[mp_units::one];
        area_fluxes[cell.value] =
          outgoing_area_flux *
          channel_area_flux[mp_units::si::metre * mp_units::si::metre];
        for (std::uint8_t arc = 0; arc < route.arc_count; ++arc) {
          const FractionalFlowArc& flow = route.arcs[arc];
          const float fraction =
            flow.fraction.numerical_value_in (mp_units::one);
          areas[flow.receiver.value] += fraction * areas[cell.value];
          incoming_area_flux[flow.receiver.value] +=
            fraction * outgoing_area_flux;
          if (--donors[flow.receiver.value] == 0)
            ready.push (flow.receiver.value);
        }
      }
      if (order.size () != lattice.size ())
        throw std::logic_error ("fractional drainage routing contains a cycle");
      return order;
    }
  }

  TerrainLatticeDomain::TerrainLatticeDomain (TerrainGrid grid)
      : m_grid (grid) {
    if (grid.width < 2 || grid.height < 2 ||
        grid.spacing_x <= 0.0f * mp_units::si::metre ||
        grid.spacing_y <= 0.0f * mp_units::si::metre ||
        grid.height_scale <= 0.0f * mp_units::si::metre ||
        (grid.topology == Topology::Torus &&
         (grid.width < 3 || grid.height < 3)))
      throw std::invalid_argument ("invalid terrain lattice domain");
  }

  std::size_t TerrainLatticeDomain::offset (CellIndex index) const {
    if (index.value >= size ())
      throw std::out_of_range ("cell is outside terrain lattice domain");
    return index.value;
  }

  CellIndex TerrainLatticeDomain::index (std::size_t offset) const {
    if (offset >= size ())
      throw std::out_of_range ("offset is outside terrain lattice domain");
    return CellIndex { static_cast<std::uint32_t> (offset) };
  }

  std::optional<CellIndex> TerrainLatticeDomain::neighbour (CellIndex index,
                                                            int columns,
                                                            int rows) const {
    const std::size_t cell = offset (index);
    int x = static_cast<int> (cell % width ()) + columns;
    int y = static_cast<int> (cell / width ()) + rows;
    if (m_grid.topology == Topology::Torus) {
      x = wrap_index (x, static_cast<int> (width ()));
      y = wrap_index (y, static_cast<int> (height ()));
    } else if (x < 0 || y < 0 || x >= static_cast<int> (width ()) ||
               y >= static_cast<int> (height ())) {
      return std::nullopt;
    }
    return CellIndex { static_cast<std::uint32_t> (
      static_cast<std::size_t> (y) * width () + static_cast<std::size_t> (x)) };
  }

  FractionalFlowDomain::FractionalFlowDomain (
    TerrainLatticeDomain lattice,
    std::vector<FractionalFlowRoute> routes,
    std::vector<CellIndex> topological_order)
      : m_lattice (std::move (lattice)), m_routes (std::move (routes)),
        m_topological_order (std::move (topological_order)) {
    if (m_routes.size () != size () || m_topological_order.size () != size ())
      throw std::invalid_argument (
        "fractional flow domain data does not match terrain lattice");
  }

  namespace {
    FractionalDrainage analyze_fractional_drainage_impl (
      const TerrainView& terrain,
      const FloodField& flood,
      const LakeCensus& census,
      std::span<const ChannelTangent> previous_tangent,
      ChannelPersistence persistence,
      const FractionalRouteBackend* backend) {
      MOPPE_PROFILE_ZONE ("analyze_fractional_drainage");
      const TerrainGrid& grid = terrain.grid ();
      if (flood.source_grid != grid ||
          census.body.size () != grid.unique_size ())
        throw std::invalid_argument (
          "fractional drainage inputs do not share one terrain lattice");
      const float persistence_value =
        persistence.numerical_value_in (mp_units::one);
      if ((!previous_tangent.empty () &&
           previous_tangent.size () != grid.unique_size ()) ||
          !std::isfinite (persistence_value) || persistence_value < 0.0f ||
          persistence_value >= 1.0f)
        throw std::invalid_argument (
          "fractional drainage channel memory is invalid");

      TerrainLatticeDomain lattice (grid);
      RoutingSurface surface (lattice);
      const std::span<const float> levels = flood.water_level.values ();
      auto& elevations = spatial::get<routing_surface_elevation> (surface);
      for (std::size_t offset = 0; offset < lattice.size (); ++offset)
        elevations[offset] = RoutingSurfaceElevation (
          levels[offset] * grid.height_scale_m () *
          routing_surface_elevation[mp_units::si::metre]);

      // The established wet graph remains the authority for flat lake routes
      // and proven depression spills. D-infinity replaces only strict dry-land
      // descent inside this Orogeny-specific reading.
      const WetDrainageRouting wet =
        route_wet_drainage (terrain, flood, census);
      std::vector<FractionalFlowRoute> routes (lattice.size ());
      std::vector<DrainageDirection> directions (
        lattice.size (), 0.0f * drainage_direction[mp_units::angular::radian]);
      std::vector<slope_t> slopes (lattice.size (),
                                   0.0f * terrain_slope[mp_units::one]);

      if (backend) {
        backend->select_dry_routes (grid,
                                    levels,
                                    previous_tangent,
                                    persistence,
                                    flood.ocean,
                                    census.body,
                                    routes,
                                    directions,
                                    slopes);
      } else {
        const DInfinityStencil stencil (grid);
        for (std::size_t offset = 0; offset < lattice.size (); ++offset) {
          if (flood.ocean[offset] || census.body[offset] != LakeCensus::dry)
            continue;
          const CellIndex cell = lattice.index (offset);
          const RouteReading reading = d_infinity_route (
            surface,
            cell,
            previous_tangent.empty ()
              ? ChannelTangent (Vec3 () * channel_tangent[mp_units::one])
              : previous_tangent[offset],
            persistence,
            stencil);
          routes[offset] = reading.route;
          directions[offset] = reading.direction;
          slopes[offset] = reading.slope;
        }
      }

      for (std::size_t offset = 0; offset < lattice.size (); ++offset) {
        if (flood.ocean[offset])
          continue;
        const CellIndex cell = lattice.index (offset);
        RouteReading reading { .route = routes[offset],
                               .direction = directions[offset],
                               .slope = slopes[offset] };
        if (reading.route.empty () && wet.receiver[offset] != cell) {
          const CellIndex receiver = wet.receiver[offset];
          const auto delta = receiver_offset (cell, receiver, lattice);
          reading.route = single_route (receiver, delta[0], delta[1], grid);
          reading.direction = direction_for_offset (delta[0], delta[1], grid);
          reading.slope = wet.slope[offset] * terrain_slope[mp_units::one];
        }
        routes[offset] = reading.route;
        directions[offset] = reading.direction;
        slopes[offset] = reading.slope;
      }

      const float cell_area_m2 = square_meters_value (grid.cell_area ());
      std::vector<FractionalContributingArea> areas (
        lattice.size (),
        cell_area_m2 * fractional_contributing_area[mp_units::si::metre *
                                                    mp_units::si::metre]);
      std::vector<ChannelTangent> tangents (
        lattice.size (), Vec3 () * channel_tangent[mp_units::one]);
      std::vector<ChannelAreaFlux> area_fluxes (
        lattice.size (),
        Vec3 () * channel_area_flux[mp_units::si::metre * mp_units::si::metre]);
      std::vector<CellIndex> order =
        accumulate (lattice, routes, directions, areas, tangents, area_fluxes);
      FractionalDrainage result (FractionalFlowDomain (
        std::move (lattice), std::move (routes), std::move (order)));
      spatial::get<drainage_direction> (result) = std::move (directions);
      spatial::get<terrain_slope> (result) = std::move (slopes);
      spatial::get<fractional_contributing_area> (result) = std::move (areas);
      spatial::get<channel_tangent> (result) = std::move (tangents);
      spatial::get<channel_area_flux> (result) = std::move (area_fluxes);
      return result;
    }
  }

  FractionalDrainage
  analyze_fractional_drainage (const TerrainView& terrain,
                               const FloodField& flood,
                               const LakeCensus& census,
                               std::span<const ChannelTangent> previous_tangent,
                               ChannelPersistence persistence) {
    return analyze_fractional_drainage_impl (
      terrain, flood, census, previous_tangent, persistence, nullptr);
  }

  FractionalDrainage
  analyze_fractional_drainage (const TerrainView& terrain,
                               const FloodField& flood,
                               const LakeCensus& census,
                               std::span<const ChannelTangent> previous_tangent,
                               ChannelPersistence persistence,
                               const FractionalRouteBackend& backend) {
    return analyze_fractional_drainage_impl (
      terrain, flood, census, previous_tangent, persistence, &backend);
  }
}
