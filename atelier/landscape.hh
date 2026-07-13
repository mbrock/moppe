#pragma once

#include "atelier/space.hh"

#include <array>
#include <cstddef>
#include <vector>

// A small, closed cellular landscape.  Its authoritative structure is a
// periodic six-neighbour graph; the toroidal shell is only one embedding of
// that structure.  The first developmental operation refines one cell into a
// seven-cell patch and later melds it back into its parent.

namespace atelier {
  inline constexpr std::size_t landscape_columns = 30;
  inline constexpr std::size_t landscape_rows = 14;
  inline constexpr std::size_t landscape_tile_count =
    landscape_columns * landscape_rows;

  using TileId = std::size_t;
  using NormalVelocity = quantity<si::metre / si::second, Real>;

  inline constexpr Length puck_radius = 0.39f * si::metre;

  struct GridCell {
    std::size_t column;
    std::size_t row;
  };

  // The immutable combinatorial law of the closed sheet.  Regular topology
  // is calculated rather than copied into every tile's state.
  class PeriodicHexTopology {
  public:
    [[nodiscard]] GridCell cell (TileId id) const;
    [[nodiscard]] TileId tile_id (GridCell cell) const;
    [[nodiscard]] TileId neighbour (TileId id, std::size_t side) const;
    [[nodiscard]] std::array<TileId, 6> neighbours (TileId id) const;
    [[nodiscard]] bool is_valid () const;
  };

  // One present leaf of the partition, still expressed entirely in the
  // landscape's intrinsic coordinates.  It has no opinion about whether the
  // closed graph will be shown as a toroidal shell or a repeating flat sheet.
  struct TileLeaf {
    GridCell cell;
    Length offset_across;
    Length offset_away;
    Length radius;
    Length normal_displacement;
    DeformationReading deformation;
    Real generation;
    Real material_seed;
  };

  class Landscape {
  public:
    Landscape ();

    void advance (Duration elapsed);

    [[nodiscard]] const PeriodicHexTopology& topology () const {
      return m_topology;
    }

    [[nodiscard]] const std::array<Length, landscape_tile_count>&
    displacements () const {
      return m_displacement;
    }

    [[nodiscard]] const std::array<NormalVelocity, landscape_tile_count>&
    velocities () const {
      return m_velocity;
    }

    [[nodiscard]] bool topology_is_valid () const;
    [[nodiscard]] bool is_refined () const;
    [[nodiscard]] Real refinement () const;
    [[nodiscard]] std::vector<TileLeaf> leaves () const;

  private:
    void step (Duration dt);
    void begin_refinement ();
    void update_development (Duration elapsed);

    PeriodicHexTopology m_topology;
    std::array<Length, landscape_tile_count> m_displacement {};
    std::array<NormalVelocity, landscape_tile_count> m_velocity {};
    std::array<Length, landscape_tile_count> m_next_displacement;
    std::array<NormalVelocity, landscape_tile_count> m_next_velocity;
    Duration m_elapsed {};
    Duration m_simulated {};
    Duration m_accumulator {};
    std::size_t m_cycle = 0;
    bool m_refined = false;
    Real m_refinement = 0;
  };
}
