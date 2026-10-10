#include <moppe/game/daylight.hh>

#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    // The authored sun's azimuth, from +Z (south) toward +X (east), and
    // the pi its elevation was always computed with.
    constexpr float authored_azimuth = 0.8f;
    constexpr float art_pi = 3.14159f;
    constexpr float authored_height = 0.62f;
    constexpr float day_tau = 6.2831853f;

    // The moon rides a little north of the equinox sun, and starts the
    // first day waxing gibbous: up through the evening and down by half
    // past two, so a night has both a moonlit half and a dark one.
    constexpr float moon_declination = 0.07f;
    constexpr float first_elongation = 2.09f;
    constexpr float elongation_per_day = 0.2128f; // a 29.5-day month

    float elevation_for (float sun_height) {
      return (sun_height - 0.5f) * art_pi;
    }
  }

  float daylight_for_elevation (float sine) {
    return smoothstep (-0.08f, 0.18f, sine);
  }

  float golden_light_for_elevation (float sine) {
    return daylight_for_elevation (sine) *
           (1.0f - smoothstep (0.15f, 0.65f, sine));
  }

  Vec3 authored_sun_direction (float sun_height) {
    const float elevation = elevation_for (sun_height);
    return Vec3 (std::cos (elevation) * std::sin (authored_azimuth),
                 std::sin (elevation),
                 std::cos (elevation) * std::cos (authored_azimuth));
  }

  // With the pole due north, the equinox sun passes through the authored
  // direction where the pole is square to it:
  // cos(lat) cos(e) cos(pi - a) + sin(lat) sin(e) = 0.
  Almanac::Almanac ()
      : m_latitude (std::atan (std::cos (authored_azimuth) /
                               std::tan (elevation_for (authored_height)))) {}

  // A body at an hour angle (zero on the meridian, negative in the
  // morning) and declination: south is +Z, east +X.
  Vec3 Almanac::body (float hour_angle, float declination) const {
    const float sin_lat = std::sin (m_latitude);
    const float cos_lat = std::cos (m_latitude);
    const Vec3 pole (0.0f, sin_lat, -cos_lat);
    const Vec3 meridian (0.0f, cos_lat, sin_lat);
    const Vec3 east (1.0f, 0.0f, 0.0f);
    return normalized (
      pole * std::sin (declination) +
      (meridian * std::cos (hour_angle) - east * std::sin (hour_angle)) *
        std::cos (declination));
  }

  float Almanac::clock_for_sun_height (float sun_height) const {
    const float cosine =
      std::clamp (std::sin (elevation_for (sun_height)) / std::cos (m_latitude),
                  -1.0f,
                  1.0f);
    return 12.0f - std::acos (cosine) * 24.0f / day_tau;
  }

  void Almanac::light (SkyReading& sky) const {
    const float sine = sky.sun[1];
    sky.daylight = daylight_for_elevation (sine);
    const float golden = golden_light_for_elevation (sine);
    sky.sun_height = 0.5f + std::asin (std::clamp (sine, -1.0f, 1.0f)) / art_pi;

    // The sun's light fades out through civil twilight; the moon's rises
    // as the sky darkens, by how much of its face is lit.
    const float sun_strength =
      (0.10f + 0.98f * sky.daylight) * smoothstep (-0.12f, -0.04f, sine);
    sky.moonlight = sky.moon_phase * smoothstep (-0.02f, 0.14f, sky.moon[1]) *
                    (1.0f - sky.daylight);
    // These are display values; the renderer's linear light is their 2.2
    // power, so the full moon is about a quarter of the sun -- far more
    // than life, as a film's night is, so the woods can still be walked.
    const float moon_strength = 0.58f * sky.moonlight;

    const DisplayColor warm (1.0f, 0.60f, 0.30f);
    const DisplayColor ivory (1.0f, 0.96f, 0.84f);
    const DisplayColor silver (0.56f, 0.70f, 1.0f);
    // The stronger of the two is the key light, less the other, so the
    // light passes through nothing as the key changes hands.
    const bool sunlit = sun_strength >= moon_strength;
    const float strength = std::fabs (sun_strength - moon_strength);
    Vec3 key = sunlit ? sky.sun : sky.moon;
    key[1] = std::max (static_cast<float> (key[1]), 0.06f);
    sky.light = normalized (key);
    sky.light_diffuse = scale_display (
      sunlit ? mix_display (ivory, warm, golden) : silver, strength);
    sky.light_specular =
      sunlit ? scale_display (mix_display (DisplayColor (0.92f, 0.95f, 1.0f),
                                           DisplayColor (1.0f, 0.86f, 0.70f),
                                           golden),
                              (0.15f + 0.85f * sky.daylight) *
                                smoothstep (-0.12f, -0.04f, sine))
             : scale_display (silver, 1.2f * moon_strength);

    // The sky's fill: the day's grey-blue, and at night a deep blue that
    // the moon lifts a little.
    const DisplayColor day (0.39f, 0.43f, 0.49f);
    const DisplayColor night = scale_display (
      DisplayColor (0.18f, 0.24f, 0.40f), 0.80f + 0.30f * sky.moonlight);
    sky.ambient = mix_display (night, day, sky.daylight);

    const DisplayColor day_horizon (0.55f, 0.68f, 0.84f);
    const DisplayColor night_horizon (0.035f, 0.045f, 0.09f);
    const DisplayColor warm_horizon (0.92f, 0.58f, 0.32f);
    sky.horizon =
      mix_display (mix_display (night_horizon, day_horizon, sky.daylight),
                   warm_horizon,
                   golden * 0.16f);
  }

  SkyReading Almanac::at (double hours) const {
    const double days = std::floor (hours / 24.0);
    SkyReading sky;
    sky.clock = static_cast<float> (hours - 24.0 * days);
    sky.day = static_cast<int> (days);
    const float hour_angle = (sky.clock - 12.0f) / 24.0f * day_tau;
    sky.sun = body (hour_angle, 0.0f);
    const float elongation =
      first_elongation + elongation_per_day * static_cast<float> (hours / 24.0);
    sky.moon = body (hour_angle - elongation, moon_declination);
    sky.moon_phase = 0.5f * (1.0f - std::cos (elongation));
    sky.pole = Vec3 (0.0f, std::sin (m_latitude), -std::cos (m_latitude));
    sky.turn = hour_angle;
    light (sky);
    return sky;
  }

  SkyReading Almanac::held (float sun_height) const {
    SkyReading sky = at (clock_for_sun_height (sun_height));
    sky.sun = authored_sun_direction (sun_height);
    light (sky);
    return sky;
  }
}
