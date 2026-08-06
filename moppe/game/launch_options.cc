#include <moppe/game/launch_options.hh>

#include <moppe/game/graphics_benchmark.hh>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string_view>
#include <vector>

namespace moppe::game {
  namespace {
    GraphicsBenchmarkConfig& benchmark_config (LaunchOptions& options) {
      if (!options.benchmark)
        options.benchmark = GraphicsBenchmarkConfig {};
      return *options.benchmark;
    }

    GazetteerCaptureConfig& gazetteer_config (LaunchOptions& options) {
      if (!options.gazetteer)
        options.gazetteer = GazetteerCaptureConfig {};
      return *options.gazetteer;
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

    bool parse_float_option (const char* value,
                             const char* option,
                             float minimum,
                             float maximum,
                             float& result,
                             std::string& error) {
      char* end = nullptr;
      errno = 0;
      const float parsed = std::strtof (value, &end);
      if (errno || end == value || *end != '\0' || !std::isfinite (parsed) ||
          parsed < minimum || parsed > maximum) {
        std::ostringstream message;
        message << option << " must be between " << minimum << " and "
                << maximum;
        error = message.str ();
        return false;
      }
      result = parsed;
      return true;
    }

    bool parse_scale (const char* value,
                      const char* option,
                      float& result,
                      std::string& error) {
      return parse_float_option (value, option, 0.25f, 1.0f, result, error);
    }

    bool parse_terrain_resolution (const char* value,
                                   int& result,
                                   std::string& error) {
      char* end = nullptr;
      errno = 0;
      const long parsed = std::strtol (value, &end, 10);
      if (errno || end == value || *end != '\0' || parsed < 128 ||
          parsed > 4096) {
        error = "--terrain-resolution must be an integer from 128 to 4096";
        return false;
      }
      result = static_cast<int> (parsed);
      return true;
    }

    bool set_world_cache_key (LaunchOptions& options,
                              const char* value,
                              std::string& error) {
      const std::string_view key = value;
      if (key.empty () || key.size () > 64 ||
          !std::all_of (key.begin (), key.end (), [] (unsigned char c) {
            return std::isalnum (c) || c == '-' || c == '_' || c == '.';
          })) {
        error = "--world-cache-key must be 1..64 letters, digits, '.', '_', "
                "or '-'";
        return false;
      }
      options.world_cache.key = key;
      return true;
    }

    // One recognized flag: how many values it consumes, what to say when they
    // are missing, and what it means for the launch.  Keeping the arity beside
    // the handler is what lets the parse loop bounds-check every flag once
    // rather than every handler repeating the same test.
    struct Flag {
      std::string_view name;
      std::string_view alias;
      int arity;
      const char* expects;
      const char* description;
      bool (*apply) (LaunchOptions&, const char* const*, std::string&);
    };

    constexpr Flag flags[] = {
      { "--help",
        "-h",
        0,
        "",
        "Show this help and exit.",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.show_help = true;
          return true;
        } },
      { "--fullscreen",
        "",
        0,
        "",
        "Present in a fullscreen Space.",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.config.fullscreen = true;
          return true;
        } },
      { "--windowed",
        "",
        0,
        "",
        "Present in a resizable window.",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.config.fullscreen = false;
          return true;
        } },
      { "--graphics-quality",
        "",
        1,
        "<low|balanced|high>",
        "Select a graphics preset.",
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
      { "--upscaling",
        "",
        1,
        "<linear|spatial|temporal>",
        "Select temporal/spatial MetalFX reconstruction or linear scaling.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          if (!parse_upscaling_mode (values[0], options.graphics.upscaling))
            return unknown ("upscaling mode", values[0], error);
          return true;
        } },
      { "--frame-interpolation",
        "",
        1,
        "<on|off>",
        "Enable or disable MetalFX frame interpolation on high-refresh macOS.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          const std::string_view value = values[0];
          if (value == "on")
            options.config.frame_interpolation = true;
          else if (value == "off")
            options.config.frame_interpolation = false;
          else
            return unknown ("frame interpolation mode", values[0], error);
          return true;
        } },
      { "--msaa",
        "",
        1,
        "<1|2|4>",
        "Set the scene multisample count before pipelines are built.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          char* end = nullptr;
          const long samples = std::strtol (values[0], &end, 10);
          if (end == values[0] || *end != '\0' ||
              (samples != 1 && samples != 2 && samples != 4)) {
            error = "--msaa must be 1, 2, or 4";
            return false;
          }
          options.config.msaa_samples = static_cast<int> (samples);
          return true;
        } },
      { "--render-scale",
        "",
        1,
        "<0.25..1>",
        "Set 3D scene dimensions relative to the drawable.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          return parse_scale (values[0],
                              "--render-scale",
                              options.graphics.render_scale_override,
                              error);
        } },
      { "--scene-megapixels",
        "",
        1,
        "<0..64>",
        "Cap desktop 3D scene area in megapixels; zero disables the cap.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          float budget = 0.0f;
          if (!parse_float_option (
                values[0], "--scene-megapixels", 0.0f, 64.0f, budget, error))
            return false;
          options.scene_megapixel_budget = budget;
          return true;
        } },
      { "--drawable-scale",
        "",
        1,
        "<0.25..1>",
        "Override the automatic macOS drawable backing-pixel scale.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          return parse_scale (values[0],
                              "--drawable-scale",
                              options.config.drawable_scale,
                              error);
        } },
      { "--graphics-enable",
        "",
        1,
        "<FEATURE,...>",
        "Enable named graphics features after preset selection.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          return set_graphics_features (
            options.graphics, values[0], true, error);
        } },
      { "--graphics-disable",
        "",
        1,
        "<FEATURE,...>",
        "Disable named graphics features after preset selection.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          return set_graphics_features (
            options.graphics, values[0], false, error);
        } },
      { "--graphics-benchmark",
        "",
        1,
        "<CSV>",
        "Run the partitioned graphics benchmark and write CSV.",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          benchmark_config (options).output_path = values[0];
          options.config.fullscreen = false;
          return true;
        } },
      { "--benchmark-frames",
        "",
        1,
        "<COUNT>",
        "Set measured frames per benchmark configuration.",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          benchmark_config (options).measured_frames = frame_count (values[0]);
          return true;
        } },
      { "--benchmark-settle",
        "",
        1,
        "<COUNT>",
        "Set settling frames between benchmark configurations.",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          benchmark_config (options).settle_frames = frame_count (values[0]);
          return true;
        } },
      { "--benchmark-prelude",
        "",
        1,
        "<COUNT>",
        "Set the benchmark ride prelude length.",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          benchmark_config (options).prelude_frames = frame_count (values[0]);
          return true;
        } },
      { "--benchmark-pass-timing",
        "",
        0,
        "",
        "Record precise Metal 4 pass timestamps in benchmark CSV.",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          benchmark_config (options).pass_timing = true;
          return true;
        } },
      { "--benchmark-partition",
        "",
        1,
        "<standard|detailed>",
        "Select the 32- or 128-configuration benchmark partition.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          GraphicsBenchmarkPartition partition;
          if (!parse_graphics_benchmark_partition (values[0], partition)) {
            error = "unknown graphics benchmark partition: " +
                    std::string (values[0]);
            return false;
          }
          benchmark_config (options).partition = partition;
          return true;
        } },
      { "--terrain-gazetteer",
        "",
        1,
        "<DIRECTORY>",
        "Capture the frozen landscape gazetteer.",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          gazetteer_config (options).output_directory = values[0];
          options.config.fullscreen = false;
          return true;
        } },
      { "--gazetteer-settle",
        "",
        1,
        "<COUNT>",
        "Set settling frames before each gazetteer image.",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          gazetteer_config (options).settle_frames = frame_count (values[0]);
          return true;
        } },
      { "--fast",
        "",
        0,
        "",
        "Use the fast terrain-generation profile.",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.generation_profile = terrain::TerrainGenerationProfile::Fast;
          return true;
        } },
      { "--terrain-quality",
        "",
        1,
        "<smoke|fast|play|research>",
        "Select the terrain-generation profile.",
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
      { "--terrain-resolution",
        "",
        1,
        "<SAMPLES>",
        "Override terrain samples per side for a resolution experiment.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          int resolution = 0;
          if (!parse_terrain_resolution (values[0], resolution, error))
            return false;
          options.terrain_resolution = resolution;
          return true;
        } },
      { "--uplift-years",
        "",
        1,
        "<YEARS>",
        "Override the terrain profile's tectonic forcing duration.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          float years = 0.0f;
          if (!parse_float_option (
                values[0], "--uplift-years", 0.0f, 10000000.0f, years, error))
            return false;
          options.uplift_duration = years * mp_units::astronomy::Julian_year;
          return true;
        } },
      { "--channel-initiation-area",
        "",
        1,
        "<SQUARE_METERS>",
        "Set the physical catchment scale where channel incision begins.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          float area = 0.0f;
          if (!parse_float_option (values[0],
                                   "--channel-initiation-area",
                                   1.0f,
                                   1000000000.0f,
                                   area,
                                   error))
            return false;
          options.channel_initiation_area =
            area * mp_units::si::metre * mp_units::si::metre;
          return true;
        } },
      { "--sediment-concentration",
        "",
        1,
        "<FRACTION>",
        "Set solid transport concentration at unit channel slope.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          float concentration = 0.0f;
          if (!parse_float_option (values[0],
                                   "--sediment-concentration",
                                   0.0f,
                                   1.0f,
                                   concentration,
                                   error))
            return false;
          options.sediment_concentration =
            concentration * terrain::sediment_concentration[mp_units::one];
          return true;
        } },
      { "--hillslope-critical-gradient",
        "",
        1,
        "<GRADIENT>",
        "Set the dimensionless gradient where bounded wasting saturates.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          float gradient = 0.0f;
          if (!parse_float_option (values[0],
                                   "--hillslope-critical-gradient",
                                   0.01f,
                                   10.0f,
                                   gradient,
                                   error))
            return false;
          options.critical_hillslope_gradient =
            gradient * proportion[mp_units::one];
          return true;
        } },
      { "--hillslope-max-multiplier",
        "",
        1,
        "<FACTOR>",
        "Bound the near-critical hillslope diffusivity multiplier.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          float multiplier = 0.0f;
          if (!parse_float_option (values[0],
                                   "--hillslope-max-multiplier",
                                   1.0f,
                                   16.0f,
                                   multiplier,
                                   error))
            return false;
          options.maximum_hillslope_multiplier =
            multiplier * proportion[mp_units::one];
          return true;
        } },
      { "--world-cache-key",
        "",
        1,
        "<NAME>",
        "Reuse a named whole-world cache across executable builds.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          return set_world_cache_key (options, values[0], error);
        } },
      { "--refresh-world-cache",
        "",
        0,
        "",
        "Rebuild and replace the selected whole-world cache.",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.world_cache.mode = WorldCacheMode::Refresh;
          return true;
        } },
      { "--no-world-cache",
        "",
        0,
        "",
        "Skip whole-world cache loading and saving for this launch.",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.world_cache.mode = WorldCacheMode::Disabled;
          return true;
        } },
      { "--screenshot",
        "",
        1,
        "<PNG>",
        "Capture one gameplay PNG and exit.",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          capture_to (options, values[0]);
          return true;
        } },
      { "--water-screenshot",
        "",
        2,
        "<FEATURE> <PNG>",
        "Capture stream, river, confluence, mouth, waterfall, or lake.",
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
      { "--window-size",
        "",
        1,
        "<WIDTHxHEIGHT>",
        "Set the windowed logical size.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          int width = 0;
          int height = 0;
          char separator = 0;
          std::istringstream text (values[0]);
          text >> width >> separator >> height;
          if (!text.eof () || (separator != 'x' && separator != 'X') ||
              width < 64 || height < 64)
            return unknown ("window size", values[0], error);
          options.config.width = width;
          options.config.height = height;
          options.config.fullscreen = false;
          return true;
        } },
      // Profiling a large window should not pull focus away from whatever
      // the developer is reading while it runs.
      { "--inactive",
        "",
        0,
        "",
        "Keep the window behind the active application.",
        [] (LaunchOptions& options, const char* const*, std::string&) {
          options.stay_inactive = true;
          return true;
        } },
      { "--seed",
        "",
        1,
        "<INTEGER>",
        "Select a deterministic world seed.",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          options.seed = std::atoi (values[0]);
          return true;
        } },
    };

    const Flag* find_flag (std::string_view name) {
      for (const Flag& flag : flags)
        if (flag.name == name || (!flag.alias.empty () && flag.alias == name))
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
      if (options.gazetteer && options.gazetteer->output_directory.empty ()) {
        error = "--terrain-gazetteer is required with gazetteer options";
        return false;
      }
      if (options.generation_profile ==
            terrain::TerrainGenerationProfile::Smoke ||
          options.generation_profile == terrain::TerrainGenerationProfile::Fast)
        options.world.resolution = 1024;
      if (options.terrain_resolution)
        options.world.resolution = *options.terrain_resolution;
      options.config.capture_frames = !options.screenshot_path.empty () ||
                                      options.gazetteer ||
                                      ::getenv ("MOPPE_CINEMATIC_CAPTURE_DIR");
      // An automated run stays behind whatever the developer is looking at.
      options.config.activate = !options.config.capture_frames &&
                                !options.benchmark && !options.stay_inactive;
      // A capture pins its own seed so repeated runs compare like with like.
      if (options.config.capture_frames && options.seed < 0)
        options.seed = 123;
      return true;
    }
  }

  std::string launch_options_help (std::string_view program_name) {
    const std::size_t separator = program_name.find_last_of ("/\\");
    if (separator != std::string_view::npos)
      program_name.remove_prefix (separator + 1);

    std::vector<std::string> syntax;
    syntax.reserve (std::size (flags));
    std::size_t width = 0;
    for (const Flag& flag : flags) {
      std::string line;
      if (!flag.alias.empty ())
        line = std::string (flag.alias) + ", ";
      line += flag.name;
      if (flag.arity > 0)
        line += " " + std::string (flag.expects);
      width = std::max (width, line.size ());
      syntax.push_back (std::move (line));
    }

    std::ostringstream output;
    output << "Usage: " << program_name << " [options]\n\nOptions:\n";
    for (std::size_t index = 0; index < std::size (flags); ++index)
      output << "  " << std::left << std::setw (static_cast<int> (width + 2))
             << syntax[index] << flags[index].description << '\n';
    output << "\nGraphics features:\n  ";
    std::size_t column = 2;
    for (const GraphicsFeature* feature : graphics_features) {
      const std::size_t separator_width = column > 2 ? 2 : 0;
      if (column + separator_width + feature->name.size () > 78) {
        output << "\n  ";
        column = 2;
      } else if (separator_width) {
        output << ", ";
        column += separator_width;
      }
      output << feature->name;
      column += feature->name.size ();
    }
    output << '\n';
    return output.str ();
  }

  bool parse_launch_options (int argc,
                             char** argv,
                             LaunchOptions& options,
                             std::string& error) {
    for (int i = 1; i < argc; ++i) {
      const Flag* flag = find_flag (argv[i]);
      if (flag && flag->name == "--help") {
        options.show_help = true;
        return true;
      }
    }

    // Presets are a baseline, while every other graphics flag is an explicit
    // override. Resolve all presets first so spelling --graphics-quality at
    // the end of a command cannot erase an earlier --render-scale or feature
    // selection. If more than one preset is present, the last still wins.
    for (int i = 1; i < argc; ++i) {
      const Flag* flag = find_flag (argv[i]);
      if (!flag)
        continue;
      if (i + flag->arity >= argc) {
        error = std::string (argv[i]) + " requires " + flag->expects;
        return false;
      }
      if (flag->name == "--graphics-quality" &&
          !flag->apply (options, argv + i + 1, error))
        return false;
      i += flag->arity;
    }

    for (int i = 1; i < argc; ++i) {
      // An unrecognized argument is not an error: hosts append their own.
      const Flag* flag = find_flag (argv[i]);
      if (!flag)
        continue;
      if (i + flag->arity >= argc) {
        error = std::string (argv[i]) + " requires " + flag->expects;
        return false;
      }
      if (flag->name != "--graphics-quality" &&
          !flag->apply (options, argv + i + 1, error))
        return false;
      i += flag->arity;
    }
    return resolve_launch_settings (options, error);
  }

  void
  publish_benchmark_environment (const GraphicsBenchmarkConfig& benchmark) {
    const int expected =
      benchmark.measured_frames *
      (1 << graphics_benchmark_dimension_count (benchmark.partition));
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
    ::setenv ("MOPPE_BENCHMARK_PARTITION",
              graphics_benchmark_partition_name (benchmark.partition),
              1);
    const std::string block_names =
      graphics_benchmark_block_names (benchmark.partition);
    ::setenv ("MOPPE_BENCHMARK_BLOCKS", block_names.c_str (), 1);
    if (benchmark.pass_timing)
      ::setenv ("MOPPE_BENCHMARK_PASSES", "1", 1);
    else
      ::unsetenv ("MOPPE_BENCHMARK_PASSES");
  }

  terrain::WorldRecipe make_launch_recipe (const LaunchOptions& options) {
    const terrain::Seed seed { static_cast<std::uint32_t> (options.seed) };
    return terrain::make_world_recipe (options.world.map_size,
                                       options.world.resolution,
                                       seed,
                                       options.world.water_level,
                                       options.generation_profile,
                                       options.uplift_duration,
                                       options.channel_initiation_area,
                                       options.sediment_concentration,
                                       options.critical_hillslope_gradient,
                                       options.maximum_hillslope_multiplier);
  }
}
