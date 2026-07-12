#ifndef MOPPE_TERRAIN_TYPES_HH
#define MOPPE_TERRAIN_TYPES_HH

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

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

  template <typename Tag, std::integral Rep>
  struct DiscreteValue {
    Rep value;

    constexpr DiscreteValue () noexcept : value (0) {}
    constexpr DiscreteValue (Rep value) noexcept : value (value) {}
    constexpr operator Rep () const noexcept {
      return value;
    }

    constexpr DiscreteValue& operator++ () noexcept {
      ++value;
      return *this;
    }

    constexpr DiscreteValue& operator+= (Rep amount) noexcept {
      value += amount;
      return *this;
    }

    friend constexpr bool operator== (DiscreteValue, DiscreteValue) = default;

    template <std::integral I>
    friend constexpr bool operator== (DiscreteValue count, I value) noexcept {
      return count.value == static_cast<Rep> (value);
    }
    template <std::integral I>
    friend constexpr bool operator== (I value, DiscreteValue count) noexcept {
      return static_cast<Rep> (value) == count.value;
    }

    template <typename OtherTag, std::integral OtherRep>
      requires (!std::same_as<Tag, OtherTag>)
    friend bool operator== (DiscreteValue,
                            DiscreteValue<OtherTag, OtherRep>) = delete;
  };

  struct CellCountTag;
  struct ReachCountTag;
  struct IterationCountTag;
  struct DropletCountTag;
  struct BatchSizeTag;
  struct StepCountTag;
  struct EventCountTag;
  struct SeparationCellCountTag;

  using CellCount = DiscreteValue<CellCountTag, std::size_t>;
  using ReachCount = DiscreteValue<ReachCountTag, std::size_t>;
  using IterationCount = DiscreteValue<IterationCountTag, int>;
  using DropletCount = DiscreteValue<DropletCountTag, int>;
  using BatchSize = DiscreteValue<BatchSizeTag, int>;
  using StepCount = DiscreteValue<StepCountTag, int>;
  using EventCount = DiscreteValue<EventCountTag, std::uint64_t>;
  using SeparationCellCount =
    DiscreteValue<SeparationCellCountTag, std::size_t>;
}

#endif
