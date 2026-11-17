#include <moppe/environment.hh>
#include <moppe/game/sky_clock.hh>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace moppe::game {
  namespace {
    SkyClock::Day plan_day () {
      SkyClock::Day day;
      const char* minutes = moppe::environment ("MOPPE_DAY");
      const char* hour = moppe::environment ("MOPPE_CLOCK");
      if (hour)
        day.first_hour =
          std::clamp ((float)::atof (hour), 0.0f, 24.0f) * si::hour;
      const quantity<si::minute, float> length =
        (minutes ? (float)::atof (minutes) : 48.0f) * si::minute;
      if (length > 0.0f * si::minute)
        day.pace = 24.0 / (60.0 * length.numerical_value_in (si::minute)) *
                   (si::hour / si::second);
      day.turning = day.pace > day_pace_t {} || hour;
      return day;
    }
  }

  SkyClock::SkyClock (proportion_t sun_height)
      : m_sun_height (sun_height), m_day (plan_day ()) {
    m_sky = m_almanac.held (m_sun_height);
    if (const char* name = moppe::environment ("MOPPE_WEATHER");
        name && std::string_view (name) != "changing") {
      m_held_sky = sky_kind_named (name);
      if (!m_held_sky)
        std::cerr << "moppe: weather: no kind named " << name
                  << "; the weather changes" << std::endl;
    }
  }

  void SkyClock::begin (GameLogicState& logic, std::uint32_t seed) {
    m_weather_timeline.emplace (seed);
    m_logged_sky.reset ();
    logic.m_day_hours = clock_time (
      (m_day.first_hour ? *m_day.first_hour
                        : m_almanac.clock_for_sun_height (m_sun_height))
        .numerical_value_in (si::hour));
    read_sky (logic);
    if (m_day.turning)
      std::cerr << "moppe: the day begins at "
                << m_sky.clock.numerical_value_in (si::hour) << " h and takes "
                << (m_day.pace > day_pace_t {}
                      ? 24.0 / (60.0 * m_day.pace.numerical_value_in (
                                         si::hour / si::second))
                      : 0.0)
                << " minutes" << std::endl;
  }

  void SkyClock::update (GameLogicState& logic, seconds_t total_time) {
    // The phase of a slow drift of the clouds, at `rate` a second.
    const auto phase = [total_time] (per_second_t rate) {
      return (total_time * rate).numerical_value_in (one);
    };
    cloud_cover_t cloudiness =
      (std::sin (phase (0.0003f / si::second)) * 0.4f + 0.5f +
       0.3f * std::pow (std::sin (phase (0.0008f / si::second)), 2.0f) +
       std::sin (phase (0.02f / si::second)) * 0.05f) *
      cloud_cover[one];
    const Weather weather = read_weather (logic);
    logic.m_weather = weather;
    cloudiness =
      std::clamp (cloudiness, weather.cloud_floor, weather.cloud_ceiling);
    logic.m_cloudiness = cloudiness;

    // Fog stays mostly sky-blue. Directional warmth is added in the
    // shaders only when looking toward the sun.
    read_sky (logic);
    // By night the haze keeps the sky's own dark blue, and an overcast
    // greys only as far as there is light to grey it.
    const scale_factor_t lit =
      (0.12f + 0.88f * m_sky.daylight.numerical_value_in (one)) *
      scale_factor[one];
    logic.m_fog = mix_display (
      mix_display (m_sky.horizon,
                   scale_display (DisplayColor (0.90f, 0.94f, 1.0f), lit),
                   0.18f * blend_weight[one]),
      scale_display (weather.fog_tint, lit),
      weather.fog_tint_amount);
  }

  Weather SkyClock::read_weather (const GameLogicState& logic) {
    if (m_held_sky)
      return weather_of (*m_held_sky);
    if (!m_day.turning || !m_weather_timeline)
      return {};
    const SkyKind kind = m_weather_timeline->kind_at (logic.m_day_hours);
    if (kind != m_logged_sky) {
      m_logged_sky = kind;
      std::cerr << "moppe: weather: " << sky_kind_name (kind) << " from "
                << std::fmod (hours_since_midnight (logic.m_day_hours), 24.0)
                << " h" << std::endl;
    }
    return m_weather_timeline->at (logic.m_day_hours);
  }

  void SkyClock::read_sky (const GameLogicState& logic) {
    m_sky = m_day.turning ? m_almanac.at (logic.m_day_hours)
                          : m_almanac.held (m_sun_height);
  }
}
