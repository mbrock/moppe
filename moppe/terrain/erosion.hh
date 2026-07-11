#ifndef MOPPE_TERRAIN_EROSION_HH
#define MOPPE_TERRAIN_EROSION_HH

#include <cstddef>
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

  // Finite-time n=1 stream-power evolution.  SI units keep age, uplift,
  // erodibility, drainage area, and terrain scale comparable across grid
  // resolutions.  sea_level is expressed in the source heightfield's units.
  struct AnalyticalErosion {
    float time_years = 100000.0f;
    float uplift_m_per_year = 0.0f;
    float erodibility = 2e-5f;
    float area_exponent = 0.4f;
    float sea_level = 50.0f / 650.0f;
    int fixed_point_iterations = 1;
    float relaxation = 1.0f;
  };

  struct AnalyticalErosionReport {
    std::size_t cells = 0;
    std::size_t fixed_boundaries = 0;
    int fixed_point_iterations = 0;
    double lowered_volume_m3 = 0.0;
    double raised_volume_m3 = 0.0;
    double mean_absolute_change_m = 0.0;
    double maximum_absolute_change_m = 0.0;
  };

  // Deterministic channel stamping along the extracted river network.
  // Bed depth follows catchment area (hydraulic-geometry style) and beds
  // are monotone downstream, so every carved channel drains to its mouth.
  // Depths and widths are in meters; area threshold is in terrain cells so
  // carved channels match the renderer's visible-river extraction.
  struct ChannelCarving {
    float sea_level = 50.0f / 650.0f;
    float minimum_area_cells = 1024.0f;
    float depth_per_sqrt_m2 = 0.0015f;
    float minimum_depth_m = 0.4f;
    float maximum_depth_m = 2.5f;
    float bank_blend_m = 6.0f;
  };

  struct ChannelCarvingReport {
    std::size_t reaches = 0;
    std::size_t carved_cells = 0;
    double lowered_volume_m3 = 0.0;
    double mean_lowering_m = 0.0;
    double maximum_lowering_m = 0.0;
  };
}

#endif
