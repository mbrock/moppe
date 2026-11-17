#ifndef MOPPE_GAME_DAYLIGHT_HH
#define MOPPE_GAME_DAYLIGHT_HH

// The day: where the sun and moon stand at an hour of the clock, how far
// the stars have turned, and the light they give.
//
// The world lies at about 60 degrees north on the autumn equinox, with
// north along -Z and east along +X. That is not a free choice: it is the
// one latitude at which the sun the game was always lit by -- south-east,
// a little over twenty degrees up -- is an equinox morning sun, so the
// day passes through the authored light at a quarter past nine.

#include <moppe/color.hh>
#include <moppe/game/game_clock.hh>
#include <moppe/gfx/math.hh>

namespace moppe::game {
  // The sky at one moment.
  struct SkyReading {
    // Hours since midnight, 0..24, and whole days since the first.
    hours_t clock = 9.0f * si::hour;
    int day = 0;
    // Toward the sun and the moon; either may be below the horizon.
    direction_t sun = unit_direction (Vec3 (0, 1, 0));
    direction_t moon = unit_direction (Vec3 (0, -1, 0));
    // The celestial pole, and how far the stars have turned about it,
    // radians.
    direction_t pole = unit_direction (Vec3 (0, 1, 0));
    radians_t turn {};
    // How much of the moon's face is lit, 0 new to 1 full, and how brightly
    // it shines in this sky, 0 by day or when it is down.
    proportion_t moon_phase = 1.0f * proportion[one];
    proportion_t moonlight = 0.0f * proportion[one];
    // 0 at night to 1 by day, through the twilights.
    proportion_t daylight = 1.0f * proportion[one];
    // The key light the world is shaded and shadowed by: the sun, or the
    // moon once the sun is gone. It stays a little above the horizon.
    direction_t light = unit_direction (Vec3 (0, 1, 0));
    DisplayColor light_diffuse {};
    DisplayColor light_specular {};
    DisplayColor ambient {};
    DisplayColor horizon {};
    // The old art-direction reading of the sun's elevation: 0.5 at the
    // horizon, 1 overhead.
    proportion_t sun_height = 0.62f * proportion[one];
  };

  // The sky's light by the sine of the sun's elevation: 0 at night to 1 by
  // day, and how golden the low sun is.
  proportion_t daylight_for_elevation (float sine);
  proportion_t golden_light_for_elevation (float sine);

  // The fixed sun the game was always lit by, at the old `sun_height`:
  // south-east, by that height above or below the horizon.
  direction_t authored_sun_direction (proportion_t sun_height);

  class Almanac {
  public:
    // The latitude that makes the authored sun an equinox morning sun.
    Almanac ();

    // The clock, in hours, at which the morning sun stands at the old
    // `sun_height`; a height the equinox sun never reaches gives noon or
    // midnight.
    hours_t clock_for_sun_height (proportion_t sun_height) const;

    // The sky at this moment of the day's clock.
    SkyReading at (clock_time_t when) const;

    // The same sky lit from a fixed sun at the old `sun_height` and
    // azimuth, for views composed under one.
    SkyReading held (proportion_t sun_height) const;

    radians_t latitude () const {
      return m_latitude;
    }

  private:
    direction_t body (radians_t hour_angle, radians_t declination) const;
    void light (SkyReading& sky) const;

    radians_t m_latitude;
  };
}

#endif
