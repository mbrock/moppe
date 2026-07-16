#ifndef MOPPE_TERRAIN_FRACTIONAL_DRAINAGE_HH
#define MOPPE_TERRAIN_FRACTIONAL_DRAINAGE_HH

#include <moppe/gfx/math.hh>
#include <moppe/spatial/bundle.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/terrain_view.hh>

#include <mp-units/systems/angular.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace moppe::terrain {
  struct FloodField;
  struct LakeCensus;

  // The unique samples of a materialized terrain. Unlike SurfaceDomain, this
  // domain omits the duplicated rendering seam and exposes the intrinsic
  // neighbourhood used by geological analyses.
  class TerrainLatticeDomain {
  public:
    using index_type = CellIndex;

    explicit TerrainLatticeDomain (TerrainGrid grid);

    const TerrainGrid& grid () const noexcept {
      return m_grid;
    }
    std::size_t width () const noexcept {
      return m_grid.unique_width ();
    }
    std::size_t height () const noexcept {
      return m_grid.unique_height ();
    }
    std::size_t size () const noexcept {
      return m_grid.unique_size ();
    }

    std::size_t offset (CellIndex index) const;
    CellIndex index (std::size_t offset) const;
    std::optional<CellIndex>
    neighbour (CellIndex index, int columns, int rows) const;

    template <typename Visitor>
    void visit_neighbourhood (CellIndex center, Visitor&& visitor) const {
      constexpr std::array offsets {
        std::array { 1, 0 },   std::array { 1, -1 }, std::array { 0, -1 },
        std::array { -1, -1 }, std::array { -1, 0 }, std::array { -1, 1 },
        std::array { 0, 1 },   std::array { 1, 1 }
      };
      for (const auto offset : offsets)
        if (const auto adjacent = neighbour (center, offset[0], offset[1]))
          visitor (*adjacent, 1.0f);
    }

    // D-infinity evaluates eight triangular facets. Each facet is formed by
    // the center, one cardinal neighbour, and an adjacent diagonal neighbour.
    template <typename Visitor>
    void visit_triangular_facets (CellIndex center, Visitor&& visitor) const {
      struct FacetOffsets {
        int cardinal_x;
        int cardinal_y;
        int diagonal_x;
        int diagonal_y;
      };
      constexpr std::array facets {
        FacetOffsets { 1, 0, 1, -1 },   FacetOffsets { 0, -1, 1, -1 },
        FacetOffsets { 0, -1, -1, -1 }, FacetOffsets { -1, 0, -1, -1 },
        FacetOffsets { -1, 0, -1, 1 },  FacetOffsets { 0, 1, -1, 1 },
        FacetOffsets { 0, 1, 1, 1 },    FacetOffsets { 1, 0, 1, 1 }
      };
      for (const FacetOffsets facet : facets) {
        const auto cardinal =
          neighbour (center, facet.cardinal_x, facet.cardinal_y);
        const auto diagonal =
          neighbour (center, facet.diagonal_x, facet.diagonal_y);
        if (cardinal && diagonal)
          visitor (*cardinal,
                   *diagonal,
                   facet.cardinal_x,
                   facet.cardinal_y,
                   facet.diagonal_x,
                   facet.diagonal_y);
      }
    }

  private:
    TerrainGrid m_grid;
  };

  inline constexpr struct flow_fraction
      : quantity_spec<mp_units::dimensionless, non_negative> {
  } flow_fraction;
  inline constexpr struct facet_coordinate
      : quantity_spec<mp_units::dimensionless, non_negative> {
  } facet_coordinate;
  inline constexpr struct drainage_direction
      : quantity_spec<mp_units::angular::angle, mp_units::is_kind> {
  } drainage_direction;
  inline constexpr struct fractional_contributing_area
      : quantity_spec<mp_units::isq::area, non_negative> {
  } fractional_contributing_area;
  // A horizontal, unit-length representative of the directed flow carried by
  // a lattice sample. Unlike drainage_direction, this can be averaged across
  // incoming arcs without an angular wrap discontinuity.
  inline constexpr struct channel_tangent
      : quantity_spec<mp_units::dimensionless,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } channel_tangent;
  // Contributing area carried in the channel-tangent direction. The arcs in
  // FractionalFlowDomain are the edge-like transport object; this cell-aligned
  // vector is their continuous summary for accumulation and later sampling.
  inline constexpr struct channel_area_flux
      : quantity_spec<mp_units::isq::area,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } channel_area_flux;
  inline constexpr struct channel_persistence
      : quantity_spec<mp_units::dimensionless,
                      non_negative,
                      mp_units::is_kind> {
  } channel_persistence;

  using FlowFraction = mp_units::quantity<flow_fraction[mp_units::one], float>;
  using FacetCoordinate =
    mp_units::quantity<facet_coordinate[mp_units::one], float>;
  using DrainageDirection =
    mp_units::quantity<drainage_direction[mp_units::angular::radian], float>;
  using FractionalContributingArea = mp_units::quantity<
    fractional_contributing_area[mp_units::si::metre * mp_units::si::metre],
    float>;
  using ChannelTangent =
    mp_units::quantity<channel_tangent[mp_units::one], Vec3>;
  using ChannelAreaFlux = mp_units::quantity<
    channel_area_flux[mp_units::si::metre * mp_units::si::metre],
    Vec3>;
  using ChannelPersistence =
    mp_units::quantity<channel_persistence[mp_units::one], float>;

  struct FractionalFlowArc {
    CellIndex receiver = no_cell;
    FlowFraction fraction = 0.0f * flow_fraction[mp_units::one];
  };

  struct FractionalFlowRoute {
    std::array<FractionalFlowArc, 2> arcs;
    std::uint8_t arc_count = 0;
    // The continuous flow ray meets the segment between the two receiver
    // samples at this affine coordinate. It is deliberately distinct from
    // the angular fraction used to partition contributing area.
    FacetCoordinate receiver_interpolation =
      0.0f * facet_coordinate[mp_units::one];
    meters_t run = 0.0f * mp_units::si::metre;

    bool empty () const noexcept {
      return arc_count == 0;
    }
  };

  // The directed topology induced by one routing analysis. The stored order
  // runs from donor-free sources toward outlets and is deterministic.
  class FractionalFlowDomain {
  public:
    using index_type = CellIndex;

    FractionalFlowDomain (TerrainLatticeDomain lattice,
                          std::vector<FractionalFlowRoute> routes,
                          std::vector<CellIndex> topological_order);

    const TerrainLatticeDomain& lattice () const noexcept {
      return m_lattice;
    }
    const TerrainGrid& grid () const noexcept {
      return m_lattice.grid ();
    }
    std::size_t size () const noexcept {
      return m_lattice.size ();
    }
    std::size_t offset (CellIndex index) const {
      return m_lattice.offset (index);
    }
    CellIndex index (std::size_t offset) const {
      return m_lattice.index (offset);
    }
    const FractionalFlowRoute& route (CellIndex index) const {
      return m_routes[m_lattice.offset (index)];
    }
    std::span<const CellIndex> topological_order () const noexcept {
      return m_topological_order;
    }

    template <typename Visitor>
    void visit_receivers (CellIndex index, Visitor&& visitor) const {
      const FractionalFlowRoute& flow = route (index);
      for (std::uint8_t arc = 0; arc < flow.arc_count; ++arc)
        visitor (flow.arcs[arc].receiver, flow.arcs[arc].fraction);
    }

  private:
    TerrainLatticeDomain m_lattice;
    std::vector<FractionalFlowRoute> m_routes;
    std::vector<CellIndex> m_topological_order;
  };

  using FractionalDrainage = spatial::Bundle<FractionalFlowDomain,
                                             DrainageDirection,
                                             slope_t,
                                             FractionalContributingArea,
                                             ChannelTangent,
                                             ChannelAreaFlux>;

  FractionalDrainage analyze_fractional_drainage (
    const TerrainView& terrain,
    const FloodField& flood,
    const LakeCensus& census,
    std::span<const ChannelTangent> previous_tangent = {},
    ChannelPersistence persistence = 0.0f * channel_persistence[mp_units::one]);

  // River extraction refined by a D-infinity reading of the same terrain.
  // The D8 graph keeps topological authority over reaches and waterfalls;
  // the fractional columns contribute smoothly varying contributing areas
  // and carved-valley knot tangents to the continuous alignments.
  RiverNetwork
  extract_river_network (const FloodField& flood,
                         const LakeCensus& census,
                         const DrainageGraph& drainage,
                         const FractionalDrainage& channels,
                         square_meters_t minimum_area,
                         const WaterfallParameters& waterfall_parameters = {});
}

#endif
