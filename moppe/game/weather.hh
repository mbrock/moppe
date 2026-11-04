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

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace moppe::game {
  struct Weather {
    // The cloud cover drifts between these.
    float cloud_floor = 0.0f;
    float cloud_ceiling = 1.0f;
    float fog_density = 1.0f; // multiplies the world's fog scale
    DisplayColor fog_tint { 0.0f, 0.0f, 0.0f };
    float fog_tint_amount = 0.0f; // how far the fog greys toward the tint
    float sunlight = 1.0f;        // direct sun through the overcast
    float skylight = 1.0f;        // the sky's diffuse fill
    float mist = 0.0f;            // valley mist, 0..1
    float rain = 0.0f;            // 1 a drizzle, 2.5 a downpour
    float lightning = 0.0f;       // how often it strikes, 0..1
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
  Weather mix_weather (const Weather& from, const Weather& to, float t);

  // The spells, laid out from hour zero as far as they are asked for.
  class WeatherTimeline {
  public:
    explicit WeatherTimeline (std::uint32_t seed);

    Weather at (double hours);
    // The kind of the spell under way at this hour.
    SkyKind kind_at (double hours);

  private:
    struct Spell {
      double start;
      SkyKind kind;
    };
    std::size_t spell_at (double hours);

    std::uint64_t m_seed;
    std::vector<Spell> m_spells;
  };

  // How brightly lightning lights the world at this moment, 0..1.
  float lightning_flash (float lightning, double seconds);
}

#endif
