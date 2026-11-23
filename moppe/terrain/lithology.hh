#ifndef MOPPE_TERRAIN_LITHOLOGY_HH
#define MOPPE_TERRAIN_LITHOLOGY_HH

// Analytical 3D Fennoscandian stratigraphy and structural fissure corridors
// over the periodic terrain torus.
//
// Rock varies both laterally and with stratigraphic elevation:
//   1. Precambrian crystalline basement (granite domes and gneiss), hard and
//      steep-forming beneath an undulating unconformity surface.
//   2. A soft shale/marl interbed resting directly on the unconformity,
//      undercutting the caprock above into stepped benches and scarps.
//   3. Resistant limestone caprock above the shale, preserving flat-topped
//      plateaus (klintar) and feeding karst spring fens along its lower foot.
//   4. Conjugate regional fracture corridors (sprickdalar) cutting across the
//      basement and cover with elevated erodibility and gentle tectonic sag,
//      guiding sweeping structural valleys, trellis bends, and bedrock gorges.

#include <moppe/quantities.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/sediment_transport.hh>

#include <array>
#include <cstdint>

namespace moppe::terrain {
  using ErodibilityFactor =
    mp_units::quantity<erodibility_factor[mp_units::one], float>;
  using FissureIntensity =
    mp_units::quantity<fissure_intensity[mp_units::one], float>;

  struct LithologySample {
    // Fluvial incision multiplier relative to StreamPowerEvolution's reference
    // rate, accounting for bedrock stratum, fracture corridors, and any loose
    // alluvial cover blanketing hard rock.
    ErodibilityFactor erodibility = 1.0f * erodibility_factor[mp_units::one];
    // Tectonic uplift multiplier in (0, 1]: rigid basement/caprock blocks rise
    // fully while shattered fissure corridors lag slightly as structural
    // troughs.
    proportion_t uplift_factor = 1.0f * proportion[mp_units::one];
    // Relative hillslope creep mobility in (0, 1]: hard granite and limestone
    // hold steeper faces; soft shale and fractured rock slump into aprons.
    proportion_t hillslope_mobility = 1.0f * proportion[mp_units::one];
    // Exposed stratigraphic shares (summing to one in bare bedrock).
    proportion_t limestone = 0.0f * proportion[mp_units::one];
    proportion_t shale = 0.0f * proportion[mp_units::one];
    proportion_t basement = 1.0f * proportion[mp_units::one];
    // Hard crystalline pluton/dome prominence within the basement.
    proportion_t granite_dome = 0.0f * proportion[mp_units::one];
    // Intensity of conjugate bedrock fracture corridors (sprickdalar).
    FissureIntensity fissure = 0.0f * fissure_intensity[mp_units::one];
  };

  // One periodic harmonic of the unconformity fold surface over the torus.
  struct FoldWave {
    int cycles_x = 0;
    int cycles_z = 0;
    meters_t amplitude = 0.0f * u::m;
    radians_t phase = 0.0f * u::rad;
  };

  // One conjugate regional fracture family (sprickdal set) over the torus.
  struct FissureFamily {
    int cycles_x = 1;
    int cycles_z = 0;
    radians_t phase = 0.0f * u::rad;
    meters_t spacing = 2500.0f * u::m;
    meters_t half_width = 75.0f * u::m;
    scale_factor_t boost = 1.0f * scale_factor[mp_units::one];
    radians_t warp_amplitude_a = 0.0f * u::rad;
    radians_t warp_amplitude_b = 0.0f * u::rad;
    int warp_a_x = 0;
    int warp_a_z = 0;
    radians_t warp_a_phase = 0.0f * u::rad;
    proportion_t modulation_depth = 0.0f * proportion[mp_units::one];
    int warp_b_x = 0;
    int warp_b_z = 0;
    radians_t warp_b_phase = 0.0f * u::rad;
    radians_t modulation_phase = 0.0f * u::rad;
    int modulation_x = 0;
    int modulation_z = 0;
    proportion_t uplift_suppression = 0.0f * proportion[mp_units::one];
  };

  struct Lithology {
    bool enabled = false;
    Seed seed { 0 };
    meters_t period_x = 10000.0f * u::m;
    meters_t period_z = 10000.0f * u::m;
    SurfaceElevation sea_level = surface_elevation_point (50.0f * u::m);

    SurfaceElevation unconformity_base =
      surface_elevation_point (148.0f * u::m);
    meters_t shale_thickness = 26.0f * u::m;
    meters_t transition_half_width = 3.5f * u::m;
    meters_t bedding_wavelength = 14.0f * u::m;
    proportion_t bedding_contrast = 0.22f * proportion[mp_units::one];
    ErodibilityFactor limestone_erodibility =
      0.26f * erodibility_factor[mp_units::one];
    ErodibilityFactor shale_erodibility =
      2.50f * erodibility_factor[mp_units::one];
    ErodibilityFactor gneiss_erodibility =
      0.78f * erodibility_factor[mp_units::one];
    ErodibilityFactor granite_erodibility =
      0.38f * erodibility_factor[mp_units::one];
    meters_t cover_scale = 2.0f * u::m;

    std::array<FoldWave, 6> fold_waves {};
    std::array<FissureFamily, 3> fissures {};

    [[nodiscard]] LithologySample sample (
      TerrainDomain::LapPosition at,
      SurfaceElevation rock_elevation,
      SedimentThickness cover = SedimentThickness::zero ()) const noexcept;

    [[nodiscard]] proportion_t
    uplift_factor_at (TerrainDomain::LapPosition at) const noexcept;

    [[nodiscard]] SurfaceElevation
    unconformity_at (TerrainDomain::LapPosition at) const noexcept;
  };

  // Build the periodic 3D stratigraphy and conjugate fissure-valley network
  // for a world of the given physical torus period and sea level.
  [[nodiscard]] Lithology make_lithology (Seed seed,
                                          meters_t period_x,
                                          meters_t period_z,
                                          SurfaceElevation sea_level);
}

#endif
