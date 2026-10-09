// Generates a recipe's land (moppe/game/land.hh) as the game would and writes
// it into a directory under its file name, which it prints:
//
//   land-bake OUTPUT_DIRECTORY [RESOLUTION [PROFILE [SEED]]]
//
// tools/bake-land and tools/publish-land drive it.

#include <moppe/game/land.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/world_recipe.hh>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
  moppe::terrain::TerrainGenerationProfile
  parse_profile (const std::string& text) {
    using Profile = moppe::terrain::TerrainGenerationProfile;
    if (text == "smoke")
      return Profile::Smoke;
    if (text == "fast")
      return Profile::Fast;
    if (text == "play")
      return Profile::Play;
    if (text == "research")
      return Profile::Research;
    throw std::invalid_argument (
      "profile must be smoke, fast, play, or research");
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    if (argc < 2 || argc > 5)
      throw std::invalid_argument (
        "usage: land-bake OUTPUT_DIRECTORY [RESOLUTION [PROFILE [SEED]]]");
    const std::filesystem::path output = argv[1];
    const int resolution = argc > 2 ? std::stoi (argv[2]) : 2048;
    const TerrainGenerationProfile profile =
      parse_profile (argc > 3 ? argv[3] : "play");
    const std::uint32_t seed =
      argc > 4 ? static_cast<std::uint32_t> (std::stoul (argv[4])) : 123;
    // The game's default extent and datum.
    const WorldRecipe recipe = make_world_recipe (
      spatial_extent_in_metres (Vec3 (5000.0f, 320.0f, 5000.0f)),
      resolution,
      Seed { seed },
      50.0f * u::m,
      profile);
    map::SurfaceGeometry surface (
      TerrainDomain (static_cast<std::size_t> (resolution),
                     static_cast<std::size_t> (resolution),
                     recipe.extent ()));

    const auto start = std::chrono::steady_clock::now ();
    const auto uplift =
      map::initialize_terrain (surface, recipe.seed (), recipe.water_datum ());
    map::evolve_terrain (surface, uplift, recipe.evolution ());
    (void)map::form_terrain_trails (surface, recipe.trail_formation ());

    std::filesystem::create_directories (output);
    const std::filesystem::path file = output / game::land_file_name (recipe);
    game::save_land (surface, file.string ());
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds> (
      std::chrono::steady_clock::now () - start);
    std::cerr << "land-bake: " << file.filename ().string () << " in "
              << elapsed.count () << " s" << std::endl;
    std::cout << file.string () << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "land-bake: " << error.what () << '\n';
    return -1;
  }
}
