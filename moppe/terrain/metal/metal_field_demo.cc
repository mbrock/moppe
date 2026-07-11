#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/geological.hh>
#include <moppe/terrain/metal/metal_evaluator.hh>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace {
  using Clock = std::chrono::steady_clock;

  template <typename Function>
  auto timed (Function&& function) {
    const auto start = Clock::now ();
    auto value = function ();
    const auto elapsed =
      std::chrono::duration<double> (Clock::now () - start).count ();
    return std::pair (std::move (value), elapsed);
  }
}

int main (int argc, char** argv) {
  try {
    using namespace moppe::terrain;
    const std::size_t side =
      argc > 1 ? static_cast<std::size_t> (std::strtoul (argv[1], nullptr, 10))
               : 513;
    const std::uint32_t seed =
      argc > 2
        ? static_cast<std::uint32_t> (std::strtoul (argv[2], nullptr, 10))
        : 123;
    const ScalarField field =
      make_geological_fields (make_geological_recipe (seed)).combined;
    const Domain2D domain { .width = side, .height = side };

    const auto [cpu, cpu_seconds] =
      timed ([&] { return CpuEvaluator ().evaluate (field, domain); });
    const metal::MetalEvaluator evaluator (MOPPE_SHADER_ASSET_PATH);
    const auto [cold_gpu, cold_seconds] =
      timed ([&] { return evaluator.evaluate (field, domain); });
    const auto [warm_gpu, warm_seconds] =
      timed ([&] { return evaluator.evaluate (field, domain); });

    float max_error = 0.0f;
    for (std::size_t i = 0; i < cpu.values ().size (); ++i)
      max_error = std::max (
        max_error, std::fabs (cpu.values ()[i] - warm_gpu.values ()[i]));

    std::cout << std::fixed << std::setprecision (4) << side << "x" << side
              << " geological field\n"
              << "CPU interpreter:       " << cpu_seconds << " s\n"
              << "Metal cold compile/run: " << cold_seconds << " s\n"
              << "Metal cached run:       " << warm_seconds << " s\n"
              << "Compiled pipelines:     "
              << evaluator.compiled_pipeline_count () << "\n"
              << "Maximum CPU/GPU error:  " << max_error << "\n";
    (void)cold_gpu;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "terrain Metal demo: " << error.what () << "\n";
    return 1;
  }
}
