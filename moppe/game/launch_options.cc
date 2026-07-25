#include <moppe/game/launch_options.hh>

#include <moppe/game/graphics_benchmark.hh>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace moppe::game {
  namespace {
    bool needs_value (const std::string& arg,
                      int index,
                      int argc,
                      const char* what,
                      std::string& error) {
      if (index + 1 < argc)
        return true;
      error = arg + " requires " + what;
      return false;
    }
  }

  bool parse_launch_options (int argc,
                             char** argv,
                             LaunchOptions& options,
                             std::string& error) {
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--fullscreen") {
        options.config.fullscreen = true;
      } else if (arg == "--windowed") {
        options.config.fullscreen = false;
      } else if (arg == "--graphics-quality") {
        if (!needs_value (arg, i, argc, "low, balanced, or high", error))
          return false;
        const std::string quality = argv[++i];
        if (quality == "low")
          options.graphics = low_graphics_settings ();
        else if (quality == "balanced")
          options.graphics = balanced_graphics_settings ();
        else if (quality == "high")
          options.graphics = high_graphics_settings ();
        else {
          error = "unknown graphics quality: " + quality;
          return false;
        }
      } else if (arg == "--graphics-enable" || arg == "--graphics-disable") {
        if (!needs_value (
              arg, i, argc, "a comma-separated feature list", error))
          return false;
        const bool enabled = arg == "--graphics-enable";
        if (!set_graphics_features (
              options.graphics, argv[++i], enabled, error))
          return false;
      } else if (arg == "--graphics-benchmark") {
        if (!needs_value (arg, i, argc, "a CSV path", error))
          return false;
        if (!options.benchmark)
          options.benchmark = GraphicsBenchmarkConfig {};
        options.benchmark->output_path = argv[++i];
        options.config.fullscreen = false;
      } else if (arg == "--benchmark-frames" || arg == "--benchmark-settle" ||
                 arg == "--benchmark-prelude") {
        if (!needs_value (arg, i, argc, "a positive frame count", error))
          return false;
        if (!options.benchmark)
          options.benchmark = GraphicsBenchmarkConfig {};
        const int value = std::max (1, std::atoi (argv[++i]));
        if (arg == "--benchmark-frames")
          options.benchmark->measured_frames = value;
        else if (arg == "--benchmark-settle")
          options.benchmark->settle_frames = value;
        else
          options.benchmark->prelude_frames = value;
      } else if (arg == "--fast") {
        options.generation_profile = terrain::TerrainGenerationProfile::Fast;
      } else if (arg == "--terrain-quality") {
        if (!needs_value (arg, i, argc, "fast, play, or research", error))
          return false;
        const std::string quality = argv[++i];
        if (quality == "fast")
          options.generation_profile = terrain::TerrainGenerationProfile::Fast;
        else if (quality == "play")
          options.generation_profile = terrain::TerrainGenerationProfile::Play;
        else if (quality == "research")
          options.generation_profile =
            terrain::TerrainGenerationProfile::Research;
        else {
          error = "unknown terrain quality: " + quality;
          return false;
        }
      } else if (arg == "--terrain-lab") {
        options.start_in_terrain_lab = true;
      } else if (arg == "--tree-demo") {
        options.tree_demo = true;
      } else if (arg == "--tree-count") {
        if (!needs_value (arg, i, argc, "an integer from 1 to 64", error))
          return false;
        const int count = std::atoi (argv[++i]);
        if (count < 1 || count > 64) {
          error = "--tree-count must be between 1 and 64";
          return false;
        }
        options.tree_count = static_cast<std::size_t> (count);
      } else if (arg == "--tree-screenshot") {
        if (!needs_value (arg, i, argc, "a PNG path", error))
          return false;
        options.tree_demo = true;
        options.screenshot_path = argv[++i];
        options.config.fullscreen = false;
      } else if (arg == "--terrain-lab-screenshot") {
        if (!needs_value (arg, i, argc, "a PNG path", error))
          return false;
        options.screenshot_path = argv[++i];
        options.start_in_terrain_lab = true;
        options.config.fullscreen = false;
        options.world.resolution = 1025;
      } else if (arg == "--screenshot") {
        if (!needs_value (arg, i, argc, "a PNG path", error))
          return false;
        options.screenshot_path = argv[++i];
        options.config.fullscreen = false;
      } else if (arg == "--water-screenshot") {
        if (i + 2 >= argc) {
          error = "--water-screenshot requires a feature and PNG path";
          return false;
        }
        const std::string feature = argv[++i];
        options.water_shot = parse_water_shot (feature);
        if (!options.water_shot) {
          error = "unknown water feature: " + feature +
                  " (use stream, river, confluence, mouth, waterfall, "
                  "or lake)";
          return false;
        }
        options.screenshot_path = argv[++i];
        options.config.fullscreen = false;
      } else if (arg == "--seed") {
        if (!needs_value (arg, i, argc, "an integer", error))
          return false;
        options.seed = std::atoi (argv[++i]);
      }
    }

    if (options.benchmark && options.benchmark->output_path.empty ()) {
      error = "--graphics-benchmark is required with benchmark options";
      return false;
    }

    // Derived settings, resolved once here rather than re-tested by every
    // reader of the command line.
    if (options.generation_profile == terrain::TerrainGenerationProfile::Fast)
      options.world.resolution = 1025;
    options.config.capture_frames = !options.screenshot_path.empty () ||
                                    ::getenv ("MOPPE_CINEMATIC_CAPTURE_DIR");
    // An automated run stays behind whatever the developer is looking at.
    options.config.activate =
      !options.config.capture_frames && !options.benchmark;
    if (!options.screenshot_path.empty () && options.seed < 0)
      options.seed = 123;
    return true;
  }

  void
  publish_benchmark_environment (const GraphicsBenchmarkConfig& benchmark) {
    const int expected =
      benchmark.measured_frames * (1 << graphics_benchmark_dimension_count ());
    ::setenv ("MOPPE_BENCHMARK_OUTPUT", benchmark.output_path.c_str (), 1);
    const std::string expected_text = std::to_string (expected);
    ::setenv ("MOPPE_BENCHMARK_EXPECTED", expected_text.c_str (), 1);
    std::string feature_names;
    for (const GraphicsFeature* feature : graphics_features)
      if (graphics_benchmark_includes (*feature)) {
        if (!feature_names.empty ())
          feature_names += ',';
        feature_names += feature->name;
      }
    ::setenv ("MOPPE_BENCHMARK_FEATURES", feature_names.c_str (), 1);
  }

  terrain::WorldRecipe make_launch_recipe (const LaunchOptions& options) {
    const terrain::Seed seed { static_cast<std::uint32_t> (options.seed) };
    return terrain::make_world_recipe (options.world.map_size,
                                       options.world.resolution,
                                       options.world.topology (),
                                       seed,
                                       options.world.water_level,
                                       options.generation_profile);
  }
}
