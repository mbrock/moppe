#include <moppe/game/forest.hh>
#include <moppe/game/generated_world.hh>
#include <moppe/game/world_cache.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/world_recipe.hh>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
  int parse_positive_int (std::string_view text, const char* name) {
    std::size_t consumed = 0;
    const int value = std::stoi (std::string (text), &consumed);
    if (consumed != text.size () || value <= 0)
      throw std::invalid_argument (std::string (name) +
                                   " must be a positive integer");
    return value;
  }

  std::uint32_t parse_seed (std::string_view text) {
    std::size_t consumed = 0;
    const unsigned long value = std::stoul (std::string (text), &consumed);
    if (consumed != text.size () ||
        value > std::numeric_limits<std::uint32_t>::max ())
      throw std::invalid_argument ("seed must be a 32-bit unsigned integer");
    return static_cast<std::uint32_t> (value);
  }

  moppe::terrain::TerrainGenerationProfile
  parse_profile (std::string_view text) {
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
    if (argc != 5)
      throw std::invalid_argument (
        "usage: terrain-cache-bake OUTPUT_DIRECTORY RESOLUTION PROFILE SEED");
    const std::filesystem::path output = argv[1];
    const int resolution = parse_positive_int (argv[2], "resolution");
    const TerrainGenerationProfile profile = parse_profile (argv[3]);
    const std::uint32_t seed = parse_seed (argv[4]);
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
    std::cout << "Evolving " << resolution << "x" << resolution << " "
              << profile_id (profile) << " terrain for seed " << seed << "..."
              << std::endl;
    const auto uplift =
      map::initialize_terrain (surface, recipe.seed (), recipe.water_datum ());
    map::evolve_terrain (surface, uplift, recipe.evolution ());
    TrailNetwork trails =
      map::form_terrain_trails (surface, recipe.trail_formation ());
    map::rebuild_geometry (surface);
    game::HydrologyAnalysis analysis =
      game::analyze_hydrology (surface, recipe);
    auto [water, readings] = game::analyze_surface (
      surface, recipe, analysis.hydrology, analysis.channels, trails.use);
    const std::uint32_t forest_seed = seed ^ 0xa34c91e5U;
    game::ForestPlan forest =
      game::plan_global_forest (surface, readings, forest_seed);
    auto world =
      std::make_unique<game::GeneratedWorld> (game::WorldParams {},
                                              recipe,
                                              std::move (surface),
                                              std::move (analysis.hydrology),
                                              std::move (water),
                                              std::move (trails),
                                              std::move (readings),
                                              std::move (forest));
    game::save_world_cache (*world, output.string ());
    world.reset ();
    if (!game::try_load_world_cache (
          game::WorldParams {}, recipe, output.string ()))
      throw std::runtime_error ("written world cache did not validate");

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (
      std::chrono::steady_clock::now () - start);
    std::uintmax_t bytes = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator (output))
      if (entry.is_regular_file ())
        bytes += entry.file_size ();
    std::cout << "Wrote " << bytes << " bytes to " << output << " in "
              << elapsed.count () << " ms" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "terrain-cache-bake: " << error.what () << '\n';
    return -1;
  }
}
