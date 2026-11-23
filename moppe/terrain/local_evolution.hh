#ifndef MOPPE_TERRAIN_LOCAL_EVOLUTION_HH
#define MOPPE_TERRAIN_LOCAL_EVOLUTION_HH

#include <moppe/map/surface.hh>

#include <optional>

namespace moppe::terrain {
  enum class EvolutionBackend { Cpu, Metal, MetalMultigrid };

  // The baker and fingerprint always use the CPU reference. Local evolution
  // publishes the surface only after the entire accelerator job succeeds.
  EvolutionBackend local_evolution_backend ();
  const char* backend_name (EvolutionBackend backend);

  struct LocalEvolutionBudget {
    cubic_meters_f64_t tectonic_uplift {};
    cubic_meters_f64_t exported_sediment {};
    cubic_meters_f64_t balance_error {};
    // Set when the backend audits the summed area delivered to its outlets.
    std::optional<square_meters_t> drainage_area_error;
  };

  LocalEvolutionBudget
  evolve_local (map::SurfaceGeometry& surface,
                std::span<const meters_per_julian_year_t> uplift,
                const StreamPowerEvolution& parameters,
                EvolutionBackend backend);
}
#endif
