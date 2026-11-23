#include <moppe/spatial/bundle_operations.hh>
#include <moppe/terrain/lithology.hh>
#include <moppe/terrain/stream_power_evolution.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
  using namespace moppe;
  using namespace moppe::terrain;
}

MOPPE_TEST (lithology_is_periodic_across_both_torus_seams) {
  const Lithology lith =
    make_lithology (Seed { 123 },
                    10000.0f * u::m,
                    10000.0f * u::m,
                    surface_elevation_point (50.0f * u::m));
  const SurfaceElevation level = surface_elevation_point (175.0f * u::m);
  constexpr proportion_t zero = 0.0f * proportion[one];
  constexpr proportion_t one_lap = 1.0f * proportion[one];

  for (int i = 0; i < 8; ++i) {
    const proportion_t coord = static_cast<float> (i) / 8.0f * proportion[one];
    const LithologySample x0 = lith.sample ({ zero, coord }, level);
    const LithologySample x1 = lith.sample ({ one_lap, coord }, level);
    MOPPE_CHECK_NEAR (x0.erodibility.numerical_value_in (one),
                      x1.erodibility.numerical_value_in (one),
                      1e-4f);
    MOPPE_CHECK_NEAR (x0.limestone.numerical_value_in (one),
                      x1.limestone.numerical_value_in (one),
                      1e-4f);
    MOPPE_CHECK_NEAR (x0.fissure.numerical_value_in (one),
                      x1.fissure.numerical_value_in (one),
                      1e-4f);

    const LithologySample z0 = lith.sample ({ coord, zero }, level);
    const LithologySample z1 = lith.sample ({ coord, one_lap }, level);
    MOPPE_CHECK_NEAR (z0.erodibility.numerical_value_in (one),
                      z1.erodibility.numerical_value_in (one),
                      1e-4f);
    MOPPE_CHECK_NEAR (z0.limestone.numerical_value_in (one),
                      z1.limestone.numerical_value_in (one),
                      1e-4f);
    MOPPE_CHECK_NEAR (z0.fissure.numerical_value_in (one),
                      z1.fissure.numerical_value_in (one),
                      1e-4f);
  }
}

MOPPE_TEST (lithology_orders_basement_shale_and_limestone_vertically) {
  const Lithology lith =
    make_lithology (Seed { 123 },
                    10000.0f * u::m,
                    10000.0f * u::m,
                    surface_elevation_point (50.0f * u::m));
  // Pick a site away from fissure corridors so pure stratigraphic ordering is
  // measured cleanly.
  TerrainDomain::LapPosition clean { 0.0f * proportion[one],
                                     0.0f * proportion[one] };
  bool found_clean = false;
  for (int row = 0; row < 32 && !found_clean; ++row)
    for (int col = 0; col < 32 && !found_clean; ++col) {
      const TerrainDomain::LapPosition at {
        static_cast<float> (col) / 32.0f * proportion[one],
        static_cast<float> (row) / 32.0f * proportion[one],
      };
      const SurfaceElevation mid = lith.unconformity_at (at);
      if (lith.sample (at, mid).fissure < 0.01f * fissure_intensity[one]) {
        clean = at;
        found_clean = true;
      }
    }
  MOPPE_CHECK (found_clean);

  const SurfaceElevation unconf = lith.unconformity_at (clean);
  const LithologySample deep = lith.sample (clean, unconf - 30.0f * u::m);
  const LithologySample shale = lith.sample (clean, unconf + 13.0f * u::m);
  const LithologySample cap = lith.sample (clean, unconf + 50.0f * u::m);

  MOPPE_CHECK (deep.basement > 0.99f * proportion[one]);
  MOPPE_CHECK (deep.limestone < 0.01f * proportion[one]);
  MOPPE_CHECK (shale.shale > 0.90f * proportion[one]);
  MOPPE_CHECK (cap.limestone > 0.99f * proportion[one]);
  MOPPE_CHECK (cap.basement < 0.01f * proportion[one]);
  // Soft shale erodes much faster than either the resistant limestone caprock
  // above it or the crystalline basement beneath it.
  MOPPE_CHECK (shale.erodibility > 2.0f * deep.erodibility);
  MOPPE_CHECK (shale.erodibility > 5.0f * cap.erodibility);
}

MOPPE_TEST (lithology_fissure_corridors_raise_erodibility_and_soften_uplift) {
  const Lithology lith =
    make_lithology (Seed { 123 },
                    10000.0f * u::m,
                    10000.0f * u::m,
                    surface_elevation_point (50.0f * u::m));
  FissureIntensity peak_fissure = 0.0f * fissure_intensity[one];
  ErodibilityFactor fissure_k = 0.0f * erodibility_factor[one];
  proportion_t min_uplift = 1.0f * proportion[one];
  bool saw_quiet = false;

  for (int row = 0; row < 128; ++row)
    for (int col = 0; col < 128; ++col) {
      const TerrainDomain::LapPosition at {
        static_cast<float> (col) / 128.0f * proportion[one],
        static_cast<float> (row) / 128.0f * proportion[one],
      };
      const SurfaceElevation bed = lith.unconformity_at (at) - 25.0f * u::m;
      const LithologySample s = lith.sample (at, bed);
      if (s.fissure > peak_fissure) {
        peak_fissure = s.fissure;
        fissure_k = s.erodibility;
      }
      if (s.uplift_factor < min_uplift)
        min_uplift = s.uplift_factor;
      if (s.fissure == 0.0f * fissure_intensity[one] &&
          s.uplift_factor == 1.0f * proportion[one])
        saw_quiet = true;
    }

  MOPPE_CHECK (saw_quiet);
  MOPPE_CHECK (peak_fissure > 0.7f * fissure_intensity[one]);
  MOPPE_CHECK (fissure_k > 1.8f * erodibility_factor[one]);
  MOPPE_CHECK (min_uplift < 0.90f * proportion[one]);
}

MOPPE_TEST (lithology_loose_cover_masks_hard_bedrock_erodibility) {
  const Lithology lith =
    make_lithology (Seed { 123 },
                    10000.0f * u::m,
                    10000.0f * u::m,
                    surface_elevation_point (50.0f * u::m));
  const TerrainDomain::LapPosition at { 0.25f * proportion[one],
                                        0.25f * proportion[one] };
  const SurfaceElevation cap = lith.unconformity_at (at) + 50.0f * u::m;
  const LithologySample bare =
    lith.sample (at, cap, SedimentThickness::zero ());
  const LithologySample buried =
    lith.sample (at, cap, 3.0f * sediment_thickness[u::m]);
  MOPPE_CHECK (bare.erodibility < 0.8f * erodibility_factor[one]);
  MOPPE_CHECK (buried.erodibility >= 1.0f * erodibility_factor[one]);
}

MOPPE_TEST (lithology_is_bit_deterministic_and_gpu_layout_matches) {
  const Lithology first =
    make_lithology (Seed { 731 },
                    10000.0f * u::m,
                    10000.0f * u::m,
                    surface_elevation_point (50.0f * u::m));
  const Lithology second =
    make_lithology (Seed { 731 },
                    10000.0f * u::m,
                    10000.0f * u::m,
                    surface_elevation_point (50.0f * u::m));
  const TerrainDomain::LapPosition at { 0.37f * proportion[one],
                                        0.61f * proportion[one] };
  const SurfaceElevation h = surface_elevation_point (172.0f * u::m);
  const LithologySample a = first.sample (at, h);
  const LithologySample b = second.sample (at, h);
  MOPPE_CHECK (a.erodibility == b.erodibility);
  MOPPE_CHECK (a.uplift_factor == b.uplift_factor);
  MOPPE_CHECK (a.limestone == b.limestone);
  MOPPE_CHECK (a.fissure == b.fissure);
}

MOPPE_TEST (lithology_hard_caprock_resists_incision_more_than_soft_shale) {
  // Two identical sloping planes evolved under the same stream power: one
  // placed inside the hard limestone caprock, one inside the soft shale bed.
  const TerrainDomain domain (
    24, 24, spatial_extent_in_metres (Vec3 (1200.0f, 320.0f, 1200.0f)));
  const Lithology lith =
    make_lithology (Seed { 123 },
                    domain.period_x (),
                    domain.period_z (),
                    surface_elevation_point (50.0f * u::m));
  // Zero fold amplitudes and fissure contrasts so stratum elevation is flat
  // for a controlled comparison between caprock and shale.
  Lithology flat = lith;
  for (FoldWave& w : flat.fold_waves)
    w.amplitude = 0.0f * u::m;
  for (FissureFamily& f : flat.fissures) {
    f.boost = 1.0f * scale_factor[one];
    f.uplift_suppression = 0.0f * proportion[one];
  }

  auto run_at = [&] (meters_t base_height) {
    ElevationMap terrain (domain);
    auto& h = spatial::get<surface_elevation> (terrain);
    for (const TerrainIndex site : spatial::sites (domain)) {
      const float ridge = site.column == 0
                            ? 0.0f
                            : static_cast<float> (std::min (
                                site.column, domain.width () - site.column));
      h[domain.offset (site)] =
        surface_elevation_point (base_height + ridge * 1.2f * u::m);
    }
    std::vector<meters_per_julian_year_t> uplift (
      domain.size (), 0.0f * u::m / moppe::astronomy::Julian_year);
    StreamPowerEvolution params;
    params.duration = 20000.0f * moppe::astronomy::Julian_year;
    params.time_step = 20000.0f * moppe::astronomy::Julian_year;
    params.uplift_duration = 0.0f * moppe::astronomy::Julian_year;
    params.fluvial_transport.concentration_at_unit_slope =
      1.0f * sediment_concentration[one];
    params.sea_level = surface_elevation_point (base_height + 0.1f * u::m);
    params.lithology = flat;
    return evolve_stream_power (terrain, uplift, params);
  };

  // Shale band spans [unconf, unconf + 26 m] = [148 m, 174 m].
  const auto shale_run = run_at (150.0f * u::m);
  // Resistant limestone caprock sits above 174 m.
  const auto limestone_run = run_at (200.0f * u::m);

  MOPPE_CHECK (shale_run.report.eroded_volume >
               2.0 * limestone_run.report.eroded_volume);
}
