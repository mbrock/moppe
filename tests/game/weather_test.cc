#include <moppe/game/weather.hh>

#include <tests/test.hh>

#include <cmath>

using namespace moppe;
using namespace moppe::game;

namespace {
  bool weather_is_wet (SkyKind kind) {
    return kind == SkyKind::drizzle || kind == SkyKind::rain ||
           kind == SkyKind::storm;
  }
}

MOPPE_TEST (the_weather_turns_gradually_and_rains_now_and_then) {
  for (std::uint32_t seed : { 123u, 7u, 4096u }) {
    WeatherTimeline timeline (seed);
    // The opening is authored under the first morning's fair sky.
    MOPPE_CHECK (timeline.kind_at (9.25) == SkyKind::fair);
    double wet = 0.0, storm = 0.0, night_storm = 0.0, hours = 0.0;
    Weather before = timeline.at (0.0);
    constexpr double step = 0.05;
    for (double h = step; h < 24.0 * 200.0; h += step) {
      const Weather now = timeline.at (h);
      // No kind of sky comes on all at once.
      MOPPE_CHECK (std::abs (now.rain - before.rain) < 0.15f);
      MOPPE_CHECK (std::abs (now.mist - before.mist) < 0.1f);
      MOPPE_CHECK (std::abs (now.sunlight - before.sunlight) < 0.05f);
      before = now;
      const SkyKind kind = timeline.kind_at (h);
      const double clock = std::fmod (h, 24.0);
      hours += step;
      wet += weather_is_wet (kind) ? step : 0.0;
      if (kind == SkyKind::storm) {
        storm += step;
        night_storm += clock >= 19.0 || clock < 5.0 ? step : 0.0;
      }
    }
    MOPPE_CHECK (wet / hours > 0.03 && wet / hours < 0.2);
    MOPPE_CHECK (storm > 0.0 && night_storm / storm > 0.5);
  }
  // The spells follow from the seed alone, however they are asked for.
  WeatherTimeline forward (123), backward (123);
  MOPPE_CHECK (backward.at (24.0 * 30.0).rain >= 0.0f);
  for (double h = 0.0; h < 24.0 * 30.0; h += 1.7)
    MOPPE_CHECK (forward.at (h).cloud_floor == backward.at (h).cloud_floor);
}

MOPPE_TEST (lightning_strikes_only_in_a_storm) {
  float calm = 0.0f, storm = 0.0f;
  for (double t = 0.0; t < 120.0; t += 1.0 / 60.0) {
    calm = std::max (calm, lightning_flash (0.0f, t));
    storm = std::max (storm, lightning_flash (1.0f, t));
  }
  MOPPE_CHECK (calm == 0.0f);
  MOPPE_CHECK (storm > 0.5f);
}
