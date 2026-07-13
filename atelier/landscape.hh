#pragma once

#include "atelier/matrix.hh"

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

  struct GridCell {
    std::size_t column;
    std::size_t row;
  };

  struct LandscapeTile {
    GridCell cell;
    std::array<TileId, 6> neighbours;
    Length displacement;
    quantity<si::metre / si::second, Real> velocity;
  };

  // A renderer-facing reading of one current leaf of the partition.  Stable
  // tile identity and topology remain in Landscape; this is deliberately a
  // disposable view of its present embedding.
  struct TileVisual {
    Matrix place_in_world;
    Real stress;
    Real generation;
  };

  class Landscape {
  public:
    Landscape ();

    void advance (Duration elapsed);

    [[nodiscard]] const std::array<LandscapeTile, landscape_tile_count>&
    tiles () const {
      return m_tiles;
    }

    [[nodiscard]] bool topology_is_valid () const;
    [[nodiscard]] bool is_refined () const;
    [[nodiscard]] Real refinement () const;
    [[nodiscard]] std::vector<TileVisual> visuals () const;

  private:
    void step (Duration dt);
    void begin_refinement ();
    void update_development (Duration elapsed);

    std::array<LandscapeTile, landscape_tile_count> m_tiles;
    std::array<Length, landscape_tile_count> m_next_displacement;
    std::array<quantity<si::metre / si::second, Real>, landscape_tile_count>
      m_next_velocity;
    Duration m_elapsed {};
    Duration m_simulated {};
    Duration m_accumulator {};
    std::size_t m_cycle = 0;
    bool m_refined = false;
    Real m_refinement = 0;
  };
}
