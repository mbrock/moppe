#ifndef MOPPE_GAME_LAUNCH_OPTIONS_HH
#define MOPPE_GAME_LAUNCH_OPTIONS_HH

#include <moppe/game/graphics_settings.hh>
#include <moppe/game/water_capture.hh>
#include <moppe/game/world.hh>
#include <moppe/game/world_cache.hh>
#include <moppe/platform/platform.hh>
#include <moppe/terrain/world_recipe.hh>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace moppe::game {
  // How long the GPU benchmark rides before it starts trusting its own
  // numbers.  The prelude establishes a representative ride; each measured
  // configuration then replays it from the same checkpoint.
  struct GraphicsBenchmarkConfig {
    std::string output_path;
    int prelude_frames = 480;
    int settle_frames = 30;
    int measured_frames = 120;
    bool pass_timing = false;
  };

  struct GazetteerCaptureConfig {
    std::string output_directory;
    // Frozen views still render a few times so auto-exposure and bounded near
    // forest residency reach the same state before every captured frame.
    int settle_frames = 18;
  };

  // Everything one launch of the game is configured with.  This is the whole
  // command line resolved into values: no argv survives past parsing, and no
  // platform service is consulted, so the derived defaults below stay
  // testable.
  struct LaunchOptions {
    WorldParams world;
    GraphicsSettings graphics = default_graphics_settings ();
    platform::Config config { .title = "Moppe" };
    terrain::TerrainGenerationProfile generation_profile =
      terrain::TerrainGenerationProfile::Play;
    WorldCacheConfig world_cache;
    bool tree_demo = false;
    std::size_t tree_count = 9;
    std::string screenshot_path;
    std::optional<WaterShot> water_shot;
    std::optional<GraphicsBenchmarkConfig> benchmark;
    std::optional<GazetteerCaptureConfig> gazetteer;
    // Applied after legacy environment settings so an explicit command-line
    // budget wins. Zero disables the desktop megapixel safety cap.
    std::optional<float> scene_megapixel_budget;
    // Keeps a hand-started run behind the active application, the way
    // captures and benchmarks already stay out of the way.
    bool stay_inactive = false;
    // Negative until the launch either names a seed or recalls a remembered
    // one; a capture always pins its own so comparisons stay reproducible.
    int seed = -1;
    bool show_help = false;
  };

  // Parses argv over already-initialized options, so a platform may install
  // its own graphics defaults first.  Unrecognized arguments are ignored.
  // Returns false with a human-readable error for a malformed command line.
  bool parse_launch_options (int argc,
                             char** argv,
                             LaunchOptions& options,
                             std::string& error);

  // Generated from the parser's flag registry so accepted options and their
  // help text remain one definition.
  std::string launch_options_help (std::string_view program_name);

  // The benchmark's output path and feature set reach the renderer backend
  // through the environment, which is the one channel both the native and
  // browser hosts already read.
  void publish_benchmark_environment (const GraphicsBenchmarkConfig& benchmark);

  // Requires a resolved (non-negative) seed.
  terrain::WorldRecipe make_launch_recipe (const LaunchOptions& options);
}

#endif
