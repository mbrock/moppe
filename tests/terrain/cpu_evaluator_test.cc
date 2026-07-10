#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/field.hh>

#include <tests/test.hh>

#include <numbers>

using namespace moppe::terrain;

MOPPE_TEST (coordinates_cover_the_requested_domain) {
  const Domain2D domain {
    .width = 3,
    .height = 3,
    .min_x = -1.0f,
    .max_x = 1.0f,
    .min_y = 10.0f,
    .max_y = 20.0f
  };
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
  const Domain2D domain { .width = 5, .height = 2 };
  const ScalarField wave = sin
    (2.0f * std::numbers::pi_v<float> * coordinate_x ());
  const ScalarRaster raster = CpuEvaluator ().evaluate (wave, domain);

  MOPPE_CHECK_NEAR (raster.at (0, 0), 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (raster.at (1, 0), 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (raster.at (2, 0), 0.0f, 1e-5f);
  MOPPE_CHECK_NEAR (raster.at (3, 0), -1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (raster.at (4, 0), 0.0f, 1e-5f);
}

MOPPE_TEST (cpu_evaluator_rejects_degenerate_domains) {
  bool threw = false;
  try {
    const Domain2D domain { .width = 1, .height = 2 };
    (void) CpuEvaluator ().evaluate (constant (0.0f), domain);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}
