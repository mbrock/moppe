#ifndef MOPPE_MAP_TERRAIN_GENERATION_HH
#define MOPPE_MAP_TERRAIN_GENERATION_HH

#include <moppe/map/surface.hh>
#include <moppe/terrain/geological.hh>
#include <moppe/terrain/stream_power_evolution.hh>
#include <moppe/terrain/trail.hh>

#include <span>
#include <vector>

namespace moppe::map {
  // Draw the canonical seeded geology into a physical surface and return the
  // uplift rate used by the following evolution pass.
  std::vector<meters_per_julian_year_t>
  initialize_terrain (SurfaceGeometry& surface,
                      terrain::Seed seed,
                      meters_t water_datum,
                      const terrain::GeologicalProgress& progress = {});

  // Evolve the initialized physical elevations in place.
  terrain::StreamPowerEvolutionReport
  evolve_terrain (SurfaceGeometry& surface,
                  std::span<const meters_per_julian_year_t> uplift,
                  const terrain::StreamPowerEvolution& parameters,
                  const terrain::StreamPowerEvolutionBackend* backend = nullptr,
                  const terrain::StreamPowerProgress& progress = {});

  // Form the canonical built circuit in place and return its useful network.
  terrain::TrailNetwork
  form_terrain_trails (SurfaceGeometry& surface,
                       const terrain::TrailFormation& parameters);
}

#endif
