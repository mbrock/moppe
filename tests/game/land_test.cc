#include <moppe/game/land.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/world_recipe.hh>

#include <tests/test.hh>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

using namespace moppe;

namespace {
  terrain::WorldRecipe small_recipe () {
    return terrain::make_world_recipe (
      spatial_extent_in_metres (Vec3 (1200.0f, 320.0f, 1200.0f)),
      65,
      terrain::Seed { 123 },
      50.0f * u::m,
      terrain::TerrainGenerationProfile::Smoke);
  }

  map::SurfaceGeometry make_surface (const terrain::WorldRecipe& recipe) {
    return map::SurfaceGeometry (terrain::TerrainDomain (
      recipe.resolution (), recipe.resolution (), recipe.extent ()));
  }

  // The land as the game makes it: geology, erosion, then the trails.
  map::SurfaceGeometry generate (const terrain::WorldRecipe& recipe) {
    map::SurfaceGeometry surface = make_surface (recipe);
    const auto uplift =
      map::initialize_terrain (surface, recipe.seed (), recipe.water_datum ());
    map::evolve_terrain (surface, uplift, recipe.evolution ());
    (void)map::form_terrain_trails (surface, recipe.trail_formation ());
    return surface;
  }

  template <typename Column>
  void hash_column (std::uint64_t& hash, const Column& column) {
    for (const auto& value : column) {
      const auto bytes = std::as_bytes (std::span (&value, 1));
      for (const std::byte byte : bytes) {
        hash ^= std::to_integer<std::uint64_t> (byte);
        hash *= 1099511628211ull;
      }
    }
  }

  std::uint64_t land_hash (const game::Land& land) {
    std::uint64_t hash = 1469598103934665603ull;
    hash_column (hash, spatial::get<terrain::surface_elevation> (land));
    hash_column (hash, spatial::get<terrain::sediment_thickness> (land));
    hash_column (hash, spatial::get<map::eroded_surface_material> (land));
    hash_column (hash, spatial::get<map::deposited_surface_material> (land));
    return hash;
  }
}

MOPPE_TEST (land_file_names_carry_version_profile_resolution_and_seed) {
  const std::string name = game::land_file_name (small_recipe ());
  MOPPE_CHECK (name.starts_with (
    "land-v" + std::to_string (game::LAND_VERSION) + "-smoke-65-123-"));
  MOPPE_CHECK (name.ends_with (".arrows"));
}

// A recipe names its land on every platform, so a changed land needs a new
// name. When this fails because the geology, erosion, or trails changed on
// purpose, bump LAND_VERSION in moppe/game/land.hh and pin the new hash.
MOPPE_TEST (land_version_pins_what_a_recipe_makes) {
  const std::uint64_t hash =
    land_hash (game::extract_land (generate (small_recipe ())));
  MOPPE_CHECK (game::LAND_VERSION == 1);
  MOPPE_CHECK (hash == 0x4159698745407d7bull);
}

MOPPE_TEST (saved_land_reads_back_exactly) {
  const terrain::WorldRecipe recipe = small_recipe ();
  const map::SurfaceGeometry generated = generate (recipe);
  const std::filesystem::path path =
    std::filesystem::temp_directory_path () / game::land_file_name (recipe);
  game::save_land (generated, path.string ());

  map::SurfaceGeometry loaded = make_surface (recipe);
  MOPPE_CHECK (game::try_load_land (loaded, path.string ()));
  MOPPE_CHECK (land_hash (game::extract_land (loaded)) ==
               land_hash (game::extract_land (generated)));

  // Land over another domain is no land at all for this one.
  map::SurfaceGeometry other =
    map::SurfaceGeometry (terrain::TerrainDomain (33, 33, recipe.extent ()));
  MOPPE_CHECK (!game::try_load_land (other, path.string ()));
  std::filesystem::remove (path);
}
