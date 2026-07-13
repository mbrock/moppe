#pragma once

#include "atelier/bundle.hh"
#include "atelier/space.hh"

#include <cstddef>
#include <optional>
#include <vector>

// A small cellular sheet whose hexagonal adjacency is independent of its
// presentation. It may close periodically or end at a finite boundary. The
// first developmental operation refines one cell into a seven-cell patch and
// later melds it back into its parent.

namespace atelier {
  inline constexpr std::size_t hex_sheet_columns = 30;
  inline constexpr std::size_t hex_sheet_rows = 14;
  inline constexpr std::size_t hex_sheet_tile_count =
    hex_sheet_columns * hex_sheet_rows;

  using TileId = std::size_t;
  inline constexpr Length puck_radius = 0.39f * si::metre;

  struct GridCell {
    std::size_t column;
    std::size_t row;
  };

  struct GridStep {
    int columns;
    int rows;
  };

  enum class SheetBoundary { periodic, open };

  // The immutable combinatorial law of the sheet. Regular topology is
  // calculated rather than copied into every tile's state.
  class HexSheetTopology {
  public:
    using index_type = TileId;

    explicit HexSheetTopology (SheetBoundary boundary = SheetBoundary::periodic)
        : m_boundary (boundary) {}

    [[nodiscard]] constexpr std::size_t size () const {
      return hex_sheet_tile_count;
    }

    [[nodiscard]] constexpr std::size_t offset (TileId id) const {
      return id;
    }

    [[nodiscard]] constexpr TileId index (std::size_t offset) const {
      return offset;
    }

    [[nodiscard]] GridCell cell (TileId id) const;
    [[nodiscard]] TileId tile_id (GridCell cell) const;
    [[nodiscard]] GridStep neighbour_step (TileId id, std::size_t side) const;
    [[nodiscard]] std::optional<TileId> neighbour (TileId id,
                                                   std::size_t side) const;
    [[nodiscard]] std::vector<TileId> neighbours (TileId id) const;
    [[nodiscard]] SheetBoundary boundary () const {
      return m_boundary;
    }
    [[nodiscard]] bool is_anchor (TileId id) const;

    template <typename Visitor>
    void visit_neighbourhood (TileId id, Visitor&& visitor) const {
      for (std::size_t side = 0; side < 6; ++side) {
        if (const auto adjacent = neighbour (id, side))
          visitor (*adjacent, Real { 1 });
      }
    }

    [[nodiscard]] bool is_valid () const;

  private:
    SheetBoundary m_boundary;
  };

  // One present leaf of the partition, still expressed entirely in the
  // sheet's intrinsic coordinates. It has no opinion about how that graph
  // will be embedded in the world.
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

  class HexSheet {
  public:
    using TileState = Bundle<HexSheetTopology,
                             NormalDisplacement,
                             NormalVelocity,
                             NormalAcceleration>;

    explicit HexSheet (SheetBoundary boundary = SheetBoundary::periodic);

    void advance (Duration elapsed);

    [[nodiscard]] const HexSheetTopology& topology () const {
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
