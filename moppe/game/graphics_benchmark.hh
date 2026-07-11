#ifndef MOPPE_GAME_GRAPHICS_BENCHMARK_HH
#define MOPPE_GAME_GRAPHICS_BENCHMARK_HH

#include <moppe/game/graphics_settings.hh>
#include <moppe/platform/platform.hh>

#include <cmath>
#include <cstdint>

namespace moppe::game {
  inline constexpr float GRAPHICS_BENCHMARK_DT = 1.0f / 120.0f;

  inline uint32_t gray_code (uint32_t index) {
    return index ^ (index >> 1);
  }

  inline platform::ControlState benchmark_input (int frame) {
    const float t = frame * GRAPHICS_BENCHMARK_DT;
    platform::ControlState input;
    input.drive = 1.0f;
    input.steer = 0.30f * std::sin (t * 0.7f) + 0.12f * std::sin (t * 1.9f);
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

  inline void apply_hot_graphics_mask (GraphicsSettings& settings,
                                       uint32_t mask) {
    int bit = 0;
    for (const GraphicsFeature* feature : graphics_features)
      if (feature->hot)
        feature->set (settings, (mask & (1u << bit++)) != 0);
  }
}

#endif
