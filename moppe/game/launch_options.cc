#include <moppe/game/launch_options.hh>

#include <moppe/game/graphics_benchmark.hh>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace moppe::game {
  namespace {
    GraphicsBenchmarkConfig& benchmark_config (LaunchOptions& options) {
      if (!options.benchmark)
        options.benchmark = GraphicsBenchmarkConfig {};
      return *options.benchmark;
    }

    int frame_count (const char* value) {
      return std::max (1, std::atoi (value));
    }

    // A capture keeps its own window, since an automated run should never
    // take over the display it was started from.
    void capture_to (LaunchOptions& options, const char* path) {
      options.screenshot_path = path;
      options.config.fullscreen = false;
    }

    bool unknown (const char* what, const char* value, std::string& error) {
      error = std::string ("unknown ") + what + ": " + value;
      return false;
    }

    // One recognized flag: how many values it consumes, what to say when they
    // are missing, and what it means for the launch.  Keeping the arity beside
    // the handler is what lets the parse loop bounds-check every flag once
    // rather than every handler repeating the same test.
    struct Flag {
      std::string_view name;
      int arity;
      const char* expects;
      bool (*apply) (LaunchOptions&, const char* const*, std::string&);
    };

    constexpr Flag flags[] = {
      { "--fullscreen",
        0,
        "",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.config.fullscreen = true;
          return true;
        } },
      { "--windowed",
        0,
        "",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.config.fullscreen = false;
          return true;
        } },
      { "--graphics-quality",
        1,
        "low, balanced, or high",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          const std::string_view quality = values[0];
          if (quality == "low")
            options.graphics = low_graphics_settings ();
          else if (quality == "balanced")
            options.graphics = balanced_graphics_settings ();
          else if (quality == "high")
            options.graphics = high_graphics_settings ();
          else
            return unknown ("graphics quality", values[0], error);
          return true;
        } },
      { "--graphics-enable",
        1,
        "a comma-separated feature list",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          return set_graphics_features (
            options.graphics, values[0], true, error);
        } },
      { "--graphics-disable",
        1,
        "a comma-separated feature list",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          return set_graphics_features (
            options.graphics, values[0], false, error);
        } },
      { "--graphics-benchmark",
        1,
        "a CSV path",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          benchmark_config (options).output_path = values[0];
          options.config.fullscreen = false;
          return true;
        } },
      { "--benchmark-frames",
        1,
        "a positive frame count",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          benchmark_config (options).measured_frames = frame_count (values[0]);
          return true;
        } },
      { "--benchmark-settle",
        1,
        "a positive frame count",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          benchmark_config (options).settle_frames = frame_count (values[0]);
          return true;
        } },
      { "--benchmark-prelude",
        1,
        "a positive frame count",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          benchmark_config (options).prelude_frames = frame_count (values[0]);
          return true;
        } },
      { "--fast",
        0,
        "",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.generation_profile = terrain::TerrainGenerationProfile::Fast;
          return true;
        } },
      { "--terrain-quality",
        1,
        "smoke, fast, play, or research",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          const std::string_view quality = values[0];
          if (quality == "smoke")
            options.generation_profile =
              terrain::TerrainGenerationProfile::Smoke;
          else if (quality == "fast")
            options.generation_profile =
              terrain::TerrainGenerationProfile::Fast;
          else if (quality == "play")
            options.generation_profile =
              terrain::TerrainGenerationProfile::Play;
          else if (quality == "research")
            options.generation_profile =
              terrain::TerrainGenerationProfile::Research;
          else
            return unknown ("terrain quality", values[0], error);
          return true;
        } },
      { "--tree-demo",
        0,
        "",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.tree_demo = true;
          return true;
        } },
      { "--tree-count",
        1,
        "an integer from 1 to 64",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          const int count = std::atoi (values[0]);
          if (count < 1 || count > 64) {
            error = "--tree-count must be between 1 and 64";
            return false;
          }
          options.tree_count = static_cast<std::size_t> (count);
          return true;
        } },
      { "--tree-screenshot",
        1,
        "a PNG path",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          options.tree_demo = true;
          capture_to (options, values[0]);
          return true;
        } },
      { "--screenshot",
        1,
        "a PNG path",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          capture_to (options, values[0]);
          return true;
        } },
      { "--water-screenshot",
        2,
        "a feature and PNG path",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          options.water_shot = parse_water_shot (values[0]);
          if (!options.water_shot) {
            error = "unknown water feature: " + std::string (values[0]) +
                    " (use stream, river, confluence, mouth, waterfall, "
                    "or lake)";
            return false;
          }
          capture_to (options, values[1]);
          return true;
        } },
      { "--seed",
        1,
        "an integer",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          options.seed = std::atoi (values[0]);
          return true;
        } },
    };

    const Flag* find_flag (std::string_view name) {
      for (const Flag& flag : flags)
        if (flag.name == name)
          return &flag;
      return nullptr;
    }

    // What the flags imply once they have all been seen.  Resolving these
    // once here is what keeps every later reader of a launch from re-deriving
    // them, and it is why the order flags arrive in does not matter.
    bool resolve_launch_settings (LaunchOptions& options, std::string& error) {
      if (options.benchmark && options.benchmark->output_path.empty ()) {
        error = "--graphics-benchmark is required with benchmark options";
        return false;
      }
      if (options.generation_profile ==
            terrain::TerrainGenerationProfile::Smoke ||
          options.generation_profile == terrain::TerrainGenerationProfile::Fast)
        options.world.resolution = 1024;
      options.config.capture_frames = !options.screenshot_path.empty () ||
                                      ::getenv ("MOPPE_CINEMATIC_CAPTURE_DIR");
      // An automated run stays behind whatever the developer is looking at.
      options.config.activate =
        !options.config.capture_frames && !options.benchmark;
      // A capture pins its own seed so repeated runs compare like with like.
      if (!options.screenshot_path.empty () && options.seed < 0)
        options.seed = 123;
      return true;
    }
  }

  bool parse_launch_options (int argc,
                             char** argv,
                             LaunchOptions& options,
                             std::string& error) {
    for (int i = 1; i < argc; ++i) {
      // An unrecognized argument is not an error: hosts append their own.
      const Flag* flag = find_flag (argv[i]);
      if (!flag)
        continue;
      if (i + flag->arity >= argc) {
        error = std::string (argv[i]) + " requires " + flag->expects;
        return false;
      }
      if (!flag->apply (options, argv + i + 1, error))
        return false;
      i += flag->arity;
    }
    return resolve_launch_settings (options, error);
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
                                       seed,
                                       options.world.water_level,
                                       options.generation_profile);
  }
}
