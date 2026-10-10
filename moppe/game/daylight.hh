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
#include <moppe/gfx/math.hh>

namespace moppe::game {
  // The sky at one moment.
  struct SkyReading {
    // Hours since midnight, 0..24, and whole days since the first.
    float clock = 9.0f;
    int day = 0;
    // Toward the sun and the moon; either may be below the horizon.
    Vec3 sun { 0, 1, 0 };
    Vec3 moon { 0, -1, 0 };
    // The celestial pole, and how far the stars have turned about it,
    // radians.
    Vec3 pole { 0, 1, 0 };
    float turn = 0.0f;
    // How much of the moon's face is lit, 0 new to 1 full, and how brightly
    // it shines in this sky, 0 by day or when it is down.
    float moon_phase = 1.0f;
    float moonlight = 0.0f;
    // 0 at night to 1 by day, through the twilights.
    float daylight = 1.0f;
    // The key light the world is shaded and shadowed by: the sun, or the
    // moon once the sun is gone. It stays a little above the horizon.
    Vec3 light { 0, 1, 0 };
    DisplayColor light_diffuse {};
    DisplayColor light_specular {};
    DisplayColor ambient {};
    DisplayColor horizon {};
    // The old art-direction reading of the sun's elevation: 0.5 at the
    // horizon, 1 overhead.
    float sun_height = 0.62f;
  };

  // The sky's light by the sine of the sun's elevation: 0 at night to 1 by
  // day, and how golden the low sun is.
  float daylight_for_elevation (float sine);
  float golden_light_for_elevation (float sine);

  // The fixed sun the game was always lit by, at the old `sun_height`:
  // south-east, by that height above or below the horizon.
  Vec3 authored_sun_direction (float sun_height);

  class Almanac {
  public:
    // The latitude that makes the authored sun an equinox morning sun.
    Almanac ();

    // The clock, in hours, at which the morning sun stands at the old
    // `sun_height`; a height the equinox sun never reaches gives noon or
    // midnight.
    float clock_for_sun_height (float sun_height) const;

    // The sky `hours` after the first midnight.
    SkyReading at (double hours) const;

    // The same sky lit from a fixed sun at the old `sun_height` and
    // azimuth, for views composed under one.
    SkyReading held (float sun_height) const;

    float latitude_radians () const {
      return m_latitude;
    }

  private:
    Vec3 body (float hour_angle, float declination) const;
    void light (SkyReading& sky) const;

    float m_latitude;
  };
}

#endif
