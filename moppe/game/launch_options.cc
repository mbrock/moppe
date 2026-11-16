#include <moppe/game/launch_options.hh>

#include <moppe/environment.hh>

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
        "Present in a fullscreen Space (the default).",
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
      { "--render-scale",
        "",
        1,
        "<0.25..1>",
        "Set 3D scene dimensions relative to the drawable.",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          // An explicit scale is held, whatever the frame rate.
          options.graphics.target_frame_rate = 0.0f;
          return parse_scale (values[0],
                              "--render-scale",
                              options.graphics.render_scale_override,
                              error);
        } },
      { "--world-size",
        "",
        1,
        "<KM>",
        "Make the world this many kilometres across (5; up to 10).",
        [] (LaunchOptions& options,
            const char* const* values,
            std::string& error) {
          float kilometres = 0.0f;
          if (!parse_float_option (
                values[0], "--world-size", 2.5f, 10.0f, kilometres, error))
            return false;
          options.world_size = kilometres * 1000.0f * u::m;
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
      { "--screenshot",
        "",
        1,
        "<PNG>",
        "Capture one gameplay PNG and exit.",
        [] (LaunchOptions& options, const char* const* values, std::string&) {
          capture_to (options, values[0]);
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
      if (options.generation_profile ==
            terrain::TerrainGenerationProfile::Smoke ||
          options.generation_profile == terrain::TerrainGenerationProfile::Fast)
        options.world.resolution = 1024;
      if (options.world_size) {
        // More country over the same samples, so a wider world costs no
        // more memory and is coarser underfoot; --terrain-resolution buys
        // the grain back where there is the memory for it (generating a
        // 4096 world peaks at 5 GB).
        const float across = options.world_size->numerical_value_in (u::m);
        const float height = extent_value (options.world.map_size)[1];
        options.world.map_size =
          spatial_extent_in_metres (Vec3 (across, height, across));
      }
      if (options.terrain_resolution)
        options.world.resolution = *options.terrain_resolution;
      options.config.capture_frames = !options.screenshot_path.empty ();
      // A capture keeps its own window (capture_to), as do those asked
      // for by the environment alone.
      if (options.config.capture_frames)
        options.config.fullscreen = false;
      // An automated run stays behind whatever the developer is looking at.
      options.config.activate =
        !options.config.capture_frames && !options.stay_inactive;
      // A capture pins its own seed so repeated runs compare like with like.
      if (options.config.capture_frames && options.seed < 0)
        options.seed = 123;
      // MOPPE_MULTIPLAYER=1 finds a companion on the local network; any
      // other value is their address.
      if (const char* companion = moppe::environment ("MOPPE_MULTIPLAYER");
          companion && !options.multiplayer) {
        const std::string_view value (companion);
        options.multiplayer =
          value == "1" ? std::string () : std::string (value);
      }
      // Two players must stand in the same world, so neither launch may
      // fall back on whichever seed its own machine last remembered.
      if (options.multiplayer && options.seed < 0)
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

  terrain::WorldRecipe make_launch_recipe (const LaunchOptions& options) {
    const terrain::Seed seed { static_cast<std::uint32_t> (options.seed) };
    return terrain::make_world_recipe (options.world.map_size,
                                       options.world.resolution,
                                       seed,
                                       options.world.water_level,
                                       options.generation_profile);
  }
}
