#ifndef MOPPE_TERRAIN_EROSION_HH
#define MOPPE_TERRAIN_EROSION_HH

#include <cstdint>

namespace moppe::terrain {
  enum class SedimentDisposition {
    Discard,
    Deposit
  };

  enum class CarvingRule {
    Unconstrained,
    PathMonotone
  };

  // Operational reading produced by one hydraulic erosion stage. Amounts are
  // measured in normalized terrain-height units; the balance is independent
  // of the physical cell area shared by every term.
  struct HydraulicErosionReport {
    std::uint64_t droplets = 0;
    std::uint64_t steps = 0;
    std::uint64_t stopped_flat = 0;
    std::uint64_t stopped_at_boundary = 0;
    std::uint64_t stopped_at_step_limit = 0;
    std::uint64_t stopped_at_water_cutoff = 0;
    double eroded = 0.0;
    double deposited = 0.0;
    double discarded_sediment = 0.0;
    double final_water = 0.0;

    double mean_steps () const noexcept {
      return droplets ? static_cast<double> (steps) / droplets : 0.0;
    }

    double mean_final_water () const noexcept {
      return droplets ? final_water / droplets : 0.0;
    }

    double discarded_fraction () const noexcept {
      return eroded > 0.0 ? discarded_sediment / eroded : 0.0;
    }
  };
}

#endif
