#ifndef MOPPE_GAME_SHORE_SURF_HH
#define MOPPE_GAME_SHORE_SURF_HH

#include <moppe/terrain/watercourse.hh>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace moppe::game {
  // A presentation field, not a water surface. Neither the shared Gerstner
  // heights nor the world cache depends on this field.
  struct ShoreSurf {
    std::size_t width = 0;
    std::size_t height = 0;
    float period_x = 0.0f; // metres
    float period_z = 0.0f;

    // Row-major samples at (x * period_x / width, z * period_z / height),
    // like the terrain lattice, NOT at half-cell-offset world positions.
    // For a repeating, linearly filtered texture, use world_xz / period +
    // 0.5 / texture_size as UV. All four lanes are finite:
    //
    // 0: travel time, seconds, positive at sea, negative on nearby land.
    //    A phase omega * (time + lane0) travels shoreward. Land is only a
    //    constant shallow-speed phase extension, not simulated runup.
    // 1: approximate signed coast distance, metres, positive at sea,
    //    negative on land, clamped to +/-256. Freshwater is -256; regions
    //    with no coast have the appropriate far distance and zero time.
    // 2,3: shoreward unit direction (x,z) TIMES a mask in [0,1]. Its length
    //    is the coastal/coherence mask; normalize only after saving that
    //    length, and multiply effects by it. Zero means no surf or wetting.
    //    Sea fades from 120 to 240 m offshore. Dry ground fades from 4 to
    //    24 m inland and from 0.2 to 1.25 m above the sea's still level.
    //    Conflicting fronts fade as their directions cancel.
    //
    // Coarsening cannot describe sub-texel water-body boundaries. Consumers
    // MUST also gate water effects by the original sea lane/depth, and
    // material wetting by the actual ground elevation at the coast. A
    // filtered mask is never permission to put surf on a lake or a cliff.
    std::vector<std::array<float, 4>> samples;
  };

  // At most 512 samples per axis, nearest original lattice sites (the
  // original spacing on smaller domains). No source-sized scratch arrays.
  // Sea is a wet sample with wave_amplitude >= 0.95, never an altitude
  // test. Wet freshwater blocks propagation and does not seed a coast.
  // The sea's still level, read from that mask, is assumed common to the
  // domain, as in WaterSheets. Ground must have the water domain's layout.
  // Travel is a first-order eikonal solve at sqrt(9.81 * max(depth, 0.25))
  // m/s, with sub-cell coast seeds but only coarse-cell front accuracy.
  // This is not a fetch, wave-energy, or flooding solve.
  // Invalid sizes or non-finite sampled inputs throw std::invalid_argument.
  [[nodiscard]] ShoreSurf
  make_shore_surf (const terrain::WaterSheets& water,
                   std::span<const terrain::SurfaceElevation> ground);
}

#endif
