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

  // Value noise that repeats over a whole number of cells on both axes, so it
  // meets itself across the torus seam. The periods are in lattice cells.
  inline noise_signal_t periodic_noise (float x,
                                        float z,
                                        std::uint32_t period_x,
                                        std::uint32_t period_z,
                                        std::uint32_t seed) {
    const float xf = std::floor (x);
    const float zf = std::floor (z);
    const auto wrap = [] (std::int64_t value, std::uint32_t period) {
      const std::int64_t p = static_cast<std::int64_t> (period);
      return static_cast<std::uint32_t> ((value % p + p) % p);
    };
    const std::uint32_t x0 = wrap (static_cast<std::int64_t> (xf), period_x);
    const std::uint32_t z0 = wrap (static_cast<std::int64_t> (zf), period_z);
    const std::uint32_t x1 = (x0 + 1) % period_x;
    const std::uint32_t z1 = (z0 + 1) % period_z;
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
