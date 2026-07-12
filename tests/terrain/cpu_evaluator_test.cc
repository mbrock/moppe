#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/field.hh>

#include <tests/test.hh>

#include <atomic>
#include <cmath>
#include <numbers>

using namespace moppe::terrain;

MOPPE_TEST (coordinates_cover_the_requested_domain) {
  const FieldSamplingGrid2D domain { .width = 3,
                                     .height = 3,
                                     .min_x = -1.0f,
                                     .max_x = 1.0f,
                                     .min_y = 10.0f,
                                     .max_y = 20.0f };
  const CpuEvaluator evaluator;
  const ScalarRaster x = evaluator.evaluate (coordinate_x (), domain);
  const ScalarRaster y = evaluator.evaluate (coordinate_y (), domain);

  MOPPE_CHECK_NEAR (x.at (0, 0), -1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (x.at (1, 1), 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (x.at (2, 2), 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (y.at (0, 0), 10.0f, 1e-6f);
  MOPPE_CHECK_NEAR (y.at (1, 1), 15.0f, 1e-6f);
  MOPPE_CHECK_NEAR (y.at (2, 2), 20.0f, 1e-6f);
}

MOPPE_TEST (cpu_evaluator_composes_arithmetic_and_sine) {
  const FieldSamplingGrid2D domain { .width = 5, .height = 2 };
  const ScalarField wave =
    sin (2.0f * std::numbers::pi_v<float> * coordinate_x ());
  const ScalarRaster raster = CpuEvaluator ().evaluate (wave, domain);

  MOPPE_CHECK_NEAR (raster.at (0, 0), 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (raster.at (1, 0), 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (raster.at (2, 0), 0.0f, 1e-5f);
  MOPPE_CHECK_NEAR (raster.at (3, 0), -1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (raster.at (4, 0), 0.0f, 1e-5f);
}

MOPPE_TEST (large_cpu_evaluation_preserves_field_values) {
  const FieldSamplingGrid2D domain { .width = 257, .height = 257 };
  const ScalarField field = coordinate_x () + 2.0f * coordinate_y ();
  const ScalarRaster raster = CpuEvaluator ().evaluate (field, domain);

  MOPPE_CHECK_NEAR (raster.at (0, 0), 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (raster.at (128, 128), 1.5f, 1e-6f);
  MOPPE_CHECK_NEAR (raster.at (256, 256), 3.0f, 1e-6f);
}

MOPPE_TEST (cpu_evaluator_reports_materialization_progress) {
  const FieldSamplingGrid2D domain { .width = 257, .height = 257 };
  std::atomic<std::size_t> latest = 0;
  std::atomic<bool> correct_total = true;
  const CpuEvaluator evaluator ([&] (std::size_t completed, std::size_t total) {
    if (total != 257)
      correct_total = false;
    std::size_t observed = latest.load ();
    while (observed < completed &&
           !latest.compare_exchange_weak (observed, completed)) {}
  });

  (void)evaluator.evaluate (coordinate_x (), domain);

  MOPPE_CHECK (correct_total);
  MOPPE_CHECK (latest == 257);
}

MOPPE_TEST (cpu_evaluator_rejects_degenerate_domains) {
  bool threw = false;
  try {
    const FieldSamplingGrid2D domain { .width = 1, .height = 2 };
    (void)CpuEvaluator ().evaluate (constant (0.0f), domain);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}

MOPPE_TEST (normalization_is_an_explicit_materialization_barrier) {
  const FieldSamplingGrid2D domain { .width = 3, .height = 2 };
  const ScalarRaster ramp =
    CpuEvaluator ().evaluate (2.0f + 4.0f * coordinate_x (), domain);
  const ScalarRaster normalized = normalize (ramp);

  MOPPE_CHECK_NEAR (normalized.at (0, 0), 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (normalized.at (1, 0), 0.5f, 1e-6f);
  MOPPE_CHECK_NEAR (normalized.at (2, 0), 1.0f, 1e-6f);
}

MOPPE_TEST (multiply_add_preserves_fused_float_semantics) {
  const float a = 0.15f;
  const float b = 0.1234567f;
  const float c = 0.7654321f;
  const ScalarField field =
    multiply_add (constant (a), constant (b), constant (c));
  const ScalarRaster raster =
    CpuEvaluator ().evaluate (field, { .width = 2, .height = 2 });

  MOPPE_CHECK (raster.at (0, 0) == std::fma (a, b, c));
}

MOPPE_TEST (periodic_fractal_noise_matches_on_both_seams) {
  const ScalarField x = coordinate_x ();
  const ScalarField y = coordinate_y ();
  const ScalarField field = periodic_fbm_noise (
    Seed { 42 }, x * 3.0f + 0.37f, y * 3.0f - 0.21f, 3, 3, 4, 2, 0.5f);
  const ScalarRaster raster =
    CpuEvaluator ().evaluate (field, { .width = 33, .height = 29 });

  for (std::size_t row = 0; row < raster.domain ().height; ++row)
    MOPPE_CHECK_NEAR (raster.at (0, row), raster.at (32, row), 1e-6f);
  for (std::size_t column = 0; column < raster.domain ().width; ++column)
    MOPPE_CHECK_NEAR (raster.at (column, 0), raster.at (column, 28), 1e-6f);
}
