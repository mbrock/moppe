#ifndef MOPPE_TERRAIN_SEA_WAVES_HH
#define MOPPE_TERRAIN_SEA_WAVES_HH

#include <moppe/correct_math.hh>
#include <moppe/gfx/math.hh>
#include <moppe/gfx/signal.hh>

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
  inline constexpr float sea_wave_lean = 1.8f;

  inline constexpr metres_per_second_squared_t sea_wave_gravity =
    9.81f * u::m / pow<2> (u::s);
  inline constexpr double sea_wave_tau = 6.283185307179586;

  // Radians a metre along the train.
  inline per_metre_t sea_wave_number (const SeaWaveTrain& train) {
    return static_cast<float> (sea_wave_tau) / train.wavelength;
  }

  // Radians a second: deep water's dispersion.
  inline per_second_t sea_wave_frequency (const SeaWaveTrain& train) {
    return sqrt (sea_wave_gravity * sea_wave_number (train));
  }

  // The train's phase at `time` (seconds of the game), wrapped to one
  // turn, so a place's phase is its distance along the train plus this
  // however long the game runs.
  inline radians_t sea_wave_phase (const SeaWaveTrain& train, double time) {
    const double turned =
      std::fmod (radians_value (train.phase) -
                   per_second_value (sea_wave_frequency (train)) * time,
                 sea_wave_tau);
    return static_cast<float> (turned < 0.0 ? turned + sea_wave_tau : turned) *
           u::rad;
  }

  // The share of the full sea's waves at a place whose sheet carries the
  // wave lane `amplitude` over water `depth` deep.
  inline float sea_wave_scale (float amplitude, meters_t depth) {
    return amplitude * amplitude *
           smoothstep (0.0f, 4.0f, depth.numerical_value_in (u::m));
  }

  struct SeaWaves {
    // Above the still level.
    meters_t height {};
    // The water's own motion at the surface.
    velocity_t velocity = moppe::velocity (Vec3 ());
  };

  // The waves at (x, z) at `time`, scaled by `scale`. The trains carry the
  // water to and fro as well as up and down, so the water standing over
  // (x, z) came from elsewhere: a few steps of fixed-point iteration find
  // where (the drawn surface does the same displacement forward).
  inline SeaWaves sea_waves_at (float x, float z, double time, float scale) {
    SeaWaves waves;
    if (!(scale > 0.0f))
      return waves;
    // Unit-blind from here: the lanes are the shader's own numbers.
    struct Lane {
      float dx, dz, k, a, h, w, phase;
    };
    std::array<Lane, sea_wave_trains.size ()> lanes;
    for (std::size_t i = 0; i < lanes.size (); ++i) {
      const SeaWaveTrain& t = sea_wave_trains[i];
      const float heading = radians_value (sea_wind_heading + t.heading);
      const float amplitude = t.amplitude.numerical_value_in (u::m);
      lanes[i] = { cr::cos (heading),
                   cr::sin (heading),
                   per_metre_value (sea_wave_number (t)),
                   amplitude * scale,
                   amplitude * scale * sea_wave_lean,
                   per_second_value (sea_wave_frequency (t)),
                   radians_value (sea_wave_phase (t, time)) };
    }
    float rx = x, rz = z;
    for (int step = 0; step < 6; ++step) {
      float ox = 0.0f, oz = 0.0f;
      for (const Lane& l : lanes) {
        const float c = cr::cos (l.k * (l.dx * rx + l.dz * rz) + l.phase);
        ox += l.h * l.dx * c;
        oz += l.h * l.dz * c;
      }
      rx = x - ox;
      rz = z - oz;
    }
    float height = 0.0f;
    float velocity[3] = { 0.0f, 0.0f, 0.0f };
    for (const Lane& l : lanes) {
      const float theta = l.k * (l.dx * rx + l.dz * rz) + l.phase;
      const float s = cr::sin (theta), c = cr::cos (theta);
      height += l.a * s;
      // Forward under a crest, back under a trough, rising ahead of one.
      velocity[0] += l.a * l.w * l.dx * s;
      velocity[1] -= l.a * l.w * c;
      velocity[2] += l.a * l.w * l.dz * s;
    }
    waves.height = height * u::m;
    waves.velocity =
      moppe::velocity (Vec3 (velocity[0], velocity[1], velocity[2]));
    return waves;
  }
}

#endif
