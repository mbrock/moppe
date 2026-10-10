#include <moppe/game/daylight.hh>

#include <tests/test.hh>

#include <cmath>

using namespace moppe;
using namespace moppe::game;

MOPPE_TEST (the_day_passes_through_the_authored_sun) {
  // The sun the game was always lit by is this latitude's equinox sun at
  // a quarter past nine.
  const Almanac almanac;
  const float clock = almanac.clock_for_sun_height (0.62f);
  MOPPE_CHECK_NEAR (clock, 9.2f, 0.1f);
  const SkyReading sky = almanac.at (clock);
  const Vec3 authored = authored_sun_direction (0.62f);
  for (int axis = 0; axis < 3; ++axis)
    MOPPE_CHECK_NEAR (sky.sun[axis], authored[axis], 2e-4f);
  MOPPE_CHECK_NEAR (sky.sun_height, 0.62f, 1e-3f);
  // By day the sun is the key light, at its authored strength.
  MOPPE_CHECK_NEAR (dot (sky.light, sky.sun), 1.0f, 1e-5f);
  MOPPE_CHECK_NEAR (sky.daylight, 1.0f, 1e-5f);
  MOPPE_CHECK (sky.moonlight == 0.0f);
  const SkyReading held = almanac.held (0.62f);
  MOPPE_CHECK_NEAR (held.light_diffuse.red, sky.light_diffuse.red, 1e-4f);
  MOPPE_CHECK_NEAR (held.ambient.blue, sky.ambient.blue, 1e-4f);
}

MOPPE_TEST (the_sun_rises_in_the_east_and_sets_in_the_west) {
  const Almanac almanac;
  // East is +X and south +Z: an equinox sun rises due east at six,
  // stands in the south at noon, and sets due west at eighteen.
  const SkyReading dawn = almanac.at (6.0);
  MOPPE_CHECK_NEAR (dawn.sun[0], 1.0f, 1e-4f);
  MOPPE_CHECK_NEAR (dawn.sun[1], 0.0f, 1e-4f);
  const SkyReading noon = almanac.at (12.0);
  MOPPE_CHECK_NEAR (noon.sun[0], 0.0f, 1e-4f);
  MOPPE_CHECK (noon.sun[2] > 0.8f);
  MOPPE_CHECK_NEAR (noon.sun[1], std::cos (almanac.latitude_radians ()), 1e-4f);
  const SkyReading dusk = almanac.at (18.0);
  MOPPE_CHECK_NEAR (dusk.sun[0], -1.0f, 1e-4f);
  // The pole stands in the north at the latitude's height, and the days
  // repeat.
  MOPPE_CHECK (noon.pole[2] < 0.0f);
  MOPPE_CHECK_NEAR (
    noon.pole[1], std::sin (almanac.latitude_radians ()), 1e-5f);
  const SkyReading tomorrow = almanac.at (36.0);
  MOPPE_CHECK (tomorrow.day == 1);
  MOPPE_CHECK_NEAR (tomorrow.clock, 12.0f, 1e-4f);
  MOPPE_CHECK_NEAR (tomorrow.sun[2], noon.sun[2], 1e-5f);
}

MOPPE_TEST (the_moon_lights_the_night) {
  const Almanac almanac;
  // The first evening's moon is up and nearly full; it is the key light,
  // from above the horizon, cool and far weaker than the sun.
  const SkyReading night = almanac.at (22.0);
  MOPPE_CHECK (night.daylight == 0.0f);
  MOPPE_CHECK (night.sun[1] < -0.2f);
  MOPPE_CHECK (night.moon[1] > 0.3f);
  MOPPE_CHECK (night.moon_phase > 0.6f);
  MOPPE_CHECK (night.moonlight > 0.4f);
  MOPPE_CHECK_NEAR (dot (night.light, night.moon), 1.0f, 1e-5f);
  MOPPE_CHECK (night.light_diffuse.blue > night.light_diffuse.red);
  const SkyReading day = almanac.at (12.0);
  MOPPE_CHECK (night.light_diffuse.green < 0.7f * day.light_diffuse.green);
  MOPPE_CHECK (night.ambient.green < day.ambient.green);

  // It has set before dawn, leaving the dark to the stars; whatever lights
  // the world never shines from below it.
  const SkyReading small_hours = almanac.at (28.5);
  MOPPE_CHECK (small_hours.moon[1] < 0.0f);
  MOPPE_CHECK (small_hours.moonlight == 0.0f);
  MOPPE_CHECK (small_hours.light_diffuse.green < 0.01f);
  for (double hour = 0.0; hour < 48.0; hour += 0.25) {
    const SkyReading sky = almanac.at (hour);
    MOPPE_CHECK (sky.light[1] > 0.05f);
    MOPPE_CHECK_NEAR (length (sky.light), 1.0f, 1e-4f);
  }
}

MOPPE_TEST (the_light_changes_hands_without_a_jump) {
  // Through dusk the key light fades to nothing before it becomes the
  // moon's, so no frame sees it leap.
  const Almanac almanac;
  SkyReading before = almanac.at (17.0);
  for (double hour = 17.0; hour < 20.0; hour += 1.0 / 240.0) {
    const SkyReading sky = almanac.at (hour);
    MOPPE_CHECK (
      std::fabs (sky.light_diffuse.green - before.light_diffuse.green) < 0.02f);
    if (dot (sky.light, before.light) < 0.99f)
      MOPPE_CHECK (sky.light_diffuse.green < 0.02f);
    before = sky;
  }
}
