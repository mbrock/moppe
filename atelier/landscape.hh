#pragma once

#include "atelier/bundle.hh"
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
  inline constexpr Length puck_radius = 0.39f * si::metre;

  struct GridCell {
    std::size_t column;
    std::size_t row;
  };

  // The immutable combinatorial law of the closed sheet.  Regular topology
  // is calculated rather than copied into every tile's state.
  class PeriodicHexTopology {
  public:
    using index_type = TileId;

    [[nodiscard]] constexpr std::size_t size () const {
      return landscape_tile_count;
    }

    [[nodiscard]] constexpr std::size_t offset (TileId id) const {
      return id;
    }

    [[nodiscard]] constexpr TileId index (std::size_t offset) const {
      return offset;
    }

    [[nodiscard]] GridCell cell (TileId id) const;
    [[nodiscard]] TileId tile_id (GridCell cell) const;
    [[nodiscard]] TileId neighbour (TileId id, std::size_t side) const;
    [[nodiscard]] std::array<TileId, 6> neighbours (TileId id) const;

    template <typename Visitor>
    void visit_neighbourhood (TileId id, Visitor&& visitor) const {
      for (std::size_t side = 0; side < 6; ++side)
        visitor (neighbour (id, side), Real { 1 });
    }

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
    NormalDisplacement normal_displacement;
    DeformationReading deformation;
    Real generation;
    Real material_seed;
  };

  class Landscape {
  public:
    using TileState = Bundle<PeriodicHexTopology,
                             NormalDisplacement,
                             NormalVelocity,
                             NormalAcceleration>;

    Landscape ();

    void advance (Duration elapsed);

    [[nodiscard]] const PeriodicHexTopology& topology () const {
      return m_tiles.domain ();
    }

    [[nodiscard]] const std::vector<NormalDisplacement>&
    displacements () const {
      return get<normal_displacement> (m_tiles);
    }

    [[nodiscard]] const std::vector<NormalVelocity>& velocities () const {
      return get<normal_velocity> (m_tiles);
    }

    [[nodiscard]] const std::vector<NormalAcceleration>& drives () const {
      return get<normal_acceleration> (m_tiles);
    }

    [[nodiscard]] bool topology_is_valid () const;
    [[nodiscard]] bool is_refined () const;
    [[nodiscard]] Real refinement () const;
    [[nodiscard]] std::vector<TileLeaf> leaves () const;

  private:
    void step (Duration dt);
    void initialize_noise_drive ();
    void begin_refinement ();
    void update_development (Duration elapsed);

    TileState m_tiles;
    TileState m_next_tiles;
    Duration m_elapsed {};
    Duration m_simulated {};
    Duration m_accumulator {};
    std::size_t m_cycle = 0;
    bool m_refined = false;
    Real m_refinement = 0;
  };
}
