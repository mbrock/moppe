// The day and its weather over the active world: the clock's pace
// (MOPPE_DAY) and first hour (MOPPE_CLOCK), the sky read from the almanac
// for the hour, and the weather following the clock from spell to spell
// unless MOPPE_WEATHER holds one kind. A launch that names a sun height
// without asking for a day keeps the fixed sun it was made under.
#ifndef MOPPE_GAME_SKY_CLOCK_HH
#define MOPPE_GAME_SKY_CLOCK_HH

#include <moppe/game/daylight.hh>
#include <moppe/game/game_state.hh>
#include <moppe/game/weather.hh>

#include <cstdint>
#include <optional>

namespace moppe::game {
  class SkyClock {
  public:
    // `sun_height` is the authored sun a held sky keeps.
    explicit SkyClock (proportion_t sun_height);

    struct Day {
      // Hours of the clock per second of play.
      day_pace_t pace {};
      // Whether the sky follows the clock, or holds the authored sun.
      bool turning = false;
      std::optional<hours_t> first_hour;
    };

    const Day& day () const noexcept {
      return m_day;
    }

    // The sky as last read.
    const SkyReading& sky () const noexcept {
      return m_sky;
    }

    // A new world's day: its weather from its seed, and the clock set to
    // the first hour.
    void begin (GameLogicState& logic, std::uint32_t seed);

    // The clouds, the weather, the sky, and the fog at `total_time` into
    // play, written into the logic. Weather remains part of the world
    // while actors are paused.
    void update (GameLogicState& logic, seconds_t total_time);

  private:
    // The weather at the logic's hour. Composed views, whose day does not
    // turn, keep the drifting sky they were made under.
    Weather read_weather (const GameLogicState& logic);

    void read_sky (const GameLogicState& logic);

    proportion_t m_sun_height;
    Almanac m_almanac;
    Day m_day;
    std::optional<SkyKind> m_held_sky;
    std::optional<WeatherTimeline> m_weather_timeline;
    std::optional<SkyKind> m_logged_sky;
    SkyReading m_sky;
  };
}

#endif
