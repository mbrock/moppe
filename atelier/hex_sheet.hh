#pragma once

#include "atelier/bundle.hh"
#include "atelier/space.hh"

#include <cstddef>
#include <cstdint>
#include <vector>

// A small cellular sheet whose hexagonal adjacency is independent of its
// presentation. Its partition may close periodically or end at a finite
// boundary, and any base hexagon may divide into two smaller growing cells.

namespace atelier {
  inline constexpr std::size_t hex_sheet_columns = 30;
  inline constexpr std::size_t hex_sheet_rows = 14;
  inline constexpr std::size_t hex_sheet_base_cell_count =
    hex_sheet_columns * hex_sheet_rows;

  using TileId = std::size_t;
  inline constexpr Length puck_radius = 0.39f * si::metre;

  struct GridCell {
    std::size_t column;
    std::size_t row;
  };

  // One live leaf in a binary cell lineage. The root of each base cell has
  // lineage 1; dividing lineage n replaces it with 2n and 2n+1.
  struct HexCell {
    GridCell base;
    Length origin_across;
    Length origin_away;
    Real growth;
    std::size_t generation;
    std::uint64_t lineage;
    std::size_t axis;
    bool positive;
  };

  struct HexSite {
    GridCell base;
    Length offset_across;
    Length offset_away;
    Length radius;
    Real generation;
    std::uint64_t lineage;
  };

  enum class SheetBoundary { periodic, open };

  // The combinatorial law of one current partition. Adjacency is rebuilt from
  // the intrinsic geometry, so small cells naturally meet large neighbours.
  class HexSheetTopology {
  public:
    using index_type = TileId;

    explicit HexSheetTopology (SheetBoundary boundary = SheetBoundary::periodic,
                               std::vector<HexCell> cells = {});

    [[nodiscard]] std::size_t size () const {
      return m_sites.size ();
    }

    [[nodiscard]] constexpr std::size_t offset (TileId id) const {
      return id;
    }

    [[nodiscard]] constexpr TileId index (std::size_t offset) const {
      return offset;
    }

    [[nodiscard]] const HexSite& site (TileId id) const {
      return m_sites[id];
    }

    [[nodiscard]] TileId tile_id (GridCell base) const;
    [[nodiscard]] const std::vector<TileId>& neighbours (TileId id) const {
      return m_neighbours[id];
    }
    [[nodiscard]] SheetBoundary boundary () const {
      return m_boundary;
    }
    [[nodiscard]] bool is_anchor (TileId id) const;

    template <typename Visitor>
    void visit_neighbourhood (TileId id, Visitor&& visitor) const {
      // A coarse/fine interface changes degree but not the total coupling
      // assigned to a cell.
      const Real influence = 1.0f / Real (neighbours (id).size ());
      for (TileId adjacent : neighbours (id))
        visitor (adjacent, influence);
    }

    [[nodiscard]] bool is_valid () const;

  private:
    void build_sites ();
    void build_neighbourhoods ();

    SheetBoundary m_boundary;
    std::vector<HexCell> m_cells;
    std::vector<HexSite> m_sites;
    std::vector<std::vector<TileId>> m_neighbours;
  };

  struct TileLeaf {
    HexSite site;
    NormalDisplacement normal_displacement;
    DeformationReading deformation;
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
    [[nodiscard]] std::size_t split_cell_count () const;
    [[nodiscard]] std::vector<TileLeaf> leaves () const;

  private:
    void step (Duration dt);
    void initialize_noise_drive ();
    void update_partition (Duration elapsed);
    void update_growth (Duration elapsed);
    void rebuild (std::vector<HexCell> cells);

    std::vector<HexCell> m_cells;
    TileState m_tiles;
    TileState m_next_tiles;
    Duration m_elapsed {};
    Duration m_simulated {};
    Duration m_accumulator {};
    std::size_t m_growth_event = 0;
    std::size_t m_partition_event = 0;
  };
}
