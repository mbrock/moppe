#include "atelier/hex_sheet.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    constexpr Duration fixed_step = (1.0f / 120.0f) * s;
    constexpr Duration partition_interval = 0.75f * s;

    std::size_t base_offset (GridCell cell) {
      return cell.row * hex_sheet_columns + cell.column;
    }

    GridCell base_cell (std::size_t offset) {
      return { offset % hex_sheet_columns, offset / hex_sheet_columns };
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

    Real unit_hash (std::uint32_t bits) {
      return Real (mix_bits (bits) & 0x00ffffffU) / Real (0x01000000U);
    }

    Real signed_noise (const HexSite& site) {
      const std::size_t base = base_offset (site.base);
      return 2.0f * unit_hash (
                      static_cast<std::uint32_t> (8 * base + site.variant)) -
             1.0f;
    }

    Real material_seed (const HexSite& site) {
      const std::size_t base = base_offset (site.base);
      return unit_hash (
        static_cast<std::uint32_t> (8 * base + site.variant + 0x51edU));
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
                                      std::vector<bool> refined)
      : m_boundary (boundary), m_refined (std::move (refined)) {
    if (m_refined.empty ())
      m_refined.resize (hex_sheet_base_cell_count, false);
    if (m_refined.size () != hex_sheet_base_cell_count)
      throw std::invalid_argument ("A hex sheet partition has the wrong size");
    build_sites ();
    build_neighbourhoods ();
  }

  void HexSheetTopology::build_sites () {
    m_first_tile.resize (hex_sheet_base_cell_count);
    m_sites.reserve (hex_sheet_base_cell_count +
                     6 * std::ranges::count (m_refined, true));
    constexpr Length child_radius =
      puck_radius / (1.0f + std::numbers::sqrt3_v<Real>);
    constexpr Length child_orbit = std::numbers::sqrt3_v<Real> * child_radius;
    for (std::size_t base = 0; base < hex_sheet_base_cell_count; ++base) {
      const GridCell cell = base_cell (base);
      m_first_tile[base] = m_sites.size ();
      if (!m_refined[base]) {
        m_sites.push_back ({ cell, 0.0f * m, 0.0f * m, puck_radius, 0, 0 });
        continue;
      }
      m_sites.push_back ({ cell, 0.0f * m, 0.0f * m, child_radius, 1, 0 });
      for (std::size_t side = 0; side < 6; ++side) {
        const Real angle =
          (Real (side) + 0.5f) * 2.0f * std::numbers::pi_v<Real> / 6.0f;
        m_sites.push_back ({ cell,
                             child_orbit * std::sin (angle),
                             child_orbit * std::cos (angle),
                             child_radius,
                             1,
                             side + 1 });
      }
    }
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

  TileId HexSheetTopology::tile_id (GridCell base, std::size_t variant) const {
    const std::size_t offset = base_offset (base);
    if (offset >= m_first_tile.size () || variant > (m_refined[offset] ? 6 : 0))
      throw std::out_of_range ("Hex site does not exist in this partition");
    return m_first_tile[offset] + variant;
  }

  bool HexSheetTopology::base_is_refined (GridCell base) const {
    return m_refined[base_offset (base)];
  }

  bool HexSheetTopology::is_anchor (TileId id) const {
    if (m_boundary != SheetBoundary::open)
      return false;
    const GridCell base = site (id).base;
    const bool edge_column =
      base.column == 0 || base.column + 1 == hex_sheet_columns;
    const bool edge_row = base.row == 0 || base.row + 1 == hex_sheet_rows;
    return edge_column && edge_row;
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
      : m_refined (hex_sheet_base_cell_count, false),
        m_tiles (HexSheetTopology (boundary, m_refined)),
        m_next_tiles (HexSheetTopology (boundary, m_refined)) {
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

  std::size_t HexSheet::refined_cell_count () const {
    return std::ranges::count (m_refined, true);
  }

  void HexSheet::advance (Duration elapsed) {
    if (elapsed < m_elapsed)
      *this = HexSheet (topology ().boundary ());
    m_accumulator += elapsed - m_elapsed;
    m_elapsed = elapsed;
    while (m_accumulator >= fixed_step) {
      m_simulated += fixed_step;
      update_partition (m_simulated);
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
      std::vector<bool> next = m_refined;
      std::size_t merged = hex_sheet_base_cell_count;

      if (m_partition_event >= 4) {
        std::vector<std::size_t> refined;
        for (std::size_t base = 0; base < next.size (); ++base) {
          const GridCell cell = base_cell (base);
          const bool corner =
            (cell.column == 0 || cell.column + 1 == hex_sheet_columns) &&
            (cell.row == 0 || cell.row + 1 == hex_sheet_rows);
          if (next[base] && !corner)
            refined.push_back (base);
        }
        if (!refined.empty ()) {
          const std::size_t choice =
            mix_bits (static_cast<std::uint32_t> (m_partition_event * 17)) %
            refined.size ();
          merged = refined[choice];
          next[merged] = false;
        }
      }

      for (std::size_t split = 0; split < 2; ++split) {
        std::size_t candidate = mix_bits (static_cast<std::uint32_t> (
                                  m_partition_event * 31 + split * 101)) %
                                hex_sheet_base_cell_count;
        for (std::size_t probe = 0; probe < next.size (); ++probe) {
          const std::size_t base = (candidate + probe) % next.size ();
          const GridCell cell = base_cell (base);
          const bool corner =
            (cell.column == 0 || cell.column + 1 == hex_sheet_columns) &&
            (cell.row == 0 || cell.row + 1 == hex_sheet_rows);
          if (!next[base] && base != merged && !corner) {
            next[base] = true;
            break;
          }
        }
      }
      repartition (std::move (next));
    }
  }

  void HexSheet::repartition (std::vector<bool> refined) {
    if (refined == m_refined)
      return;

    const HexSheetTopology old_topology = topology ();
    const auto old_displacement = displacements ();
    const auto old_velocity = velocities ();
    const auto old_drive = drives ();
    HexSheetTopology new_topology (old_topology.boundary (), refined);
    TileState new_tiles (new_topology);
    TileState new_next_tiles (new_topology);
    auto& new_displacement = get<normal_displacement> (new_tiles);
    auto& new_velocity = get<normal_velocity> (new_tiles);
    auto& new_drive = get<normal_acceleration> (new_tiles);

    for (std::size_t base = 0; base < hex_sheet_base_cell_count; ++base) {
      const GridCell cell = base_cell (base);
      const bool was_refined = m_refined[base];
      const bool is_refined = refined[base];
      const std::size_t new_count = is_refined ? 7 : 1;
      for (std::size_t variant = 0; variant < new_count; ++variant) {
        const TileId target = new_topology.tile_id (cell, variant);
        if (was_refined == is_refined) {
          const TileId source = old_topology.tile_id (cell, variant);
          new_displacement[target] = old_displacement[source];
          new_velocity[target] = old_velocity[source];
          new_drive[target] = old_drive[source];
        } else if (is_refined) {
          const TileId source = old_topology.tile_id (cell);
          new_displacement[target] = old_displacement[source];
          new_velocity[target] = old_velocity[source];
          new_drive[target] = old_drive[source];
        } else {
          const TileId first = old_topology.tile_id (cell);
          auto displacement = old_displacement[first] - old_displacement[first];
          auto velocity = old_velocity[first] - old_velocity[first];
          auto drive = old_drive[first] - old_drive[first];
          for (std::size_t child = 0; child < 7; ++child) {
            const TileId source = old_topology.tile_id (cell, child);
            displacement += old_displacement[source] / 7.0f;
            velocity += old_velocity[source] / 7.0f;
            drive += old_drive[source] / 7.0f;
          }
          new_displacement[target] = displacement;
          new_velocity[target] = velocity;
          new_drive[target] = drive;
        }
        if (new_topology.is_anchor (target)) {
          new_displacement[target] =
            new_displacement[target] - new_displacement[target];
          new_velocity[target] = new_velocity[target] - new_velocity[target];
          new_drive[target] = new_drive[target] - new_drive[target];
        }
      }
    }

    get<normal_displacement> (new_next_tiles) = new_displacement;
    get<normal_velocity> (new_next_tiles) = new_velocity;
    get<normal_acceleration> (new_next_tiles) = new_drive;
    m_refined = std::move (refined);
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
    extend_into (m_next_tiles, m_tiles, [=] (const auto& tile) {
      const auto displacement = get<normal_displacement> (tile);
      const auto velocity = get<normal_velocity> (tile);
      const auto drive = get<normal_acceleration> (tile);
      if (tile.domain ().is_anchor (tile.index ()))
        return bundle_values (
          displacement - displacement, velocity - velocity, drive - drive);
      const auto acceleration =
        coupling * laplacian<normal_displacement> (tile) -
        stiffness * displacement - damping * velocity + breath * drive;
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
