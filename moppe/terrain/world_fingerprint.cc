// Prints a hash of the world after every generation stage, and of the
// elevations after every geological step, so two builds -- two machines, two
// compilers, a serial and a parallel version of a stage -- can be compared
// line by line. The first differing line names where they part.
//
//   world-fingerprint [RESOLUTION [PROFILE [SEED [KM]]]]
//   (default 257 play 123 5)
//
// MOPPE_FINGERPRINT_COST=1 adds, on the error stream so the hashes still
// diff, what each line cost: the seconds since the line before, the memory
// resident now, and the most that has been resident so far.

#include <moppe/game/forest.hh>
#include <moppe/game/generated_world.hh>
#include <moppe/game/land.hh>
#include <moppe/game/world_cache.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/world_recipe.hh>

#include <moppe/environment.hh>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/resource.h>
#ifdef __APPLE__
#include <mach/mach.h>
#else
#include <unistd.h>
#endif

namespace {
  // Megabytes resident now.
  double resident_mb () {
#ifdef __APPLE__
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info (mach_task_self (),
                   MACH_TASK_BASIC_INFO,
                   reinterpret_cast<task_info_t> (&info),
                   &count) != KERN_SUCCESS)
      return 0.0;
    return static_cast<double> (info.resident_size) / 1048576.0;
#else
    long pages = 0, resident = 0;
    if (std::FILE* statm = std::fopen ("/proc/self/statm", "r")) {
      if (std::fscanf (statm, "%ld %ld", &pages, &resident) != 2)
        resident = 0;
      std::fclose (statm);
    }
    return static_cast<double> (resident) *
           static_cast<double> (sysconf (_SC_PAGESIZE)) / 1048576.0;
#endif
  }

  // Megabytes at the most there has been.
  double peak_mb () {
    rusage usage {};
    getrusage (RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return static_cast<double> (usage.ru_maxrss) / 1048576.0;
#else
    return static_cast<double> (usage.ru_maxrss) / 1024.0;
#endif
  }

  void cost (const std::string& stage) {
    static const bool wanted =
      moppe::environment ("MOPPE_FINGERPRINT_COST") != nullptr;
    if (!wanted)
      return;
    using Clock = std::chrono::steady_clock;
    static Clock::time_point before = Clock::now ();
    const Clock::time_point now = Clock::now ();
    std::fprintf (stderr,
                  "%-36s %7.2f s %7.0f MB now %7.0f MB peak\n",
                  stage.c_str (),
                  std::chrono::duration<double> (now - before).count (),
                  resident_mb (),
                  peak_mb ());
    before = Clock::now ();
  }

  // FNV-1a over bytes.
  struct Hash {
    std::uint64_t value = 1469598103934665603ull;

    void add (std::span<const std::byte> bytes) {
      for (const std::byte byte : bytes) {
        value ^= std::to_integer<std::uint64_t> (byte);
        value *= 1099511628211ull;
      }
    }
  };

  template <typename Range>
  std::uint64_t hash_of (const Range& range) {
    Hash hash;
    hash.add (std::as_bytes (std::span (std::data (range), std::size (range))));
    return hash.value;
  }

  void line (const std::string& stage, std::uint64_t hash) {
    cost (stage);
    std::cout << std::setw (36) << std::left << stage << ' ' << std::hex
              << std::setw (16) << std::setfill ('0') << std::right << hash
              << std::setfill (' ') << std::dec << '\n';
  }

  void surface_lines (const std::string& stage,
                      const moppe::map::SurfaceGeometry& surface) {
    using namespace moppe;
    line (stage + " elevation",
          hash_of (spatial::get<terrain::surface_elevation> (surface)));
    line (stage + " sediment",
          hash_of (spatial::get<terrain::sediment_thickness> (surface)));
    line (stage + " eroded",
          hash_of (spatial::get<map::eroded_surface_material> (surface)));
    line (stage + " deposited",
          hash_of (spatial::get<map::deposited_surface_material> (surface)));
    // Three components each, not the vector's padded storage.
    std::vector<float> normals;
    for (const auto& normal : spatial::get<terrain::terrain_normal> (surface))
      for (std::size_t axis = 0; axis < 3; ++axis)
        normals.push_back (normal.numerical_value_in (mp_units::one)[axis]);
    line (stage + " normal", hash_of (normals));
  }

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
    const int resolution = argc > 1 ? std::stoi (argv[1]) : 257;
    const TerrainGenerationProfile profile =
      parse_profile (argc > 2 ? argv[2] : "play");
    const std::uint32_t seed =
      argc > 3 ? static_cast<std::uint32_t> (std::stoul (argv[3])) : 123;
    // The game's default extent, as tools/bake-world bakes it, or as wide
    // as --world-size would make it.
    const float across_m = argc > 4 ? std::stof (argv[4]) * 1000.0f : 5000.0f;
    const WorldRecipe recipe = make_world_recipe (
      spatial_extent_in_metres (Vec3 (across_m, 320.0f, across_m)),
      resolution,
      Seed { seed },
      50.0f * u::m,
      profile);
    map::SurfaceGeometry surface (
      TerrainDomain (static_cast<std::size_t> (resolution),
                     static_cast<std::size_t> (resolution),
                     recipe.extent ()));

    const auto uplift =
      map::initialize_terrain (surface, recipe.seed (), recipe.water_datum ());
    surface_lines ("initialized", surface);
    line ("initialized uplift", hash_of (uplift));
    map::evolve_terrain (
      surface,
      uplift,
      recipe.evolution (),
      [] (IterationCount step,
          IterationCount,
          std::span<const SurfaceElevation> elevations) {
        line ("step " +
                std::to_string (step.numerical_value_in (mp_units::one)) +
                " elevation",
              hash_of (elevations));
      });
    surface_lines ("evolved", surface);
    // As the game does: give the rivers their beds, form the trails, then
    // find their network in the formed surface, so a world from saved land
    // is the same.
    line (
      "channels cut and fill",
      hash_of (
        map::form_river_channels (surface, recipe.water_datum ()).change_m));
    surface_lines ("channels", surface);
    line ("trails earthwork",
          hash_of (map::form_terrain_trails (surface, recipe.trail_formation ())
                     .earthwork_delta_m));
    surface_lines ("trails", surface);
    game::quantize_land (surface);
    surface_lines ("land", surface);
    TrailNetwork trails =
      analyze_trail_network (surface, recipe.trail_formation ());
    map::rebuild_geometry (surface);
    surface_lines ("geometry", surface);
    game::Hydrology hydrology = game::analyze_hydrology (surface, recipe);
    cost ("hydrology");
    auto [water, readings] =
      game::analyze_surface (surface, recipe, hydrology, trails.use);
    cost ("water and readings");
    game::ForestPlan forest =
      game::plan_global_forest (surface, readings, water, seed ^ 0xa34c91e5U);
    cost ("forest plan");
    auto world = std::make_unique<game::GeneratedWorld> (game::WorldParams {},
                                                         recipe,
                                                         std::move (surface),
                                                         std::move (hydrology),
                                                         std::move (water),
                                                         std::move (trails),
                                                         std::move (readings),
                                                         std::move (forest));

    // The finished world, by the files of its cache.
    const std::filesystem::path directory =
      std::filesystem::temp_directory_path () /
      ("world-fingerprint-" + std::to_string (seed) + "-" +
       std::to_string (resolution) + "-" +
       std::to_string (static_cast<int> (across_m)));
    std::filesystem::remove_all (directory);
    game::save_world_cache (*world, directory.string ());
    cost ("world cache saved");
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator (directory))
      files.push_back (entry.path ());
    std::ranges::sort (files);
    for (const std::filesystem::path& file : files) {
      std::ifstream input (file, std::ios::binary);
      const std::vector<char> bytes ((std::istreambuf_iterator<char> (input)),
                                     std::istreambuf_iterator<char> ());
      line ("cache " + file.filename ().string (), hash_of (bytes));
    }
    std::filesystem::remove_all (directory);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "world-fingerprint: " << error.what () << '\n';
    return -1;
  }
}
