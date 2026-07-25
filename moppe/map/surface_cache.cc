#include <moppe/map/surface_cache.hh>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

// A saved surface is the expensive part of world generation: the evolved
// heightfield and the material its erosion moved. Normals and every reading
// are cheap to derive again, so they are not stored.

namespace moppe::map {
  namespace {
    constexpr char surface_magic[4] = { 'M', 'O', 'P', '4' };
    constexpr char material_magic[4] = { 'L', 'G', 'R', '1' };
  }

  bool try_load_cache (SurfaceGeometry& geometry, const std::string& path) {
    std::ifstream file (path, std::ios::binary);
    if (!file)
      return false;
    auto& elevation = spatial::get<terrain::surface_elevation> (geometry);
    char magic[4] {};
    std::int32_t stored_width = 0;
    std::int32_t stored_height = 0;
    file.read (magic, 4);
    file.read (reinterpret_cast<char*> (&stored_width), 4);
    file.read (reinterpret_cast<char*> (&stored_height), 4);
    if (!file || std::memcmp (magic, surface_magic, 4) != 0 ||
        static_cast<std::size_t> (stored_width) !=
          geometry.domain ().width () ||
        static_cast<std::size_t> (stored_height) !=
          geometry.domain ().height ())
      return false;

    std::vector<float> values (geometry.size ());
    file.read (reinterpret_cast<char*> (values.data ()),
               static_cast<std::streamsize> (values.size () * sizeof (float)));
    if (!file)
      return false;
    for (std::size_t cell = 0; cell < values.size (); ++cell)
      elevation[cell] =
        SurfaceElevation (values[cell] * terrain::surface_elevation[u::m]);

    reset_material_history (geometry);
    char ledger_magic[4] {};
    file.read (ledger_magic, 4);
    if (file.gcount () == 4 &&
        std::memcmp (ledger_magic, material_magic, 4) == 0) {
      std::vector<float> eroded (values.size ());
      std::vector<float> deposited (values.size ());
      file.read (
        reinterpret_cast<char*> (eroded.data ()),
        static_cast<std::streamsize> (eroded.size () * sizeof (float)));
      const bool have_eroded = static_cast<bool> (file);
      file.read (
        reinterpret_cast<char*> (deposited.data ()),
        static_cast<std::streamsize> (deposited.size () * sizeof (float)));
      if (!have_eroded || !file)
        reset_material_history (geometry);
      else {
        auto& eroded_column = spatial::get<eroded_surface_material> (geometry);
        auto& deposited_column =
          spatial::get<deposited_surface_material> (geometry);
        for (std::size_t cell = 0; cell < values.size (); ++cell) {
          eroded_column[cell] =
            eroded[cell] * eroded_surface_material[mp_units::one];
          deposited_column[cell] =
            deposited[cell] * deposited_surface_material[mp_units::one];
        }
      }
    }
    return true;
  }

  void save_cache (const SurfaceGeometry& geometry, const std::string& path) {
    std::ofstream file (path, std::ios::binary);
    if (!file)
      throw std::runtime_error ("can't write surface cache: " + path);
    const std::int32_t stored_width =
      static_cast<std::int32_t> (geometry.domain ().width ());
    const std::int32_t stored_height =
      static_cast<std::int32_t> (geometry.domain ().height ());
    file.write (surface_magic, 4);
    file.write (reinterpret_cast<const char*> (&stored_width), 4);
    file.write (reinterpret_cast<const char*> (&stored_height), 4);
    for (SurfaceElevation value :
         spatial::get<terrain::surface_elevation> (geometry)) {
      const float scalar = terrain::surface_elevation_value (value);
      file.write (reinterpret_cast<const char*> (&scalar), sizeof (scalar));
    }
    file.write (material_magic, 4);
    for (ErodedSurfaceMaterial value :
         spatial::get<eroded_surface_material> (geometry)) {
      const float scalar = value.numerical_value_in (mp_units::one);
      file.write (reinterpret_cast<const char*> (&scalar), sizeof (scalar));
    }
    for (DepositedSurfaceMaterial value :
         spatial::get<deposited_surface_material> (geometry)) {
      const float scalar = value.numerical_value_in (mp_units::one);
      file.write (reinterpret_cast<const char*> (&scalar), sizeof (scalar));
    }
  }
}
