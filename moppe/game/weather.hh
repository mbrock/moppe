#ifndef MOPPE_GAME_WEATHER_HH
#define MOPPE_GAME_WEATHER_HH

// The weather: spells of one kind of sky after another -- clear, fair,
// overcast, valley mist, drizzle, rain, and now and then a thunderstorm,
// most often by night -- each passing into the next over an hour or two of
// the clock. The spells follow from the world's seed and the hour counted
// from the first midnight, so two players who share a clock share the
// weather. MOPPE_WEATHER names one kind to hold instead (clear, fair,
// overcast, mist, drizzle, rain, storm); `changing` is the default. The
// renderer draws the mist and the rain; the game shapes the sky, fog, and
// light around them.

#include <moppe/color.hh>
#include <moppe/game/game_clock.hh>
#include <moppe/gfx/math.hh>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace moppe::game {
  using cloud_cover_t = mp_units::quantity<cloud_cover[mp_units::one], float>;

  struct Weather {
    // The cloud cover drifts between these.
    cloud_cover_t cloud_floor = 0.0f * cloud_cover[one];
    cloud_cover_t cloud_ceiling = 1.0f * cloud_cover[one];
    // Multiplies the world's fog scale.
    scale_factor_t fog_density = 1.0f * scale_factor[one];
    DisplayColor fog_tint { 0.0f, 0.0f, 0.0f };
    // How far the fog greys toward the tint.
    blend_weight_t fog_tint_amount = 0.0f * blend_weight[one];
    // Direct sun through the overcast, and the sky's diffuse fill.
    scale_factor_t sunlight = 1.0f * scale_factor[one];
    scale_factor_t skylight = 1.0f * scale_factor[one];
    proportion_t mist = 0.0f * proportion[one]; // valley mist
    // 1 a drizzle, 2.5 a downpour.
    scale_factor_t rain = 0.0f * scale_factor[one];
    proportion_t lightning = 0.0f * proportion[one]; // how often it strikes
  };

  enum class SkyKind : std::uint8_t {
    clear,
    fair,
    overcast,
    mist,
    drizzle,
    rain,
    storm
  };

  const char* sky_kind_name (SkyKind kind);
  std::optional<SkyKind> sky_kind_named (std::string_view name);
  Weather weather_of (SkyKind kind);
  Weather
  mix_weather (const Weather& from, const Weather& to, blend_weight_t t);

  // The spells, laid out from hour zero as far as they are asked for.
  class WeatherTimeline {
  public:
    explicit WeatherTimeline (std::uint32_t seed);

    Weather at (clock_time_t hours);
    // The kind of the spell under way at this hour.
    SkyKind kind_at (clock_time_t hours);

  private:
    struct Spell {
      clock_time_t start;
      SkyKind kind;
    };
    std::size_t spell_at (clock_time_t hours);

    std::uint64_t m_seed;
    std::vector<Spell> m_spells;
  };

  // How brightly lightning lights the world at this moment.
  proportion_t lightning_flash (proportion_t lightning, play_time_t now);
}

#endif
