#include <moppe/game/land.hh>

#include <moppe/spatial/bundle_storage.hh>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace moppe::game {
  namespace {
    // Everything in a recipe that a launch can change, which the version
    // does not cover: the profile's constants are code, but the extent,
    // datum, and evolution settings can come from the command line. Floats
    // as their bits, since a rounded decimal would blur the key.
    std::string recipe_key (const terrain::WorldRecipe& recipe) {
      const Vec3 extent = extent_value (recipe.extent ());
      const auto bits = [] (float value) {
        return std::bit_cast<std::uint32_t> (value);
      };
      const terrain::StreamPowerEvolution& evolution = recipe.evolution ();
      std::ostringstream key;
      key << std::hex << bits (extent[0]) << '-' << bits (extent[1]) << '-'
          << bits (extent[2]) << '-'
          << bits (recipe.water_datum ().numerical_value_in (u::m)) << '-'
          << bits (evolution.uplift_duration.numerical_value_in (
               mp_units::astronomy::Julian_year))
          << '-'
          << bits (evolution.channel_initiation_area.numerical_value_in (u::m *
                                                                         u::m))
          << '-'
          << bits (evolution.fluvial_transport.runoff_rate.numerical_value_in (
               u::m / mp_units::astronomy::Julian_year))
          << '-'
          << bits (evolution.fluvial_transport.concentration_at_unit_slope
                     .numerical_value_in (mp_units::one))
          << '-'
          << bits (evolution.critical_hillslope_gradient.numerical_value_in (
               mp_units::one))
          << '-'
          << bits (evolution.maximum_hillslope_diffusivity_multiplier
                     .numerical_value_in (mp_units::one));
      return key.str ();
    }

    // FNV-1a, which is the same on every platform, unlike std::hash.
    std::uint32_t fnv1a (const std::string& text) {
      std::uint32_t hash = 2166136261u;
      for (const char c : text) {
        hash ^= static_cast<unsigned char> (c);
        hash *= 16777619u;
      }
      return hash;
    }

    template <auto Spec, typename From, typename To>
    void copy_column (const From& from, To& to) {
      std::ranges::copy (spatial::get<Spec> (from),
                         spatial::get<Spec> (to).begin ());
    }

    template <typename From, typename To>
    void copy_land (const From& from, To& to) {
      copy_column<terrain::surface_elevation> (from, to);
      copy_column<terrain::sediment_thickness> (from, to);
      copy_column<map::eroded_surface_material> (from, to);
      copy_column<map::deposited_surface_material> (from, to);
    }
  }

  std::string land_file_name (const terrain::WorldRecipe& recipe) {
    std::ostringstream name;
    name << "land-v" << LAND_VERSION << '-'
         << terrain::profile_id (recipe.generation_profile ()) << '-'
         << recipe.resolution () << '-' << recipe.seed ().value << '-'
         << std::hex << std::setw (8) << std::setfill ('0')
         << fnv1a (recipe_key (recipe)) << ".arrows";
    return name.str ();
  }

  Land extract_land (const map::SurfaceGeometry& surface) {
    Land land (surface.domain ());
    copy_land (surface, land);
    return land;
  }

  bool try_load_land (map::SurfaceGeometry& surface, const std::string& path) {
    std::ifstream file (path, std::ios::binary);
    if (!file)
      return false;
    Land land (surface.domain ());
    if (!spatial::load_bundle (file, land))
      return false;
    copy_land (land, surface);
    return true;
  }

  void save_land (const map::SurfaceGeometry& surface,
                  const std::string& path) {
    const std::string partial = path + ".partial";
    {
      std::ofstream file (partial, std::ios::binary);
      if (!file)
        throw std::runtime_error ("can't write land: " + partial);
      spatial::write_bundle (file, extract_land (surface));
      if (!file)
        throw std::runtime_error ("can't write land: " + partial);
    }
    std::filesystem::rename (partial, path);
  }
}
