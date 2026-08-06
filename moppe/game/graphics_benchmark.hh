#ifndef MOPPE_GAME_GRAPHICS_BENCHMARK_HH
#define MOPPE_GAME_GRAPHICS_BENCHMARK_HH

#include <moppe/game/graphics_settings.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/partition.hh>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace moppe::game {
  inline constexpr float GRAPHICS_BENCHMARK_DT = 1.0f / 120.0f;

  enum class GraphicsBenchmarkPartition {
    Standard,
    Detailed,
  };

  inline const char*
  graphics_benchmark_partition_name (GraphicsBenchmarkPartition partition) {
    return partition == GraphicsBenchmarkPartition::Detailed ? "detailed"
                                                             : "standard";
  }

  inline bool
  parse_graphics_benchmark_partition (std::string_view name,
                                      GraphicsBenchmarkPartition& partition) {
    if (name == "standard")
      partition = GraphicsBenchmarkPartition::Standard;
    else if (name == "detailed")
      partition = GraphicsBenchmarkPartition::Detailed;
    else
      return false;
    return true;
  }

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

  enum class RidingGraphicsBlock {
    forest,
    undergrowth,
    water,
    post,
    other_features,
    ocean,
    waterfalls,
    bloom,
    auto_exposure,
  };

  inline constexpr std::string_view
  riding_graphics_block_name (RidingGraphicsBlock block) {
    switch (block) {
    case RidingGraphicsBlock::forest:
      return "forest";
    case RidingGraphicsBlock::undergrowth:
      return "undergrowth";
    case RidingGraphicsBlock::water:
      return "water";
    case RidingGraphicsBlock::post:
      return "post";
    case RidingGraphicsBlock::other_features:
      return "other-features";
    case RidingGraphicsBlock::ocean:
      return "ocean";
    case RidingGraphicsBlock::waterfalls:
      return "waterfalls";
    case RidingGraphicsBlock::bloom:
      return "bloom";
    case RidingGraphicsBlock::auto_exposure:
      return "auto-exposure";
    }
    return "invalid";
  }

  // The default quotient answers broad subsystem questions in 32
  // configurations. Forest and undergrowth remain separate because they are
  // independent geometry amplifiers; water and post each travel together.
  struct RidingGraphicsPartition {
    using block_type = RidingGraphicsBlock;
    inline static constexpr std::array blocks {
      RidingGraphicsBlock::forest,         RidingGraphicsBlock::undergrowth,
      RidingGraphicsBlock::water,          RidingGraphicsBlock::post,
      RidingGraphicsBlock::other_features,
    };

    constexpr RidingGraphicsBlock operator() (GraphicsFeatureId feature) const {
      switch (feature) {
      case GraphicsFeatureId::forest:
        return RidingGraphicsBlock::forest;
      case GraphicsFeatureId::undergrowth:
        return RidingGraphicsBlock::undergrowth;
      case GraphicsFeatureId::ocean:
      case GraphicsFeatureId::waterfall_curtains:
        return RidingGraphicsBlock::water;
      case GraphicsFeatureId::bloom:
      case GraphicsFeatureId::auto_exposure:
        return RidingGraphicsBlock::post;
      default:
        return RidingGraphicsBlock::other_features;
      }
    }

    static constexpr std::string_view name (RidingGraphicsBlock block) {
      return riding_graphics_block_name (block);
    }
  };

  // The opt-in refinement separates water and post components while keeping
  // the heterogeneous remainder grouped. It is deliberately called detailed,
  // not exhaustive: fifteen raw Boolean features would make 32768 runs.
  struct DetailedRidingGraphicsPartition {
    using block_type = RidingGraphicsBlock;
    inline static constexpr std::array blocks {
      RidingGraphicsBlock::forest,         RidingGraphicsBlock::ocean,
      RidingGraphicsBlock::waterfalls,     RidingGraphicsBlock::bloom,
      RidingGraphicsBlock::auto_exposure,  RidingGraphicsBlock::undergrowth,
      RidingGraphicsBlock::other_features,
    };

    constexpr RidingGraphicsBlock operator() (GraphicsFeatureId feature) const {
      switch (feature) {
      case GraphicsFeatureId::forest:
        return RidingGraphicsBlock::forest;
      case GraphicsFeatureId::ocean:
        return RidingGraphicsBlock::ocean;
      case GraphicsFeatureId::waterfall_curtains:
        return RidingGraphicsBlock::waterfalls;
      case GraphicsFeatureId::bloom:
        return RidingGraphicsBlock::bloom;
      case GraphicsFeatureId::auto_exposure:
        return RidingGraphicsBlock::auto_exposure;
      case GraphicsFeatureId::undergrowth:
        return RidingGraphicsBlock::undergrowth;
      default:
        return RidingGraphicsBlock::other_features;
      }
    }

    static constexpr std::string_view name (RidingGraphicsBlock block) {
      return riding_graphics_block_name (block);
    }
  };

  static_assert (NamedFiniteGraphicsFeaturePartition<RidingGraphicsPartition>);
  static_assert (
    NamedFiniteGraphicsFeaturePartition<DetailedRidingGraphicsPartition>);

  inline constexpr RidingGraphicsPartition graphics_benchmark_partition;
  inline constexpr DetailedRidingGraphicsPartition
    detailed_graphics_benchmark_partition;

  inline constexpr bool
  graphics_benchmark_includes (const GraphicsFeature& feature) {
    // Debug views may be hot without being part of the ordinary riding
    // presentation whose costs this benchmark partitions.
    return feature.hot && feature.id != GraphicsFeatureId::terrain_topology;
  }

  template <NamedFiniteGraphicsFeaturePartition P>
  inline constexpr bool graphics_benchmark_partition_covers_features (P p) {
    for (const GraphicsFeature* feature : graphics_features) {
      if (!graphics_benchmark_includes (*feature))
        continue;
      const auto block = p (feature->id);
      bool found = false;
      for (const auto candidate : P::blocks)
        found = found || candidate == block;
      if (!found)
        return false;
    }
    return true;
  }

  static_assert (graphics_benchmark_partition_covers_features (
    graphics_benchmark_partition));
  static_assert (graphics_benchmark_partition_covers_features (
    detailed_graphics_benchmark_partition));

  inline int
  graphics_benchmark_dimension_count (GraphicsBenchmarkPartition partition) {
    return partition == GraphicsBenchmarkPartition::Detailed
             ? static_cast<int> (
                 DetailedRidingGraphicsPartition::blocks.size ())
             : static_cast<int> (RidingGraphicsPartition::blocks.size ());
  }

  inline std::string_view
  graphics_benchmark_block_name (GraphicsBenchmarkPartition partition,
                                 int bit) {
    if (partition == GraphicsBenchmarkPartition::Detailed)
      return DetailedRidingGraphicsPartition::name (
        DetailedRidingGraphicsPartition::blocks[bit]);
    return RidingGraphicsPartition::name (RidingGraphicsPartition::blocks[bit]);
  }

  inline std::string
  graphics_benchmark_block_names (GraphicsBenchmarkPartition partition) {
    std::string names;
    for (int bit = 0; bit < graphics_benchmark_dimension_count (partition);
         ++bit) {
      if (!names.empty ())
        names += ',';
      names += graphics_benchmark_block_name (partition, bit);
    }
    return names;
  }

  inline InputFrame benchmark_input (int frame) {
    const float t = frame * GRAPHICS_BENCHMARK_DT;
    InputFrame input;
    input.drive = 1.0f;
    input.turn = 0.30f * std::sin (t * 0.7f) + 0.12f * std::sin (t * 1.9f);
    input.boost = std::fmod (t, 4.0f) < 0.8f ? 1.0f : 0.0f;
    return input;
  }

  // The renderer-free schedule for one graphics benchmark.  It materializes
  // its prelude and replay tapes independently: the latter starts again at
  // logical frame zero for every graphics configuration.  The owner chooses
  // when to checkpoint or restore a session and when to reset renderer
  // history at the returned boundaries.
  class GraphicsBenchmarkReplay {
  public:
    struct Config {
      int prelude_frames = 480;
      int settle_frames = 30;
      int measured_frames = 120;
      GraphicsBenchmarkPartition partition =
        GraphicsBenchmarkPartition::Standard;
    };

    struct Frame {
      InputFrame input;
      int epoch = 0;
      uint32_t partition_mask = 0;
      int logical_frame = 0;
      bool measured = false;
      bool prelude = false;
    };

    enum class Boundary {
      none,
      prelude_complete,
      epoch_complete,
      complete,
    };

    explicit GraphicsBenchmarkReplay (Config config) : m_config (config) {
      if (m_config.prelude_frames < 1 || m_config.settle_frames < 1 ||
          m_config.measured_frames < 1)
        throw std::invalid_argument (
          "graphics benchmark frame counts must be positive");
      m_prelude_tape = make_tape (m_config.prelude_frames);
      m_replay_tape =
        make_tape (m_config.settle_frames + m_config.measured_frames);
    }

    std::span<const InputFrame> prelude_tape () const noexcept {
      return { m_prelude_tape.data (), m_prelude_tape.size () };
    }

    std::span<const InputFrame> replay_tape () const noexcept {
      return { m_replay_tape.data (), m_replay_tape.size () };
    }

    int configuration_count () const noexcept {
      return 1 << graphics_benchmark_dimension_count (m_config.partition);
    }

    std::optional<Frame> current_frame () const {
      if (m_complete)
        return std::nullopt;
      if (!m_replaying)
        return Frame {
          m_prelude_tape[m_prelude_frame], 0, 0, m_prelude_frame, false, true
        };
      return Frame { m_replay_tape[m_logical_frame],
                     m_epoch,
                     gray_code (m_epoch),
                     m_logical_frame,
                     m_logical_frame >= m_config.settle_frames,
                     false };
    }

    Boundary finish_frame () {
      if (m_complete)
        return Boundary::complete;

      if (!m_replaying) {
        ++m_prelude_frame;
        if (m_prelude_frame < static_cast<int> (m_prelude_tape.size ()))
          return Boundary::none;
        m_replaying = true;
        return Boundary::prelude_complete;
      }

      ++m_logical_frame;
      if (m_logical_frame < static_cast<int> (m_replay_tape.size ()))
        return Boundary::none;
      m_logical_frame = 0;
      ++m_epoch;
      if (m_epoch < configuration_count ())
        return Boundary::epoch_complete;
      m_complete = true;
      return Boundary::complete;
    }

  private:
    static std::vector<InputFrame> make_tape (int frame_count) {
      std::vector<InputFrame> tape;
      tape.reserve (frame_count);
      for (int frame = 0; frame < frame_count; ++frame)
        tape.push_back (benchmark_input (frame));
      return tape;
    }

    Config m_config;
    std::vector<InputFrame> m_prelude_tape;
    std::vector<InputFrame> m_replay_tape;
    int m_prelude_frame = 0;
    int m_epoch = 0;
    int m_logical_frame = 0;
    bool m_replaying = false;
    bool m_complete = false;
  };

  inline int hot_graphics_feature_count () {
    int count = 0;
    for (const GraphicsFeature* feature : graphics_features)
      if (feature->hot)
        ++count;
    return count;
  }

  template <NamedFiniteGraphicsFeaturePartition P>
  inline uint32_t apply_graphics_benchmark_mask_with_partition (
    GraphicsSettings& settings, uint32_t partition_mask, P partition) {
    uint32_t feature_mask = 0;
    int feature_bit = 0;
    for (const GraphicsFeature* feature : graphics_features) {
      if (!graphics_benchmark_includes (*feature))
        continue;
      const auto block = partition (feature->id);
      int block_bit = 0;
      while (P::blocks[block_bit] != block)
        ++block_bit;
      const bool enabled = (partition_mask & (1u << block_bit)) != 0;
      feature->set (settings, enabled);
      if (enabled)
        feature_mask |= 1u << feature_bit;
      ++feature_bit;
    }
    return feature_mask;
  }

  inline uint32_t
  apply_graphics_benchmark_mask (GraphicsSettings& settings,
                                 uint32_t partition_mask,
                                 GraphicsBenchmarkPartition partition) {
    if (partition == GraphicsBenchmarkPartition::Detailed)
      return apply_graphics_benchmark_mask_with_partition (
        settings, partition_mask, detailed_graphics_benchmark_partition);
    return apply_graphics_benchmark_mask_with_partition (
      settings, partition_mask, graphics_benchmark_partition);
  }
}

#endif
