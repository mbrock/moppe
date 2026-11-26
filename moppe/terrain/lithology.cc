#include <moppe/correct_math.hh>
#include <moppe/terrain/lithology.hh>

#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace moppe::terrain {
  namespace {
    using mp_units::one;

    constexpr radians_t two_pi_rad = (2.0f * std::numbers::pi_v<float>)*u::rad;
    constexpr radians_t half_pi_rad = (0.5f * std::numbers::pi_v<float>)*u::rad;
    constexpr proportion_t whole = 1.0f * proportion[one];
    constexpr proportion_t none_part = 0.0f * proportion[one];

    proportion_t uniform_unit (std::mt19937& rng) {
      return static_cast<float> (rng () >> 8) * (1.0f / 16777216.0f) *
             proportion[one];
    }

    template <typename Quantity>
    Quantity uniform_between (std::mt19937& rng, Quantity low, Quantity high) {
      return low + (high - low) * uniform_unit (rng).numerical_value_in (one);
    }

    struct WaveVector {
      int nx = 1;
      int nz = 0;
      meters_t wavelength = 1000.0f * u::m;
    };

    // Nearest non-zero integer torus wavevector (nx, nz) matching an azimuth
    // angle and physical wavelength over (period_x, period_z).
    WaveVector periodic_wave (radians_t azimuth,
                              meters_t target_wavelength,
                              meters_t period_x,
                              meters_t period_z) {
      const auto cycles_x =
        (period_x / target_wavelength) * cr::cos (radians_value (azimuth));
      const auto cycles_z =
        (period_z / target_wavelength) * cr::sin (radians_value (azimuth));
      int nx =
        static_cast<int> (std::lround (cycles_x.numerical_value_in (one)));
      int nz =
        static_cast<int> (std::lround (cycles_z.numerical_value_in (one)));
      if (nx == 0 && nz == 0) {
        if (mp_units::abs (cycles_x) >= mp_units::abs (cycles_z))
          nx = cycles_x < 0.0f * one ? -1 : 1;
        else
          nz = cycles_z < 0.0f * one ? -1 : 1;
      }
      const auto inv_x = static_cast<float> (nx) / period_x;
      const auto inv_z = static_cast<float> (nz) / period_z;
      const auto inv_len = mp_units::sqrt (inv_x * inv_x + inv_z * inv_z);
      return {
        .nx = nx,
        .nz = nz,
        .wavelength = 1.0f / inv_len,
      };
    }

    proportion_t ramp_metres (meters_t edge0, meters_t edge1, meters_t value) {
      return smoothstep (meters_value (edge0),
                         meters_value (edge1),
                         meters_value (value)) *
             proportion[one];
    }

    radians_t plane_angle (int nx,
                           int nz,
                           TerrainDomain::LapPosition at,
                           radians_t phase) {
      const auto laps = static_cast<float> (nx) * at.along_x +
                        static_cast<float> (nz) * at.along_z;
      return two_pi_rad * laps.numerical_value_in (one) + phase;
    }

    struct UnconformityReading {
      SurfaceElevation elevation {};
      proportion_t dome = none_part;
    };

    UnconformityReading unconformity_of (const Lithology& lith,
                                         TerrainDomain::LapPosition at) {
      meters_t sum = 0.0f * u::m;
      meters_t dome_sum = 0.0f * u::m;
      for (std::size_t k = 0; k < lith.fold_waves.size (); ++k) {
        const FoldWave& w = lith.fold_waves[k];
        const radians_t angle =
          plane_angle (w.cycles_x, w.cycles_z, at, w.phase);
        const meters_t contribution =
          w.amplitude * cr::cos (radians_value (angle));
        sum += contribution;
        if (k < 4)
          dome_sum += contribution;
      }
      return {
        .elevation = lith.unconformity_base + sum,
        .dome = ramp_metres (6.0f * u::m, 38.0f * u::m, dome_sum),
      };
    }

    struct FissureReading {
      FissureIntensity intensity = 0.0f * fissure_intensity[one];
      scale_factor_t boost = 1.0f * scale_factor[one];
      proportion_t uplift_factor = whole;
    };

    FissureReading fissure_of (const Lithology& lith,
                               TerrainDomain::LapPosition at) {
      proportion_t strongest = none_part;
      scale_factor_t boost = 1.0f * scale_factor[one];
      proportion_t suppression = none_part;
      for (const FissureFamily& f : lith.fissures) {
        const radians_t angle_wa =
          plane_angle (f.warp_a_x, f.warp_a_z, at, f.warp_a_phase);
        const radians_t angle_wb =
          plane_angle (f.warp_b_x, f.warp_b_z, at, f.warp_b_phase);
        const radians_t phase =
          plane_angle (f.cycles_x, f.cycles_z, at, f.phase) +
          f.warp_amplitude_a * cr::sin (radians_value (angle_wa)) +
          f.warp_amplitude_b * cr::sin (radians_value (angle_wb));
        const auto cycles = phase / two_pi_rad;
        const proportion_t frac =
          (cycles.numerical_value_in (one) -
           std::floor (cycles.numerical_value_in (one))) *
          proportion[one];
        const proportion_t dist_cycles = std::min (frac, whole - frac);
        const meters_t dist = f.spacing * dist_cycles.numerical_value_in (one);
        // Narrow shattered fracture core for high fluvial erodibility, inside
        // a broader, gentler tectonic sag that guides drainage without
        // stamping sharp creases across unincised plains.
        const proportion_t core_band =
          ramp_metres (f.half_width, 0.0f * u::m, dist);
        const proportion_t sag_band =
          ramp_metres (2.1f * f.half_width, 0.0f * u::m, dist);
        const radians_t angle_ma =
          plane_angle (f.modulation_x, f.modulation_z, at, f.modulation_phase);
        const radians_t angle_mb =
          plane_angle (2 * f.modulation_x,
                       2 * f.modulation_z,
                       at,
                       two_pi_rad * 0.37f + f.warp_a_phase);
        const proportion_t mod_a =
          (0.5f + 0.5f * cr::cos (radians_value (angle_ma))) * proportion[one];
        const proportion_t mod_b =
          (0.5f + 0.5f * cr::cos (radians_value (angle_mb))) * proportion[one];
        const proportion_t strike = 0.65f * mod_a + 0.35f * mod_b;
        const proportion_t strike_sq =
          (strike.numerical_value_in (one) * strike.numerical_value_in (one)) *
          proportion[one];
        const proportion_t continuity =
          whole - (f.modulation_depth.numerical_value_in (one) *
                   (whole - strike_sq).numerical_value_in (one)) *
                    proportion[one];
        const proportion_t core = (core_band.numerical_value_in (one) *
                                   core_band.numerical_value_in (one) *
                                   continuity.numerical_value_in (one)) *
                                  proportion[one];
        const proportion_t sag = (sag_band.numerical_value_in (one) *
                                  sag_band.numerical_value_in (one) *
                                  continuity.numerical_value_in (one)) *
                                 proportion[one];
        strongest = std::max (strongest, core);
        const scale_factor_t local_boost =
          1.0f * scale_factor[one] +
          (f.boost - 1.0f * scale_factor[one]) * core.numerical_value_in (one);
        boost = std::max (boost, local_boost);
        const proportion_t local_suppression =
          (f.uplift_suppression.numerical_value_in (one) *
           sag.numerical_value_in (one)) *
          proportion[one];
        suppression = std::max (suppression, local_suppression);
      }
      return {
        .intensity = quantity_cast<fissure_intensity> (strongest),
        .boost = boost,
        .uplift_factor =
          std::max (0.35f * proportion[one], whole - suppression),
      };
    }
  }

  Lithology make_lithology (Seed seed,
                            meters_t period_x,
                            meters_t period_z,
                            SurfaceElevation sea_level) {
    Lithology result;
    result.enabled = true;
    result.seed = seed;
    result.period_x = std::max (period_x, 200.0f * u::m);
    result.period_z = std::max (period_z, 200.0f * u::m);
    result.sea_level = sea_level;

    std::mt19937 rng (seed.value ^ 0x57fa7191u);
    result.unconformity_base = sea_level + 98.0f * u::m;
    result.shale_thickness = 26.0f * u::m;
    result.transition_half_width = 3.5f * u::m;
    result.bedding_wavelength = 14.0f * u::m;
    result.bedding_contrast = 0.22f * proportion[one];
    result.limestone_erodibility = 0.26f * erodibility_factor[one];
    result.shale_erodibility = 2.50f * erodibility_factor[one];
    result.gneiss_erodibility = 0.78f * erodibility_factor[one];
    result.granite_erodibility = 0.38f * erodibility_factor[one];
    result.cover_scale = 2.0f * u::m;

    // On worlds wider than 10 km, scale structural fold and regional fault
    // wavelengths with the torus so fracture corridors form sweeping fjord and
    // mountain valleys rather than repeating as a tight grid. Worlds <= 10 km
    // keep macro_scale = 1 exactly.
    const meters_t largest_period = std::max (result.period_x, result.period_z);
    const scale_factor_t macro_scale =
      (largest_period > 10000.0f * u::m
         ? cr::pow (meters_value (largest_period) / 10000.0f, 0.82f)
         : 1.0f) *
      scale_factor[one];
    const proportion_t macro_soften =
      (largest_period > 10000.0f * u::m ? 0.55f : 1.0f) * proportion[one];

    // Broad, coherent regional dip and gentle dome folding so limestone caps
    // read as continuous tablelands and cuestas rather than broken noise.
    constexpr std::array<meters_t, 6> fold_wavelengths {
      6800.0f * u::m, 4800.0f * u::m, 3400.0f * u::m,
      2500.0f * u::m, 1750.0f * u::m, 1250.0f * u::m,
    };
    constexpr std::array<meters_t, 6> fold_amplitudes {
      22.0f * u::m, 15.0f * u::m, 10.5f * u::m,
      7.5f * u::m,  4.5f * u::m,  3.0f * u::m,
    };
    const radians_t fold_base_angle =
      uniform_between (rng, 0.0f * u::rad, two_pi_rad);
    for (std::size_t k = 0; k < fold_wavelengths.size (); ++k) {
      const radians_t azimuth =
        fold_base_angle + static_cast<float> (k) * 1.13f * u::rad +
        uniform_between (rng, -0.25f * u::rad, 0.25f * u::rad);
      const meters_t target_wave =
        fold_wavelengths[k] * macro_scale.numerical_value_in (one);
      const WaveVector wave =
        periodic_wave (azimuth, target_wave, result.period_x, result.period_z);
      // Preserve regional bedding dip angle on small domains where the
      // longest periodic wave snaps to one torus lap.
      const proportion_t dip_scale = std::min (
        whole, quantity_cast<proportion> (wave.wavelength / target_wave));
      result.fold_waves[k] = {
        .cycles_x = wave.nx,
        .cycles_z = wave.nz,
        .amplitude = fold_amplitudes[k] * dip_scale.numerical_value_in (one),
        .phase = uniform_between (rng, 0.0f * u::rad, two_pi_rad),
      };
    }

    // Two primary conjugate Fennoscandian fracture sets (intersecting at ~62-78
    // degrees, deliberately oblique to the lattice axes) plus a finer cross
    // joint set. Each corridor combines elevated erodibility with modest
    // crustal uplift suppression so drainage captures and carves genuine
    // sprickdalar across plateaus and basement ridges.
    const int quadrant = static_cast<int> (rng () & 3u);
    const radians_t primary_azimuth =
      static_cast<float> (quadrant) * half_pi_rad +
      uniform_between (rng, 0.26f * u::rad, 0.56f * u::rad);
    const radians_t conjugate_azimuth =
      primary_azimuth + uniform_between (rng, 1.08f * u::rad, 1.36f * u::rad);
    const radians_t cross_azimuth =
      primary_azimuth + uniform_between (rng, 2.05f * u::rad, 2.45f * u::rad);

    const std::array<radians_t, 3> azimuths { primary_azimuth,
                                              conjugate_azimuth,
                                              cross_azimuth };
    const std::array<meters_t, 3> spacings {
      uniform_between (rng, 2500.0f * u::m, 3100.0f * u::m) *
        macro_scale.numerical_value_in (one),
      uniform_between (rng, 2800.0f * u::m, 3500.0f * u::m) *
        macro_scale.numerical_value_in (one),
      uniform_between (rng, 1500.0f * u::m, 1900.0f * u::m) *
        macro_scale.numerical_value_in (one),
    };
    const std::array<meters_t, 3> half_widths {
      80.0f * u::m * macro_scale.numerical_value_in (one),
      70.0f * u::m * macro_scale.numerical_value_in (one),
      48.0f * u::m * macro_scale.numerical_value_in (one),
    };
    const auto scaled_boost = [macro_soften] (float raw) {
      return (1.0f + (raw - 1.0f) * macro_soften.numerical_value_in (one)) *
             scale_factor[one];
    };
    const std::array<scale_factor_t, 3> boosts {
      scaled_boost (4.30f),
      scaled_boost (3.80f),
      scaled_boost (2.40f),
    };
    const std::array<proportion_t, 3> suppressions {
      0.21f * macro_soften,
      0.17f * macro_soften,
      0.08f * macro_soften,
    };
    const std::array<proportion_t, 3> mod_depths {
      0.65f * proportion[one],
      0.70f * proportion[one],
      0.78f * proportion[one],
    };

    for (std::size_t f = 0; f < result.fissures.size (); ++f) {
      const WaveVector line = periodic_wave (
        azimuths[f], spacings[f], result.period_x, result.period_z);
      const radians_t perp = azimuths[f] + half_pi_rad;
      const WaveVector wa = periodic_wave (
        perp + uniform_between (rng, -0.35f * u::rad, 0.35f * u::rad),
        uniform_between (rng, 3400.0f * u::m, 4800.0f * u::m) *
          macro_scale.numerical_value_in (one),
        result.period_x,
        result.period_z);
      const WaveVector wb = periodic_wave (
        perp + uniform_between (rng, 0.45f * u::rad, 1.05f * u::rad),
        uniform_between (rng, 1800.0f * u::m, 2600.0f * u::m) *
          macro_scale.numerical_value_in (one),
        result.period_x,
        result.period_z);
      const WaveVector mod = periodic_wave (
        perp + uniform_between (rng, -0.25f * u::rad, 0.25f * u::rad),
        uniform_between (rng, 2400.0f * u::m, 3800.0f * u::m) *
          macro_scale.numerical_value_in (one),
        result.period_x,
        result.period_z);

      result.fissures[f] = {
        .cycles_x = line.nx,
        .cycles_z = line.nz,
        .phase = uniform_between (rng, 0.0f * u::rad, two_pi_rad),
        .spacing = line.wavelength,
        .half_width = half_widths[f],
        .boost = boosts[f],
        .warp_amplitude_a = (f < 2 ? 0.42f : 0.30f) * u::rad,
        .warp_amplitude_b = (f < 2 ? 0.20f : 0.15f) * u::rad,
        .warp_a_x = wa.nx,
        .warp_a_z = wa.nz,
        .warp_a_phase = uniform_between (rng, 0.0f * u::rad, two_pi_rad),
        .modulation_depth = mod_depths[f],
        .warp_b_x = wb.nx,
        .warp_b_z = wb.nz,
        .warp_b_phase = uniform_between (rng, 0.0f * u::rad, two_pi_rad),
        .modulation_phase = uniform_between (rng, 0.0f * u::rad, two_pi_rad),
        .modulation_x = mod.nx,
        .modulation_z = mod.nz,
        .uplift_suppression = suppressions[f],
      };
    }

    return result;
  }

  proportion_t
  Lithology::uplift_factor_at (TerrainDomain::LapPosition at) const noexcept {
    if (!enabled)
      return whole;
    return fissure_of (*this, at).uplift_factor;
  }

  SurfaceElevation
  Lithology::unconformity_at (TerrainDomain::LapPosition at) const noexcept {
    if (!enabled)
      return sea_level;
    return unconformity_of (*this, at).elevation;
  }

  LithologySample Lithology::sample (TerrainDomain::LapPosition at,
                                     SurfaceElevation rock_elevation,
                                     SedimentThickness cover) const noexcept {
    if (!enabled)
      return {};
    const UnconformityReading unconf = unconformity_of (*this, at);
    const meters_t above_unconf = meters_t (rock_elevation - unconf.elevation);
    const meters_t above_shale = above_unconf - shale_thickness;

    const proportion_t above_basement =
      ramp_metres (-transition_half_width, transition_half_width, above_unconf);
    const proportion_t limestone_share =
      ramp_metres (-transition_half_width, transition_half_width, above_shale);
    const proportion_t basement_share = whole - above_basement;
    const proportion_t shale_share =
      std::max (none_part, above_basement - limestone_share);

    // Subtle rhythmic bedding inside thick limestone yields horizontal ledges.
    const radians_t bed_angle =
      two_pi_rad * (std::max (0.0f * u::m, above_shale) / bedding_wavelength)
                     .numerical_value_in (one);
    const scale_factor_t bedding =
      (1.0f + bedding_contrast.numerical_value_in (one) *
                cr::sin (radians_value (bed_angle))) *
      scale_factor[one];
    const ErodibilityFactor basement_k =
      gneiss_erodibility + (granite_erodibility - gneiss_erodibility) *
                             unconf.dome.numerical_value_in (one);
    const ErodibilityFactor limestone_k =
      limestone_erodibility * bedding.numerical_value_in (one);

    const ErodibilityFactor stratum_k =
      basement_k * basement_share.numerical_value_in (one) +
      shale_erodibility * shale_share.numerical_value_in (one) +
      limestone_k * limestone_share.numerical_value_in (one);

    const FissureReading fis = fissure_of (*this, at);
    // Shattered rock along regional fracture corridors erodes rapidly even
    // through hard granite and limestone caprock.
    const ErodibilityFactor bedrock_k =
      stratum_k * fis.boost.numerical_value_in (one);

    // Loose alluvial cover thicker than ~2 m is reworked at full reference
    // speed rather than waiting on underlying hard bedrock.
    const proportion_t cover_blend =
      ramp_metres (0.0f * u::m, cover_scale, meters_t (cover));
    const ErodibilityFactor cover_floor =
      std::max (bedrock_k, 1.0f * erodibility_factor[one]);
    const ErodibilityFactor effective_k =
      bedrock_k +
      (cover_floor - bedrock_k) * cover_blend.numerical_value_in (one);

    // Hillslope creep stays <= 1.0 so explicit stability bounds stay exact:
    // hard granite and limestone hold steeper cliff faces; soft shale and
    // fissure corridors relax into aprons.
    const proportion_t mobility = std::clamp (
      (0.45f + 0.35f * effective_k.numerical_value_in (one)) * proportion[one],
      0.45f * proportion[one],
      whole);
    const proportion_t dome_exposed =
      std::clamp ((unconf.dome.numerical_value_in (one) *
                   basement_share.numerical_value_in (one)) *
                    proportion[one],
                  none_part,
                  whole);

    return {
      .erodibility = effective_k,
      .uplift_factor = fis.uplift_factor,
      .hillslope_mobility = mobility,
      .limestone = limestone_share,
      .shale = shale_share,
      .basement = basement_share,
      .granite_dome = dome_exposed,
      .fissure = fis.intensity,
    };
  }
}
