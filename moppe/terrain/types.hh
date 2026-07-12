#ifndef MOPPE_TERRAIN_TYPES_HH
#define MOPPE_TERRAIN_TYPES_HH

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <mp-units/framework.h>

namespace moppe::terrain {
  struct Seed {
    std::uint32_t value;

    friend constexpr bool operator== (Seed, Seed) = default;
  };

  constexpr Seed next_seed (Seed seed) {
    return Seed { seed.value + 1 };
  }

  struct SequenceOffset {
    std::uint64_t value;

    friend constexpr bool operator== (SequenceOffset, SequenceOffset) = default;
  };

  template <typename Tag>
  struct Identifier {
    std::uint32_t value;

    constexpr Identifier () noexcept : value (0) {}
    constexpr Identifier (std::uint32_t value) noexcept : value (value) {}

    // Hydrology arrays are dense and indexed by these values. This conversion
    // keeps indexing cheap; construction and assignment remain nominal.
    constexpr operator std::uint32_t () const noexcept {
      return value;
    }

    friend constexpr bool operator== (Identifier, Identifier) = default;
    template <std::integral I>
    friend constexpr bool operator== (Identifier id, I value) noexcept {
      return id.value == static_cast<std::uint32_t> (value);
    }
    template <std::integral I>
    friend constexpr bool operator== (I value, Identifier id) noexcept {
      return static_cast<std::uint32_t> (value) == id.value;
    }

    template <typename OtherTag>
      requires (!std::same_as<Tag, OtherTag>)
    friend bool operator== (Identifier, Identifier<OtherTag>) = delete;
  };

  struct CellIndexTag;
  struct WaterBodyIdTag;
  struct RiverReachIdTag;
  struct WaterfallIdTag;

  using CellIndex = Identifier<CellIndexTag>;
  using WaterBodyId = Identifier<WaterBodyIdTag>;
  using RiverReachId = Identifier<RiverReachIdTag>;
  using WaterfallId = Identifier<WaterfallIdTag>;

  inline constexpr CellIndex no_cell {
    std::numeric_limits<std::uint32_t>::max ()
  };
  inline constexpr WaterBodyId no_water_body {
    std::numeric_limits<std::uint32_t>::max ()
  };
  inline constexpr RiverReachId no_river_reach {
    std::numeric_limits<std::uint32_t>::max ()
  };
  inline constexpr WaterfallId no_waterfall {
    std::numeric_limits<std::uint32_t>::max ()
  };

  // Numbers of entities are quantities of dimension one.  Distinct kinds
  // preserve their algebra while preventing unrelated counts from mixing.
  inline constexpr struct cell_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } cell_count;
  inline constexpr struct reach_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } reach_count;
  inline constexpr struct iteration_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } iteration_count;
  inline constexpr struct droplet_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } droplet_count;
  inline constexpr struct batch_size
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } batch_size;
  inline constexpr struct step_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } step_count;
  inline constexpr struct event_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } event_count;
  inline constexpr struct separation_cell_count
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } separation_cell_count;

  using CellCount = mp_units::quantity<cell_count[mp_units::one], std::size_t>;
  using ReachCount =
    mp_units::quantity<reach_count[mp_units::one], std::size_t>;
  using IterationCount =
    mp_units::quantity<iteration_count[mp_units::one], int>;
  using DropletCount = mp_units::quantity<droplet_count[mp_units::one], int>;
  using BatchSize = mp_units::quantity<batch_size[mp_units::one], int>;
  using StepCount = mp_units::quantity<step_count[mp_units::one], int>;
  using EventCount =
    mp_units::quantity<event_count[mp_units::one], std::uint64_t>;
  using SeparationCellCount =
    mp_units::quantity<separation_cell_count[mp_units::one], std::size_t>;

  template <mp_units::Quantity Q>
  constexpr auto count_value (Q value) noexcept {
    return value.numerical_value_in (mp_units::one);
  }
}

#endif
