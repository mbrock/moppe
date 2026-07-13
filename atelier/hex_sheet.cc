#include "atelier/hex_sheet.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    constexpr Duration fixed_step = (1.0f / 120.0f) * s;
    constexpr Duration partition_interval = 0.75f * s;
    constexpr Duration growth_interval = 0.15f * s;
    constexpr Duration growth_duration = 6.0f * s;
    constexpr std::size_t maximum_generation = 3;

    std::size_t base_offset (GridCell cell) {
      return cell.row * hex_sheet_columns + cell.column;
    }

    GridCell base_cell (std::size_t offset) {
      return { offset % hex_sheet_columns, offset / hex_sheet_columns };
    }

    std::vector<HexCell> initial_cells () {
      std::vector<HexCell> cells;
      cells.reserve (hex_sheet_base_cell_count);
      for (std::size_t base = 0; base < hex_sheet_base_cell_count; ++base) {
        cells.push_back (
          { base_cell (base), 0.0f * m, 0.0f * m, 1.0f, 0, 1, 0, false });
      }
      return cells;
    }

    std::uint32_t mix_bits (std::uint32_t bits) {
      bits += 0x9e3779b9U;
      bits ^= bits >> 16;
      bits *= 0x7feb352dU;
      bits ^= bits >> 15;
      bits *= 0x846ca68bU;
      bits ^= bits >> 16;
      return bits;
    }

    bool same_base (GridCell first, GridCell second) {
      return first.column == second.column && first.row == second.row;
    }

    bool is_corner (GridCell cell) {
      const bool edge_column =
        cell.column == 0 || cell.column + 1 == hex_sheet_columns;
      const bool edge_row = cell.row == 0 || cell.row + 1 == hex_sheet_rows;
      return edge_column && edge_row;
    }

    HexSite materialize (const HexCell& cell) {
      constexpr Length child_radius = 0.45f * puck_radius;
      const Real progress = std::clamp (cell.growth, 0.0f, 1.0f);
      const Real eased = progress * progress * (3.0f - 2.0f * progress);
      const Length radius =
        cell.generation == 0
          ? puck_radius
          : child_radius + eased * (puck_radius - child_radius);
      const Length half_spacing = 0.5f * std::numbers::sqrt3_v<Real> * radius;
      const Real angle = Real (cell.axis) * std::numbers::pi_v<Real> / 3.0f;
      const Real direction = cell.positive ? 1.0f : -1.0f;
      const Length branch_across =
        cell.generation == 0 ? 0.0f * m
                             : direction * half_spacing * std::sin (angle);
      const Length branch_away =
        cell.generation == 0 ? 0.0f * m
                             : direction * half_spacing * std::cos (angle);
      return { cell.base,
               cell.origin_across + branch_across,
               cell.origin_away + branch_away,
               radius,
               Real (cell.generation),
               cell.lineage };
    }

    Real unit_hash (std::uint32_t bits) {
      return Real (mix_bits (bits) & 0x00ffffffU) / Real (0x01000000U);
    }

    Real signed_noise (const HexSite& site) {
      const std::size_t base = base_offset (site.base);
      return 2.0f * unit_hash (static_cast<std::uint32_t> (131 * base +
                                                           17 * site.lineage)) -
             1.0f;
    }

    Real material_seed (const HexSite& site) {
      const std::size_t base = base_offset (site.base);
      return unit_hash (
        static_cast<std::uint32_t> (131 * base + 17 * site.lineage + 0x51edU));
    }

    struct IntrinsicPoint {
      Real across;
      Real away;
    };

    IntrinsicPoint intrinsic_point (const HexSite& site) {
      const Real row_offset = site.base.row % 2 == 0 ? 0.0f : 0.5f;
      const Real across_pitch =
        std::numbers::sqrt3_v<Real> * in_metres (puck_radius);
      const Real away_pitch = 1.5f * in_metres (puck_radius);
      return {
        .across = (Real (site.base.column) + row_offset) * across_pitch +
                  in_metres (site.offset_across),
        .away =
          Real (site.base.row) * away_pitch + in_metres (site.offset_away),
      };
    }

    Real periodic_delta (Real delta, Real period) {
      return delta - std::round (delta / period) * period;
    }

    bool sites_touch (const HexSite& first,
                      const HexSite& second,
                      SheetBoundary boundary) {
      const IntrinsicPoint a = intrinsic_point (first);
      const IntrinsicPoint b = intrinsic_point (second);
      Real across = b.across - a.across;
      Real away = b.away - a.away;
      if (boundary == SheetBoundary::periodic) {
        const Real across_period = Real (hex_sheet_columns) *
                                   std::numbers::sqrt3_v<Real> *
                                   in_metres (puck_radius);
        const Real away_period =
          Real (hex_sheet_rows) * 1.5f * in_metres (puck_radius);
        across = periodic_delta (across, across_period);
        away = periodic_delta (away, away_period);
      }
      const Real distance_squared = across * across + away * away;
      const Real contact = 0.89f * in_metres (first.radius + second.radius);
      return distance_squared > 0.000001f &&
             distance_squared <= contact * contact;
    }

    DeformationReading deformation_of (const auto& tile) {
      const auto displacement = get<normal_displacement> (tile);
      const auto total_difference = fold_neighbourhood (
        tile,
        displacement - displacement,
        [displacement] (auto total, const auto& neighbour, auto influence) {
          const NormalDisplacement difference =
            get<normal_displacement> (neighbour) - displacement;
          const auto magnitude =
            difference < 0.0f * m ? -difference : difference;
          return total + influence * magnitude;
        });
      const Real reading =
        std::min (1.0f, total_difference.numerical_value_in (m) / 0.45f);
      return DeformationReading (reading * mp_units::one);
    }
  }

  HexSheetTopology::HexSheetTopology (SheetBoundary boundary,
                                      std::vector<HexCell> cells)
      : m_boundary (boundary), m_cells (std::move (cells)) {
    if (m_cells.empty ())
      m_cells = initial_cells ();
    build_sites ();
    build_neighbourhoods ();
  }

  void HexSheetTopology::build_sites () {
    m_sites.reserve (m_cells.size ());
    for (const HexCell& cell : m_cells)
      m_sites.push_back (materialize (cell));
  }

  void HexSheetTopology::build_neighbourhoods () {
    m_neighbours.resize (m_sites.size ());
    for (TileId first = 0; first < m_sites.size (); ++first) {
      for (TileId second = first + 1; second < m_sites.size (); ++second) {
        if (!sites_touch (m_sites[first], m_sites[second], m_boundary))
          continue;
        m_neighbours[first].push_back (second);
        m_neighbours[second].push_back (first);
      }
    }
  }

  TileId HexSheetTopology::tile_id (GridCell base) const {
    for (TileId id = 0; id < m_sites.size (); ++id) {
      if (same_base (m_sites[id].base, base))
        return id;
    }
    throw std::out_of_range ("Hex base has no live cells");
  }

  bool HexSheetTopology::is_anchor (TileId id) const {
    if (m_boundary != SheetBoundary::open)
      return false;
    return is_corner (site (id).base);
  }

  bool HexSheetTopology::is_valid () const {
    for (TileId id = 0; id < size (); ++id) {
      if (neighbours (id).empty ())
        return false;
      for (TileId adjacent : neighbours (id)) {
        if (adjacent >= size () || adjacent == id)
          return false;
        const auto& reverse = neighbours (adjacent);
        if (std::ranges::find (reverse, id) == reverse.end ())
          return false;
      }
    }
    return true;
  }

  HexSheet::HexSheet (SheetBoundary boundary)
      : m_cells (initial_cells ()),
        m_tiles (HexSheetTopology (boundary, m_cells)),
        m_next_tiles (HexSheetTopology (boundary, m_cells)) {
    if (!topology_is_valid ())
      throw std::runtime_error ("The hex sheet topology is inconsistent");
    initialize_noise_drive ();
  }

  void HexSheet::initialize_noise_drive () {
    auto& drive = get<normal_acceleration> (m_tiles);
    auto& next_drive = get<normal_acceleration> (m_next_tiles);
    constexpr auto amplitude = 3.2f * m / (s * s);
    for (TileId id = 0; id < m_tiles.size (); ++id) {
      drive[id] = topology ().is_anchor (id)
                    ? 0.0f * m / (s * s)
                    : signed_noise (topology ().site (id)) * amplitude;
      next_drive[id] = drive[id];
    }
  }

  bool HexSheet::topology_is_valid () const {
    return topology ().is_valid ();
  }

  std::size_t HexSheet::split_cell_count () const {
    return m_cells.size () - hex_sheet_base_cell_count;
  }

  void HexSheet::advance (Duration elapsed) {
    if (elapsed < m_elapsed)
      *this = HexSheet (topology ().boundary ());
    m_accumulator += elapsed - m_elapsed;
    m_elapsed = elapsed;
    while (m_accumulator >= fixed_step) {
      m_simulated += fixed_step;
      update_partition (m_simulated);
      update_growth (m_simulated);
      step (fixed_step);
      m_accumulator -= fixed_step;
    }
  }

  void HexSheet::update_partition (Duration elapsed) {
    const auto target_event =
      static_cast<std::size_t> (elapsed.numerical_value_in (s) /
                                partition_interval.numerical_value_in (s));
    while (m_partition_event < target_event) {
      ++m_partition_event;
      std::vector<HexCell> next = m_cells;
      bool changed = false;

      for (std::size_t split = 0; split < 2; ++split) {
        std::vector<std::size_t> eligible;
        for (std::size_t index = 0; index < next.size (); ++index) {
          const HexCell& cell = next[index];
          if (cell.growth >= 1.0f && cell.generation < maximum_generation &&
              !is_corner (cell.base))
            eligible.push_back (index);
        }
        if (eligible.empty ())
          break;

        // Every third event prefers the deepest mature lineage. This makes
        // recursive division visible without letting it consume the sheet.
        if (m_partition_event % 3 == 0) {
          const auto deepest = std::ranges::max (
            eligible | std::views::transform ([&] (std::size_t index) {
              return next[index].generation;
            }));
          std::erase_if (eligible, [&] (std::size_t index) {
            return next[index].generation != deepest;
          });
        }

        const auto bits =
          static_cast<std::uint32_t> (m_partition_event * 31 + split * 101);
        const std::size_t index = eligible[mix_bits (bits) % eligible.size ()];
        const HexCell parent = next[index];
        const HexSite parent_site = materialize (parent);
        const std::size_t axis =
          (parent.axis + 1 + mix_bits (bits + 53) % 2) % 3;
        HexCell first { parent.base,
                        parent_site.offset_across,
                        parent_site.offset_away,
                        0.0f,
                        parent.generation + 1,
                        2 * parent.lineage,
                        axis,
                        false };
        HexCell second = first;
        second.lineage += 1;
        second.positive = true;
        next[index] = first;
        next.insert (next.begin () + static_cast<std::ptrdiff_t> (index + 1),
                     second);
        changed = true;
      }
      if (changed)
        rebuild (std::move (next));
    }
  }

  void HexSheet::update_growth (Duration elapsed) {
    const auto target_event = static_cast<std::size_t> (
      elapsed.numerical_value_in (s) / growth_interval.numerical_value_in (s));
    while (m_growth_event < target_event) {
      ++m_growth_event;
      std::vector<HexCell> next = m_cells;
      bool changed = false;
      const Real increment = Real (growth_interval / growth_duration);
      for (HexCell& cell : next) {
        if (cell.generation > 0 && cell.growth < 1.0f) {
          cell.growth = std::min (1.0f, cell.growth + increment);
          changed = true;
        }
      }
      if (changed)
        rebuild (std::move (next));
    }
  }

  void HexSheet::rebuild (std::vector<HexCell> cells) {
    const HexSheetTopology old_topology = topology ();
    const auto old_displacement = displacements ();
    const auto old_velocity = velocities ();
    const auto old_drive = drives ();
    HexSheetTopology new_topology (old_topology.boundary (), cells);
    TileState new_tiles (new_topology);
    TileState new_next_tiles (new_topology);
    auto& new_displacement = get<normal_displacement> (new_tiles);
    auto& new_velocity = get<normal_velocity> (new_tiles);
    auto& new_drive = get<normal_acceleration> (new_tiles);

    for (TileId target = 0; target < new_topology.size (); ++target) {
      const HexSite& site = new_topology.site (target);
      TileId source = old_topology.size ();
      for (TileId candidate = 0; candidate < old_topology.size ();
           ++candidate) {
        const HexSite& old_site = old_topology.site (candidate);
        if (same_base (old_site.base, site.base) &&
            (old_site.lineage == site.lineage ||
             old_site.lineage == site.lineage / 2)) {
          source = candidate;
          if (old_site.lineage == site.lineage)
            break;
        }
      }
      if (source == old_topology.size ())
        throw std::logic_error ("A new hex cell has no parent lineage");
      new_displacement[target] = old_displacement[source];
      new_velocity[target] = old_velocity[source];
      new_drive[target] = old_drive[source];
      if (new_topology.site (target).lineage !=
          old_topology.site (source).lineage)
        new_drive[target] = signed_noise (site) * 3.2f * m / (s * s);
      if (new_topology.is_anchor (target)) {
        new_displacement[target] =
          new_displacement[target] - new_displacement[target];
        new_velocity[target] = new_velocity[target] - new_velocity[target];
        new_drive[target] = new_drive[target] - new_drive[target];
      }
    }

    get<normal_displacement> (new_next_tiles) = new_displacement;
    get<normal_velocity> (new_next_tiles) = new_velocity;
    get<normal_acceleration> (new_next_tiles) = new_drive;
    m_cells = std::move (cells);
    m_tiles = std::move (new_tiles);
    m_next_tiles = std::move (new_next_tiles);
    if (!topology_is_valid ())
      throw std::runtime_error ("Adaptive hex partition is inconsistent");
  }

  void HexSheet::step (Duration dt) {
    constexpr auto stiffness = 2.3f / (s * s);
    constexpr auto coupling = 31.2f / (s * s);
    constexpr auto damping = 1.25f / s;
    constexpr AngularRate breathing_rate =
      0.07f * angular::revolution / si::second;
    const Real breath = angular::sin (breathing_rate * m_simulated)
                          .numerical_value_in (mp_units::one);
    const Real seconds = m_simulated.numerical_value_in (s);
    extend_into (m_next_tiles, m_tiles, [=] (const auto& tile) {
      const auto displacement = get<normal_displacement> (tile);
      const auto velocity = get<normal_velocity> (tile);
      const auto drive = get<normal_acceleration> (tile);
      if (tile.domain ().is_anchor (tile.index ()))
        return bundle_values (
          displacement - displacement, velocity - velocity, drive - drive);
      const IntrinsicPoint point =
        intrinsic_point (tile.domain ().site (tile.index ()));
      const Real wave =
        std::sin (1.20f * point.across + 0.45f * point.away - 2.2f * seconds) +
        0.45f * std::sin (-0.55f * point.across + 1.35f * point.away -
                          1.5f * seconds);
      const auto wave_drive = wave * 1.8f * m / (s * s);
      const auto acceleration =
        coupling * laplacian<normal_displacement> (tile) -
        stiffness * displacement - damping * velocity + breath * drive +
        wave_drive;
      const auto next_velocity = velocity + acceleration * dt;
      return bundle_values (
        displacement + next_velocity * dt, next_velocity, drive);
    });

    auto& displacement = get<normal_displacement> (m_tiles);
    auto& velocity = get<normal_velocity> (m_tiles);
    auto& next_displacement = get<normal_displacement> (m_next_tiles);
    auto& next_velocity = get<normal_velocity> (m_next_tiles);
    velocity.swap (next_velocity);
    displacement.swap (next_displacement);
  }

  std::vector<TileLeaf> HexSheet::leaves () const {
    std::vector<TileLeaf> result;
    result.reserve (topology ().size ());
    for (TileId id = 0; id < topology ().size (); ++id) {
      const BundleFocus tile (m_tiles, id);
      const HexSite& site = topology ().site (id);
      result.push_back ({ site,
                          get<normal_displacement> (tile),
                          deformation_of (tile),
                          material_seed (site) });
    }
    return result;
  }
}
