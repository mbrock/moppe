#include <moppe/terrain/local_evolution.hh>

#include <moppe/environment.hh>

#include <stdexcept>
#include <string_view>

namespace moppe::terrain {
#ifdef MOPPE_METAL_EVOLUTION
  LocalEvolutionBudget evolve_metal (map::SurfaceGeometry&,
                                     std::span<const meters_per_julian_year_t>,
                                     const StreamPowerEvolution&,
                                     bool multigrid);
#endif

  const char* backend_name (EvolutionBackend backend) {
    switch (backend) {
    case EvolutionBackend::Cpu:
      return "cpu";
    case EvolutionBackend::Metal:
      return "metal";
    case EvolutionBackend::MetalMultigrid:
      return "metal-multigrid";
    }
    throw std::invalid_argument ("unknown evolution backend");
  }

  EvolutionBackend local_evolution_backend () {
    const char* choice = environment ("MOPPE_OROGENY");
    if (choice) {
      for (auto backend : { EvolutionBackend::Cpu,
                            EvolutionBackend::Metal,
                            EvolutionBackend::MetalMultigrid })
        if (std::string_view (choice) == backend_name (backend))
          return backend;
      throw std::invalid_argument ("unknown MOPPE_OROGENY backend");
    }
#ifdef MOPPE_METAL_EVOLUTION
    return EvolutionBackend::MetalMultigrid;
#else
    return EvolutionBackend::Cpu;
#endif
  }

  LocalEvolutionBudget
  evolve_local (map::SurfaceGeometry& surface,
                std::span<const meters_per_julian_year_t> uplift,
                const StreamPowerEvolution& parameters,
                EvolutionBackend backend) {
    if (backend == EvolutionBackend::Cpu) {
      const auto report = map::evolve_terrain (surface, uplift, parameters);
      return { report.tectonic_uplift_volume,
               report.exported_sediment_volume,
               report.sediment_balance_residual,
               std::nullopt };
    }
#ifdef MOPPE_METAL_EVOLUTION
    return evolve_metal (
      surface, uplift, parameters, backend == EvolutionBackend::MetalMultigrid);
#else
    throw std::runtime_error ("Metal evolution is unavailable on this host");
#endif
  }
}
