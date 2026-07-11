#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/geological.hh>
#include <moppe/terrain/metal/metal_evaluator.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>

namespace {
  using namespace moppe::terrain;

  void check_rasters_near (const ScalarRaster& cpu,
                           const ScalarRaster& gpu,
                           float tolerance) {
    MOPPE_CHECK (cpu.values ().size () == gpu.values ().size ());
    for (std::size_t i = 0; i < cpu.values ().size (); ++i)
      MOPPE_CHECK_NEAR (cpu.values ()[i], gpu.values ()[i], tolerance);
  }

  ScalarField parameterized_wave (float frequency, float bias) {
    return sin (frequency * coordinate_x ()) + bias * coordinate_y ();
  }
}

MOPPE_TEST (metal_stitching_matches_pointwise_cpu_operations) {
  using namespace moppe::terrain;
  const Domain2D domain { .width = 31,
                          .height = 19,
                          .min_x = -1.0f,
                          .max_x = 2.0f,
                          .min_y = -0.5f,
                          .max_y = 0.75f };
  const ScalarField field = smoothstep (
    -0.3f,
    0.8f,
    multiply_add (
      sin (coordinate_x ()), coordinate_y (), 0.25f - 1.7f * coordinate_x ()));
  const ScalarRaster cpu = CpuEvaluator ().evaluate (field, domain);
  const metal::MetalEvaluator evaluator (MOPPE_SHADER_ASSET_PATH);
  const ScalarRaster gpu = evaluator.evaluate (field, domain);

  check_rasters_near (cpu, gpu, 2e-6f);
  MOPPE_CHECK (evaluator.compiled_pipeline_count () == 1);
}

MOPPE_TEST (metal_stitching_accepts_minimal_graphs) {
  using namespace moppe::terrain;
  const Domain2D domain { .width = 7, .height = 5 };
  const metal::MetalEvaluator evaluator (MOPPE_SHADER_ASSET_PATH);

  const ScalarRaster x = evaluator.evaluate (coordinate_x (), domain);
  MOPPE_CHECK_NEAR (x.at (0, 2), 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (x.at (6, 2), 1.0f, 1e-6f);
  const ScalarRaster value = evaluator.evaluate (constant (0.375f), domain);
  for (float sample : value.values ())
    MOPPE_CHECK_NEAR (sample, 0.375f, 1e-6f);
  MOPPE_CHECK (evaluator.compiled_pipeline_count () == 2);
}

MOPPE_TEST (metal_pipeline_cache_ignores_parameter_values) {
  using namespace moppe::terrain;
  const metal::MetalEvaluator evaluator (MOPPE_SHADER_ASSET_PATH);
  const Domain2D domain { .width = 17, .height = 11 };

  const ScalarField first = parameterized_wave (2.0f, 0.2f);
  const ScalarRaster first_gpu = evaluator.evaluate (first, domain);
  check_rasters_near (
    CpuEvaluator ().evaluate (first, domain), first_gpu, 2e-6f);
  const ScalarField second = parameterized_wave (7.5f, -0.4f);
  const ScalarRaster second_gpu = evaluator.evaluate (second, domain);
  check_rasters_near (
    CpuEvaluator ().evaluate (second, domain), second_gpu, 2e-6f);

  MOPPE_CHECK (evaluator.compiled_pipeline_count () == 1);
}

MOPPE_TEST (metal_stitching_matches_all_noise_families) {
  using namespace moppe::terrain;
  const ScalarField x = coordinate_x () * 3.0f - 0.31f;
  const ScalarField y = coordinate_y () * 2.0f + 0.17f;
  const ScalarField field = perlin_noise (11, x, y) +
                            fbm_noise (12, x, y, 3, 2.1f, 0.47f) +
                            ridged_noise (13, x, y, 4, 1.9f, 0.52f) +
                            periodic_fbm_noise (14, x, y, 3, 2, 3, 2, 0.5f) +
                            periodic_ridged_noise (15, x, y, 3, 2, 3, 2, 0.5f);
  const Domain2D domain { .width = 37, .height = 29 };
  const ScalarRaster cpu = CpuEvaluator ().evaluate (field, domain);
  const metal::MetalEvaluator evaluator (MOPPE_SHADER_ASSET_PATH);
  const ScalarRaster gpu = evaluator.evaluate (field, domain);

  check_rasters_near (cpu, gpu, 3e-5f);
}

MOPPE_TEST (metal_stitching_materializes_the_geological_recipe) {
  using namespace moppe::terrain;
  const ScalarField field =
    make_geological_fields (make_geological_recipe (123)).combined;
  const Domain2D domain { .width = 65, .height = 65 };
  const ScalarRaster cpu = CpuEvaluator ().evaluate (field, domain);
  const metal::MetalEvaluator evaluator (MOPPE_SHADER_ASSET_PATH);
  const ScalarRaster gpu = evaluator.evaluate (field, domain);

  check_rasters_near (cpu, gpu, 5e-5f);
  for (std::size_t y = 0; y < domain.height; ++y)
    MOPPE_CHECK_NEAR (gpu.at (0, y), gpu.at (domain.width - 1, y), 2e-6f);
  for (std::size_t x = 0; x < domain.width; ++x)
    MOPPE_CHECK_NEAR (gpu.at (x, 0), gpu.at (x, domain.height - 1), 2e-6f);
}
