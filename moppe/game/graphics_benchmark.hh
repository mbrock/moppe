#ifndef MOPPE_GAME_GRAPHICS_BENCHMARK_HH
#define MOPPE_GAME_GRAPHICS_BENCHMARK_HH

#include <moppe/game/graphics_settings.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/partition.hh>

#include <array>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <string_view>

namespace moppe::game {
  inline constexpr float GRAPHICS_BENCHMARK_DT = 1.0f / 120.0f;

  template <typename P>
  concept GraphicsFeaturePartition = requires {
    typename P::block_type;
  } && Partition<P, GraphicsFeatureId, typename P::block_type>;

  template <typename P>
  concept NamedFiniteGraphicsFeaturePartition =
    GraphicsFeaturePartition<P> && requires { P::blocks; } &&
    std::ranges::forward_range<decltype (P::blocks)> &&
    std::same_as<std::ranges::range_value_t<decltype (P::blocks)>,
                 typename P::block_type> &&
    requires (typename P::block_type block) {
      { P::name (block) } -> std::convertible_to<std::string_view>;
    };

  inline uint32_t gray_code (uint32_t index) {
    return index ^ (index >> 1);
  }

  // The ordinary riding benchmark preserves the four distinctions that have
  // shown measurable cost, including rivers, and deliberately identifies the
  // small effects.
  // Its block type is local to this particular quotient.
  struct RidingGraphicsPartition {
    enum class Block {
      ocean,
      rivers,
      bloom,
      auto_exposure,
      small_effects,
    };

    using block_type = Block;
    inline static constexpr std::array blocks {
      Block::ocean,         Block::rivers,        Block::bloom,
      Block::auto_exposure, Block::small_effects,
    };

    constexpr Block operator() (GraphicsFeatureId feature) const {
      switch (feature) {
      case GraphicsFeatureId::ocean:
        return Block::ocean;
      case GraphicsFeatureId::river_ribbons:
        return Block::rivers;
      case GraphicsFeatureId::bloom:
        return Block::bloom;
      case GraphicsFeatureId::auto_exposure:
        return Block::auto_exposure;
      default:
        return Block::small_effects;
      }
    }

    static constexpr std::string_view name (Block block) {
      switch (block) {
      case Block::ocean:
        return "ocean";
      case Block::rivers:
        return "rivers";
      case Block::bloom:
        return "bloom";
      case Block::auto_exposure:
        return "auto-exposure";
      case Block::small_effects:
        return "small-effects";
      }
    }
  };

  static_assert (NamedFiniteGraphicsFeaturePartition<RidingGraphicsPartition>);

  inline constexpr RidingGraphicsPartition graphics_benchmark_partition;

  inline constexpr bool
  graphics_benchmark_includes (const GraphicsFeature& feature) {
    // Debug views may be hot without being part of the ordinary riding
    // presentation whose costs this benchmark partitions.
    return feature.hot && feature.id != GraphicsFeatureId::terrain_topology;
  }

  inline constexpr bool graphics_benchmark_partition_covers_features () {
    for (const GraphicsFeature* feature : graphics_features) {
      if (!graphics_benchmark_includes (*feature))
        continue;
      const auto block = graphics_benchmark_partition (feature->id);
      bool found = false;
      for (const auto candidate : RidingGraphicsPartition::blocks)
        found = found || candidate == block;
      if (!found)
        return false;
    }
    return true;
  }

  static_assert (graphics_benchmark_partition_covers_features ());

  inline constexpr int graphics_benchmark_dimension_count () {
    return static_cast<int> (RidingGraphicsPartition::blocks.size ());
  }

  inline InputFrame benchmark_input (int frame) {
    const float t = frame * GRAPHICS_BENCHMARK_DT;
    InputFrame input;
    input.drive = 1.0f;
    input.turn = 0.30f * std::sin (t * 0.7f) + 0.12f * std::sin (t * 1.9f);
    input.boost = std::fmod (t, 4.0f) < 0.8f ? 1.0f : 0.0f;
    return input;
  }

  inline int hot_graphics_feature_count () {
    int count = 0;
    for (const GraphicsFeature* feature : graphics_features)
      if (feature->hot)
        ++count;
    return count;
  }

  inline uint32_t apply_graphics_benchmark_mask (GraphicsSettings& settings,
                                                 uint32_t partition_mask) {
    uint32_t feature_mask = 0;
    int feature_bit = 0;
    for (const GraphicsFeature* feature : graphics_features) {
      if (!graphics_benchmark_includes (*feature))
        continue;
      const auto block = graphics_benchmark_partition (feature->id);
      int block_bit = 0;
      while (RidingGraphicsPartition::blocks[block_bit] != block)
        ++block_bit;
      const bool enabled = (partition_mask & (1u << block_bit)) != 0;
      feature->set (settings, enabled);
      if (enabled)
        feature_mask |= 1u << feature_bit;
      ++feature_bit;
    }
    return feature_mask;
  }
}

#endif
