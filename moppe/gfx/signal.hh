#ifndef MOPPE_GFX_SIGNAL_HH
#define MOPPE_GFX_SIGNAL_HH

#include <moppe/gfx/math.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>

// The shaping primitives the world is built out of: a soft step between two
// edges, a repeatable hash, and the periodic value noise laid over the torus.
//
// Each of these had grown several copies -- four separate smoothsteps, three
// hashes -- all of them bare floats, all of them the same arithmetic. One
// definition each, and the ones that carry a meaning say so: a smoothstep
// answers a proportion, and noise answers a noise signal, both of which the
// quantity registry already had names for and nothing used.

namespace moppe {
  // The canonical soft step. Returns 0 below edge0, 1 above edge1, and the
  // cubic ease between them.
  inline float smoothstep (float edge0, float edge1, float value) {
    const float t = std::clamp ((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  // The same step between two edges of a measured quantity: an elevation
  // band, a distance falloff, a depth ramp. The edges and the value share a
  // unit, their ratio does not have one, and the answer is a proportion.
  template <typename Edge, typename Value>
    requires requires (Edge edge, Value value) {
      (value - edge) / (edge - edge);
    }
  proportion_t band (Edge edge0, Edge edge1, Value value) {
    const float t =
      ((value - edge0) / (edge1 - edge0)).numerical_value_in (mp_units::one);
    return smoothstep (0.0f, 1.0f, t) * proportion[mp_units::one];
  }

  // A repeatable hash over lattice coordinates and a world seed. The world
  // has to look the same on every run and on every backend, so this is
  // spelled out rather than drawn from a random engine.
  inline std::uint32_t
  lattice_hash (std::uint32_t x, std::uint32_t z, std::uint32_t seed) {
    std::uint32_t value = seed ^ (x * 0x9e3779b9U) ^ (z * 0x85ebca6bU);
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
  }

  inline float unit_hash (std::uint32_t bits) {
    return static_cast<float> (bits & 0x00ffffffU) /
           static_cast<float> (0x01000000U);
  }

  // Value noise that repeats a whole number of times around the torus, so it
  // meets itself at the seam.
  //
  // The place is where a site stands as a fraction of one lap. The count is
  // how many patches fit around the world -- and it is one number, used both
  // to scale the place and to wrap the lattice, because those are the same
  // number. They were once written separately and had to be kept in step by
  // hand; a field whose scale and period disagree simply stops closing.
  //
  // It has to be a whole number for the same reason.
  inline noise_signal_t periodic_noise (proportion_t along_x,
                                        proportion_t along_z,
                                        std::uint32_t patches_per_lap,
                                        std::uint32_t seed) {
    const float patches = static_cast<float> (patches_per_lap);
    const float x = along_x.numerical_value_in (mp_units::one) * patches;
    const float z = along_z.numerical_value_in (mp_units::one) * patches;
    const float xf = std::floor (x);
    const float zf = std::floor (z);
    const auto wrap = [patches_per_lap] (float value) {
      const std::int64_t period = static_cast<std::int64_t> (patches_per_lap);
      const std::int64_t whole = static_cast<std::int64_t> (value);
      return static_cast<std::uint32_t> ((whole % period + period) % period);
    };
    const std::uint32_t x0 = wrap (xf);
    const std::uint32_t z0 = wrap (zf);
    const std::uint32_t x1 = (x0 + 1) % patches_per_lap;
    const std::uint32_t z1 = (z0 + 1) % patches_per_lap;
    const float tx = smoothstep (0.0f, 1.0f, x - xf);
    const float tz = smoothstep (0.0f, 1.0f, z - zf);
    const float a = unit_hash (lattice_hash (x0, z0, seed));
    const float b = unit_hash (lattice_hash (x1, z0, seed));
    const float c = unit_hash (lattice_hash (x0, z1, seed));
    const float d = unit_hash (lattice_hash (x1, z1, seed));
    return std::lerp (std::lerp (a, b, tx), std::lerp (c, d, tx), tz) *
           noise_signal[mp_units::one];
  }
}

#endif
