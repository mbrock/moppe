#include <moppe/game/graphics_benchmark.hh>
#include <moppe/game/launch_options.hh>

#include <tests/test.hh>

#include <cstdlib>
#include <string>
#include <vector>

using namespace moppe;

namespace {
  // The parser reads argv; a test spells its command line as strings and
  // lends their storage for the call.
  bool parse (std::vector<std::string> arguments,
              game::LaunchOptions& options,
              std::string& error) {
    arguments.insert (arguments.begin (), "moppe");
    std::vector<char*> argv;
    for (std::string& argument : arguments)
      argv.push_back (argument.data ());
    return game::parse_launch_options (
      static_cast<int> (argv.size ()), argv.data (), options, error);
  }

  game::LaunchOptions parsed (std::vector<std::string> arguments) {
    game::LaunchOptions options;
    std::string error;
    MOPPE_CHECK (parse (std::move (arguments), options, error));
    return options;
  }
}

MOPPE_TEST (launch_defaults_to_an_activated_play_window) {
  const game::LaunchOptions options = parsed ({});
  MOPPE_CHECK (options.generation_profile ==
               terrain::TerrainGenerationProfile::Play);
  MOPPE_CHECK (options.config.activate);
  MOPPE_CHECK (!options.config.capture_frames);
  MOPPE_CHECK (!options.config.frame_interpolation);
  MOPPE_CHECK_NEAR (options.config.drawable_scale, 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (options.graphics.render_scale_override, 0.5f, 0.0f);
  MOPPE_CHECK (!options.graphics.motion_blur);
  MOPPE_CHECK (options.graphics.upscaling == render::UpscalingMode::Temporal);
  MOPPE_CHECK (options.screenshot_path.empty ());
  MOPPE_CHECK (!options.benchmark.has_value ());
  MOPPE_CHECK (options.world_cache.mode == game::WorldCacheMode::Reuse);
  MOPPE_CHECK (options.world_cache.key.empty ());
  // Unresolved: main recalls the last played seed for an ordinary launch.
  MOPPE_CHECK (options.seed < 0);
}

MOPPE_TEST (launch_help_lists_every_supported_option_and_short_alias) {
  game::LaunchOptions options;
  std::string error;
  MOPPE_CHECK (parse ({ "-h" }, options, error));
  MOPPE_CHECK (options.show_help);

  const std::string help = game::launch_options_help ("/tmp/moppe");
  MOPPE_CHECK (help.starts_with ("Usage: moppe [options]"));
  for (const char* option : {
         "--help",
         "--fullscreen",
         "--windowed",
         "--graphics-quality",
         "--upscaling",
         "--frame-interpolation",
         "--msaa",
         "--render-scale",
         "--scene-megapixels",
         "--drawable-scale",
         "--graphics-enable",
         "--graphics-disable",
         "--graphics-benchmark",
         "--benchmark-frames",
         "--benchmark-settle",
         "--benchmark-prelude",
         "--terrain-gazetteer",
         "--gazetteer-settle",
         "--fast",
         "--terrain-quality",
         "--uplift-years",
         "--world-cache-key",
         "--refresh-world-cache",
         "--no-world-cache",
         "--screenshot",
         "--water-screenshot",
         "--window-size",
         "--inactive",
         "--seed",
       })
    MOPPE_CHECK (help.find (option) != std::string::npos);
  MOPPE_CHECK (help.find ("-h, --help") != std::string::npos);
  MOPPE_CHECK (help.find ("Graphics features:") != std::string::npos);
  MOPPE_CHECK (help.find ("undergrowth") != std::string::npos);
}

MOPPE_TEST (launch_quality_flags_select_settings) {
  MOPPE_CHECK (!parsed ({ "--graphics-quality", "low" }).graphics.bloom);
  MOPPE_CHECK (parsed ({ "--graphics-quality", "high" }).graphics.bloom);
  MOPPE_CHECK (
    parsed ({ "--graphics-quality", "high", "--graphics-disable", "bloom" })
      .graphics.bloom == false);
  MOPPE_CHECK (
    parsed ({ "--terrain-quality", "research" }).generation_profile ==
    terrain::TerrainGenerationProfile::Research);
  MOPPE_CHECK (parsed ({ "--terrain-quality", "smoke" }).generation_profile ==
               terrain::TerrainGenerationProfile::Smoke);
  MOPPE_CHECK (parsed ({ "--upscaling", "linear" }).graphics.upscaling ==
               render::UpscalingMode::Linear);
  MOPPE_CHECK (parsed ({ "--upscaling", "temporal" }).graphics.upscaling ==
               render::UpscalingMode::Temporal);
  MOPPE_CHECK (
    parsed ({ "--frame-interpolation", "on" }).config.frame_interpolation);
  MOPPE_CHECK (
    !parsed ({ "--frame-interpolation", "off" }).config.frame_interpolation);
  const game::LaunchOptions high = parsed ({ "--graphics-quality", "high" });
  MOPPE_CHECK_NEAR (high.graphics.render_scale_override, 0.0f, 0.0f);
  MOPPE_CHECK (high.graphics.motion_blur);
  MOPPE_CHECK (
    parsed ({ "--graphics-quality", "balanced", "--upscaling", "linear" })
      .graphics.upscaling == render::UpscalingMode::Linear);
  const game::LaunchOptions scales =
    parsed ({ "--drawable-scale", "0.5", "--render-scale", "0.375" });
  MOPPE_CHECK_NEAR (scales.config.drawable_scale, 0.5f, 0.0f);
  MOPPE_CHECK_NEAR (scales.graphics.render_scale_override, 0.375f, 0.0f);
  const game::LaunchOptions raster =
    parsed ({ "--msaa", "2", "--scene-megapixels", "1.25" });
  MOPPE_CHECK (raster.config.msaa_samples == 2);
  MOPPE_CHECK (raster.scene_megapixel_budget.has_value ());
  MOPPE_CHECK_NEAR (*raster.scene_megapixel_budget, 1.25f, 0.0f);

  // A preset is the baseline regardless of where it appears; explicit
  // settings must not disappear merely because the preset was spelled last.
  const game::LaunchOptions trailing_preset = parsed ({
    "--render-scale",
    "0.25",
    "--graphics-enable",
    "bloom",
    "--graphics-quality",
    "low",
  });
  MOPPE_CHECK_NEAR (
    trailing_preset.graphics.render_scale_override, 0.25f, 0.0f);
  MOPPE_CHECK (trailing_preset.graphics.bloom);
}

MOPPE_TEST (launch_rejects_malformed_command_lines) {
  // A rejected command line leaves its options half-written, so each case
  // starts from a fresh launch rather than the wreckage of the last.
  const auto rejects = [] (std::vector<std::string> arguments) {
    game::LaunchOptions options;
    std::string error;
    return !parse (std::move (arguments), options, error) && !error.empty ();
  };
  MOPPE_CHECK (rejects ({ "--graphics-quality" }));
  MOPPE_CHECK (rejects ({ "--graphics-quality", "medium" }));
  MOPPE_CHECK (rejects ({ "--upscaling", "neural" }));
  MOPPE_CHECK (rejects ({ "--frame-interpolation", "maybe" }));
  MOPPE_CHECK (rejects ({ "--msaa", "0" }));
  MOPPE_CHECK (rejects ({ "--msaa", "8" }));
  MOPPE_CHECK (rejects ({ "--msaa", "2x" }));
  MOPPE_CHECK (rejects ({ "--scene-megapixels", "65" }));
  MOPPE_CHECK (rejects ({ "--drawable-scale", "0.1" }));
  MOPPE_CHECK (rejects ({ "--drawable-scale", "half" }));
  MOPPE_CHECK (rejects ({ "--render-scale", "1.1" }));
  MOPPE_CHECK (rejects ({ "--terrain-quality", "sculpted" }));
  MOPPE_CHECK (rejects ({ "--uplift-years" }));
  MOPPE_CHECK (rejects ({ "--uplift-years", "ancient" }));
  MOPPE_CHECK (rejects ({ "--uplift-years", "-1" }));
  MOPPE_CHECK (rejects ({ "--uplift-years", "10000001" }));
  MOPPE_CHECK (rejects ({ "--world-cache-key" }));
  MOPPE_CHECK (rejects ({ "--world-cache-key", "../shared" }));
  MOPPE_CHECK (rejects ({ "--world-cache-key", "spaces are unsafe" }));
  MOPPE_CHECK (rejects ({ "--water-screenshot", "canyon", "/tmp/a.png" }));
  MOPPE_CHECK (rejects ({ "--water-screenshot", "mouth" }));
  MOPPE_CHECK (rejects ({ "--screenshot" }));
  MOPPE_CHECK (rejects ({ "--seed" }));
  MOPPE_CHECK (rejects ({ "--graphics-enable", "not-a-feature" }));
  // Benchmark pacing without a destination has nowhere to write.
  MOPPE_CHECK (rejects ({ "--benchmark-frames", "10" }));
  MOPPE_CHECK (rejects ({ "--gazetteer-settle", "10" }));
  // An unrecognized argument is not an error; it is simply not a setting.
  MOPPE_CHECK (!rejects ({ "--not-a-flag" }));
}

MOPPE_TEST (launch_selects_stable_whole_world_cache_policy) {
  const game::LaunchOptions named =
    parsed ({ "--world-cache-key", "terrain-tuning_3" });
  MOPPE_CHECK (named.world_cache.mode == game::WorldCacheMode::Reuse);
  MOPPE_CHECK (named.world_cache.key == "terrain-tuning_3");

  MOPPE_CHECK (parsed ({ "--refresh-world-cache" }).world_cache.mode ==
               game::WorldCacheMode::Refresh);
  MOPPE_CHECK (parsed ({ "--no-world-cache" }).world_cache.mode ==
               game::WorldCacheMode::Disabled);
}

MOPPE_TEST (launch_captures_pin_a_seed_and_stay_out_of_the_way) {
  const game::LaunchOptions shot = parsed ({ "--screenshot", "/tmp/shot.png" });
  MOPPE_CHECK (shot.screenshot_path == "/tmp/shot.png");
  MOPPE_CHECK (!shot.config.fullscreen);
  MOPPE_CHECK (shot.config.capture_frames);
  MOPPE_CHECK (!shot.config.activate);
  MOPPE_CHECK (shot.seed == 123);

  // An explicit seed still wins over the capture default.
  MOPPE_CHECK (
    parsed ({ "--screenshot", "/tmp/shot.png", "--seed", "7" }).seed == 7);

  const game::LaunchOptions water =
    parsed ({ "--water-screenshot", "mouth", "/tmp/mouth.png" });
  MOPPE_CHECK (water.water_shot == game::WaterShot::Mouth);
  MOPPE_CHECK (water.screenshot_path == "/tmp/mouth.png");

  const game::LaunchOptions gazetteer = parsed (
    { "--terrain-gazetteer", "/tmp/gazetteer", "--gazetteer-settle", "7" });
  MOPPE_CHECK (gazetteer.gazetteer.has_value ());
  MOPPE_CHECK (gazetteer.gazetteer->output_directory == "/tmp/gazetteer");
  MOPPE_CHECK (gazetteer.gazetteer->settle_frames == 7);
  MOPPE_CHECK (!gazetteer.config.fullscreen);
  MOPPE_CHECK (!gazetteer.config.activate);
  MOPPE_CHECK (gazetteer.config.capture_frames);
  MOPPE_CHECK (gazetteer.seed == 123);
}

MOPPE_TEST (launch_resolutions_follow_the_generation_they_serve) {
  MOPPE_CHECK (parsed ({ "--fast" }).world.resolution == 1024);
  MOPPE_CHECK (parsed ({ "--terrain-quality", "fast" }).world.resolution ==
               1024);
  MOPPE_CHECK (parsed ({ "--terrain-quality", "smoke" }).world.resolution ==
               1024);
  MOPPE_CHECK (parsed ({}).world.resolution != 1024);
}

MOPPE_TEST (launch_benchmark_pacing_survives_flag_order) {
  const game::LaunchOptions options = parsed ({ "--benchmark-frames",
                                                "24",
                                                "--graphics-benchmark",
                                                "/tmp/gpu.csv",
                                                "--benchmark-pass-timing",
                                                "--benchmark-settle",
                                                "5" });
  MOPPE_CHECK (options.benchmark.has_value ());
  MOPPE_CHECK (options.benchmark->output_path == "/tmp/gpu.csv");
  MOPPE_CHECK (options.benchmark->measured_frames == 24);
  MOPPE_CHECK (options.benchmark->settle_frames == 5);
  MOPPE_CHECK (options.benchmark->prelude_frames == 480);
  MOPPE_CHECK (options.benchmark->pass_timing);
  MOPPE_CHECK (!options.config.fullscreen);
  MOPPE_CHECK (!options.config.activate);
}

MOPPE_TEST (launch_recipe_carries_the_resolved_seed_and_profile) {
  game::LaunchOptions options =
    parsed ({ "--terrain-quality", "fast", "--uplift-years", "750000" });
  options.seed = 4321;
  const terrain::WorldRecipe recipe = game::make_launch_recipe (options);
  MOPPE_CHECK (recipe.seed ().value == 4321u);
  MOPPE_CHECK (recipe.generation_profile () ==
               terrain::TerrainGenerationProfile::Fast);
  MOPPE_CHECK (recipe.resolution () == options.world.resolution);
  MOPPE_CHECK (recipe.evolution ().uplift_duration ==
               750000.0f * mp_units::astronomy::Julian_year);
}

MOPPE_TEST (launch_benchmark_environment_reaches_the_backend) {
  game::GraphicsBenchmarkConfig benchmark;
  benchmark.output_path = "/tmp/gpu.csv";
  benchmark.measured_frames = 2;
  game::publish_benchmark_environment (benchmark);
  MOPPE_CHECK (std::string (::getenv ("MOPPE_BENCHMARK_OUTPUT")) ==
               "/tmp/gpu.csv");
  const int expected = benchmark.measured_frames *
                       (1 << game::graphics_benchmark_dimension_count ());
  MOPPE_CHECK (std::atoi (::getenv ("MOPPE_BENCHMARK_EXPECTED")) == expected);
  MOPPE_CHECK (
    std::string (::getenv ("MOPPE_BENCHMARK_FEATURES")).find ("bloom") !=
    std::string::npos);
  MOPPE_CHECK (::getenv ("MOPPE_BENCHMARK_PASSES") == nullptr);

  benchmark.pass_timing = true;
  game::publish_benchmark_environment (benchmark);
  MOPPE_CHECK (::getenv ("MOPPE_BENCHMARK_PASSES") != nullptr);
}
