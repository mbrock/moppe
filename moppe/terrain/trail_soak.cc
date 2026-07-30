#include <moppe/map/surface.hh>
#include <moppe/terrain/world_recipe.hh>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
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

// Runs the loading worker's exact surface pipeline over a range of seeds and
// reports whether each world got its trail circuit. World generation must
// never fail; any seed printed as "failed" here is a bug to chase.
int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    if (argc != 5)
      throw std::invalid_argument (
        "usage: terrain-trail-soak RESOLUTION PROFILE FIRST_SEED SEEDS");
    const int resolution = parse_positive_int (argv[1], "resolution");
    const TerrainGenerationProfile profile = parse_profile (argv[2]);
    const int first_seed = parse_positive_int (argv[3], "first seed");
    const int seeds = parse_positive_int (argv[4], "seeds");

    int failures = 0;
    for (int index = 0; index < seeds; ++index) {
      const std::uint32_t seed =
        static_cast<std::uint32_t> (first_seed + index);
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
      const std::vector<meters_per_julian_year_t> uplift =
        map::initialize_terrain (
          surface, recipe.seed (), recipe.water_datum ());
      map::evolve_terrain (surface, uplift, recipe.evolution ());
      std::cout << "seed " << seed << ": " << std::flush;
      try {
        const TrailNetwork network =
          map::form_terrain_trails (surface, recipe.trail_formation ());
        const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::steady_clock::now () - start);
        std::cout << "ok, circuit "
                  << static_cast<long long> (
                       network.alignment.length.numerical_value_in (u::m))
                  << " m, " << elapsed.count () << " ms\n";
      } catch (const std::exception& error) {
        ++failures;
        std::cout << "failed: " << error.what () << "\n";
      }
    }
    std::cout << failures << " of " << seeds << " seeds failed\n";
    return failures == 0 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "terrain-trail-soak: " << error.what () << "\n";
    return -1;
  }
}
