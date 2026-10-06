#ifndef MOPPE_GAME_WEATHER_HH
#define MOPPE_GAME_WEATHER_HH

// Authored weather: a mood chosen for the valley rather than a simulation.
// MOPPE_WEATHER picks one -- clear (the drifting sky the game always had),
// mist (a grey, low sky and mist lying in the valleys), or drizzle (heavier
// overcast, mist, and a fine rain). The renderer draws the mist and the
// rain; the game shapes the sky, fog, and light around them.

#include <moppe/color.hh>
#include <moppe/environment.hh>

#include <string_view>

namespace moppe::game {
  struct Weather {
    float cloud_floor = 0.0f; // the least cloud cover the sky drifts to
    float fog_density = 1.0f; // multiplies the world's fog scale
    DisplayColor fog_tint { 0.0f, 0.0f, 0.0f };
    float fog_tint_amount = 0.0f; // how far the fog greys toward the tint
    float sunlight = 1.0f;        // direct sun through the overcast
    float skylight = 1.0f;        // the sky's diffuse fill
    float mist = 0.0f;            // valley mist, 0..1
    float rain = 0.0f;            // drizzle, 0..1
  };

  inline Weather weather_named (std::string_view name) {
    if (name == "mist")
      return { .cloud_floor = 0.72f,
               .fog_density = 2.6f,
               .fog_tint = { 0.74f, 0.78f, 0.79f },
               .fog_tint_amount = 0.7f,
               .sunlight = 0.5f,
               .skylight = 1.7f,
               .mist = 1.0f };
    if (name == "drizzle")
      return { .cloud_floor = 0.94f,
               .fog_density = 2.2f,
               .fog_tint = { 0.64f, 0.68f, 0.70f },
               .fog_tint_amount = 0.85f,
               .sunlight = 0.25f,
               .skylight = 1.9f,
               .mist = 0.6f,
               .rain = 1.0f };
    return {};
  }

  inline const Weather& current_weather () {
    static const Weather weather = [] {
      const char* name = moppe::environment ("MOPPE_WEATHER");
      return weather_named (name ? name : "clear");
    }();
    return weather;
  }
}

#endif
