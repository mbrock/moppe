#include <moppe/environment.hh>
#include <moppe/game/launch_options.hh>

#include <tests/test.hh>

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
  MOPPE_CHECK (options.config.fullscreen);
  MOPPE_CHECK (!parsed ({ "--windowed" }).config.fullscreen);
  MOPPE_CHECK (!options.config.capture_frames);
  MOPPE_CHECK_NEAR (
    options.graphics.render_scale_override.numerical_value_in (one),
    0.5f,
    0.0f);
  MOPPE_CHECK (options.graphics.motion_blur);
  MOPPE_CHECK (options.screenshot_path.empty ());
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
         "--render-scale",
         "--world-size",
         "--terrain-resolution",
         "--screenshot",
         "--window-size",
         "--inactive",
         "--seed",
         "--multiplayer",
         "--join",
       })
    MOPPE_CHECK (help.find (option) != std::string::npos);
  MOPPE_CHECK (help.find ("-h, --help") != std::string::npos);
}

MOPPE_TEST (launch_multiplayer_pins_the_shared_world) {
  MOPPE_CHECK (!parsed ({}).multiplayer);
  const game::LaunchOptions lan = parsed ({ "--multiplayer" });
  MOPPE_CHECK (lan.multiplayer && lan.multiplayer->empty ());
  MOPPE_CHECK (lan.seed == 123);
  const game::LaunchOptions room = parsed ({ "--join", "room:birch-42" });
  MOPPE_CHECK (room.multiplayer && *room.multiplayer == "room:birch-42");
  MOPPE_CHECK (room.seed == 123);

  moppe::set_environment ("MOPPE_MULTIPLAYER", "1");
  const game::LaunchOptions found = parsed ({});
  MOPPE_CHECK (found.multiplayer && found.multiplayer->empty ());
  MOPPE_CHECK (found.seed == 123);
  const game::LaunchOptions cli_join =
    parsed ({ "--join", "ws://relay.example/room/pine" });
  MOPPE_CHECK (cli_join.multiplayer &&
               *cli_join.multiplayer == "ws://relay.example/room/pine");
  moppe::set_environment ("MOPPE_MULTIPLAYER", "chapel:27961");
  const game::LaunchOptions named = parsed ({});
  MOPPE_CHECK (named.multiplayer && *named.multiplayer == "chapel:27961");
  const game::LaunchOptions cli_lan = parsed ({ "--multiplayer" });
  MOPPE_CHECK (cli_lan.multiplayer && cli_lan.multiplayer->empty ());
  MOPPE_CHECK (parsed ({ "--seed", "7" }).seed == 7);
  moppe::set_environment ("MOPPE_MULTIPLAYER", nullptr);
}

MOPPE_TEST (launch_quality_flags_select_settings) {
  MOPPE_CHECK (!parsed ({ "--graphics-quality", "low" }).graphics.bloom);
  MOPPE_CHECK (parsed ({ "--graphics-quality", "high" }).graphics.bloom);
  const game::LaunchOptions high = parsed ({ "--graphics-quality", "high" });
  MOPPE_CHECK_NEAR (
    high.graphics.render_scale_override.numerical_value_in (one), 0.0f, 0.0f);
  MOPPE_CHECK (high.graphics.motion_blur);
  const game::LaunchOptions scales = parsed ({ "--render-scale", "0.375" });
  MOPPE_CHECK_NEAR (
    scales.graphics.render_scale_override.numerical_value_in (one),
    0.375f,
    0.0f);
  // Play sheds resolution to hold 60 frames a second, unless a scale is
  // asked for outright.
  MOPPE_CHECK_NEAR (
    parsed ({}).graphics.target_frame_rate.numerical_value_in (u::Hz),
    60.0f,
    0.0f);
  MOPPE_CHECK_NEAR (
    scales.graphics.target_frame_rate.numerical_value_in (u::Hz), 0.0f, 0.0f);
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
  MOPPE_CHECK (rejects ({ "--render-scale", "1.1" }));
  MOPPE_CHECK (rejects ({ "--world-size", "40" }));
  MOPPE_CHECK (rejects ({ "--terrain-resolution", "1024.5" }));
  MOPPE_CHECK (rejects ({ "--terrain-resolution", "64" }));
  MOPPE_CHECK (rejects ({ "--screenshot" }));
  MOPPE_CHECK (rejects ({ "--seed" }));
  MOPPE_CHECK (rejects ({ "--join" }));
  MOPPE_CHECK (rejects ({ "--join", "" }));
  // An unrecognized argument is not an error; it is simply not a setting.
  MOPPE_CHECK (!rejects ({ "--not-a-flag" }));
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
}

MOPPE_TEST (launch_resolutions_follow_the_generation_they_serve) {
  MOPPE_CHECK (parsed ({}).world.resolution == 4096);
  MOPPE_CHECK (extent_value (parsed ({}).world.map_size)[0] == 10000);
  MOPPE_CHECK (parsed ({ "--terrain-resolution", "1024" }).world.resolution ==
               1024);
  // A wider world is spread over the same samples unless more are asked
  // for.
  const game::LaunchOptions wide = parsed ({ "--world-size", "10" });
  MOPPE_CHECK (extent_value (wide.world.map_size)[0] == 10000.0f);
  MOPPE_CHECK (extent_value (wide.world.map_size)[1] ==
               extent_value (parsed ({}).world.map_size)[1]);
  MOPPE_CHECK (wide.world.resolution == parsed ({}).world.resolution);
  MOPPE_CHECK (parsed ({ "--world-size", "10", "--terrain-resolution", "4096" })
                 .world.resolution == 4096);
}

MOPPE_TEST (launch_recipe_carries_the_resolved_seed_and_profile) {
  game::LaunchOptions options = parsed ({ "--world-size", "10" });
  options.seed = 4321;
  const terrain::WorldRecipe recipe = game::make_launch_recipe (options);
  MOPPE_CHECK (recipe.seed ().value == 4321u);
  MOPPE_CHECK (recipe.generation_profile () == options.generation_profile);
  MOPPE_CHECK (recipe.resolution () == options.world.resolution);
  MOPPE_CHECK (extent_value (recipe.extent ())[0] == 10000.0f);
}
