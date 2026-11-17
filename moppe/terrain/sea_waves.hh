#ifndef MOPPE_TERRAIN_SEA_WAVES_HH
#define MOPPE_TERRAIN_SEA_WAVES_HH

#include <moppe/correct_math.hh>
#include <moppe/gfx/math.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/terrain/domain.hh>

#include <array>
#include <cmath>

// The sea's waves: a few long-crested trains running before the wind, each
// a Gerstner wave, so the water itself rises and falls and its crests
// sharpen while its troughs broaden. The water program draws exactly these
// (the renderer hands it this table each frame, `sea-train-N` in
// world.lisp), and what floats or wades reads the same surface from
// `sea_waves_at`, so the boat rides the waves that are drawn.
//
// How much of the full sea a place carries is `sea_wave_scale`: the sheet's
// wave lane (the sea one, a lake a tenth, a river none), squared so that
// only the open sea really heaves, and shrinking over the last few metres
// of depth so that the waves die on the shore instead of baring its bed.

namespace moppe::terrain {
  struct SeaWaveTrain {
    meters_t wavelength;
    meters_t amplitude; // up and down about the still level
    radians_t heading;  // off the wind's
    radians_t phase;    // so that no two trains crest together
  };

  // The wind's heading, x toward z, which the water's ripples run along.
  inline constexpr radians_t sea_wind_heading = 0.4542f * u::rad;

  // Each about equally steep (amplitude by wavenumber near 0.05), the
  // longer from nearer the wind, the shorter spread wider about it: a sea
  // a fresh breeze has raised, a metre or so from trough to crest.
  inline constexpr std::array<SeaWaveTrain, 6> sea_wave_trains { {
    { 44.0f * u::m, 0.350f * u::m, 0.00f * u::rad, 0.0f * u::rad },
    { 29.0f * u::m, 0.230f * u::m, 0.38f * u::rad, 1.7f * u::rad },
    { 19.0f * u::m, 0.150f * u::m, -0.52f * u::rad, 4.1f * u::rad },
    { 12.5f * u::m, 0.100f * u::m, 0.80f * u::rad, 2.6f * u::rad },
    { 8.3f * u::m, 0.066f * u::m, -1.00f * u::rad, 5.3f * u::rad },
    { 5.6f * u::m, 0.045f * u::m, 0.21f * u::rad, 0.9f * u::rad },
  } };

  // How far each train carries the water to and fro, by its amplitude: the
  // crests draw together and the troughs spread, by at most about half
  // their spacing between them all.
  inline constexpr scale_factor_t sea_wave_lean = 1.8f * scale_factor[one];

  inline constexpr metres_per_second_squared_t sea_wave_gravity =
    9.81f * u::m / pow<2> (u::s);
  // The phase is kept in double until it is wrapped, so it stays exact
  // however long the game runs.
  using sea_wave_radians_t = quantity<si::radian, double>;
  inline constexpr sea_wave_radians_t sea_wave_tau = 6.283185307179586 * u::rad;

  // Radians a metre along the train (counted as plain per metre: the
  // dispersion's square root has no place for a radian).
  inline per_metre_t sea_wave_number (const SeaWaveTrain& train) {
    return static_cast<float> (sea_wave_tau.numerical_value_in (u::rad)) /
           train.wavelength;
  }

  // Radians a second: deep water's dispersion.
  inline per_second_t sea_wave_frequency (const SeaWaveTrain& train) {
    return sqrt (sea_wave_gravity * sea_wave_number (train));
  }

  // The train's phase at `time` (seconds of the game), wrapped to one
  // turn, so a place's phase is its distance along the train plus this
  // however long the game runs.
  inline radians_t sea_wave_phase (const SeaWaveTrain& train, double time) {
    const sea_wave_radians_t turned =
      fmod (sea_wave_radians_t (train.phase) -
              per_second_value (sea_wave_frequency (train)) * time * u::rad,
            sea_wave_tau);
    return value_cast<float> (
      turned < sea_wave_radians_t::zero () ? turned + sea_wave_tau : turned);
  }

  // The share of the full sea's waves at a place whose sheet carries the
  // wave lane `amplitude` over water `depth` deep.
  inline proportion_t sea_wave_scale (WaveAmplitude amplitude, meters_t depth) {
    return quantity_cast<proportion> (
      amplitude * amplitude *
      smoothstep (0.0f, 4.0f, depth.numerical_value_in (u::m)));
  }

  struct SeaWaves {
    // Above the still level.
    meters_t height {};
    // The water's own motion at the surface.
    velocity_t velocity = moppe::velocity (Vec3 ());
  };

  // The waves at `at` at `time`, scaled by `scale`. The trains carry the
  // water to and fro as well as up and down, so the water standing over
  // `at` came from elsewhere: a few steps of fixed-point iteration find
  // where (the drawn surface does the same displacement forward).
  inline SeaWaves
  sea_waves_at (const plan_position_t& at, double time, proportion_t scale) {
    SeaWaves waves;
    if (!(scale > proportion_t::zero ()))
      return waves;
    // The shader's own lanes, in the same order of operations. dx and dz
    // are the train's heading as a unit vector's two lanes.
    struct Lane {
      float dx, dz;
      per_metre_t k;
      meters_t a, h;
      per_second_t w;
      radians_t phase;
    };
    std::array<Lane, sea_wave_trains.size ()> lanes;
    for (std::size_t i = 0; i < lanes.size (); ++i) {
      const SeaWaveTrain& t = sea_wave_trains[i];
      const radians_t heading = sea_wind_heading + t.heading;
      const meters_t amplitude = t.amplitude;
      lanes[i] = { cr::cos (radians_value (heading)),
                   cr::sin (radians_value (heading)),
                   sea_wave_number (t),
                   meters_t (amplitude * scale),
                   meters_t (amplitude * scale * sea_wave_lean),
                   sea_wave_frequency (t),
                   sea_wave_phase (t, time) };
    }
    const meters_t x = x_of (at), z = z_of (at);
    meters_t rx = x, rz = z;
    for (int step = 0; step < 6; ++step) {
      meters_t ox = 0.0f * u::m, oz = 0.0f * u::m;
      for (const Lane& l : lanes) {
        const radians_t theta = l.k * (l.dx * rx + l.dz * rz) + l.phase;
        const float c = cr::cos (radians_value (theta));
        ox += l.h * l.dx * c;
        oz += l.h * l.dz * c;
      }
      rx = x - ox;
      rz = z - oz;
    }
    meters_t height = 0.0f * u::m;
    velocity_component_t vx = 0.0f * u::m / u::s;
    velocity_component_t vy = 0.0f * u::m / u::s;
    velocity_component_t vz = 0.0f * u::m / u::s;
    for (const Lane& l : lanes) {
      const radians_t theta = l.k * (l.dx * rx + l.dz * rz) + l.phase;
      const float s = cr::sin (radians_value (theta)),
                  c = cr::cos (radians_value (theta));
      height += l.a * s;
      // Forward under a crest, back under a trough, rising ahead of one.
      vx += l.a * l.w * l.dx * s;
      vy -= l.a * l.w * c;
      vz += l.a * l.w * l.dz * s;
    }
    waves.height = height;
    waves.velocity =
      moppe::velocity (Vec3 (vx.numerical_value_in (u::m / u::s),
                             vy.numerical_value_in (u::m / u::s),
                             vz.numerical_value_in (u::m / u::s)));
    return waves;
  }
}

#endif
