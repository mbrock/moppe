#include <moppe/map/surface.hh>

#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace moppe::map {
  int Surface::width () const noexcept {
    return static_cast<int> (m_geometry.domain ().width ());
  }

  int Surface::height () const noexcept {
    return static_cast<int> (m_geometry.domain ().height ());
  }

  Vec3 Surface::sample_spacing () const noexcept {
    return Vec3 (meters_value (m_geometry.domain ().spacing_x ()),
                 1.0f,
                 meters_value (m_geometry.domain ().spacing_z ()));
  }

  Vec3 Surface::world_period () const noexcept {
    return Vec3 (meters_value (m_geometry.domain ().period_x ()),
                 0.0f,
                 meters_value (m_geometry.domain ().period_z ()));
  }

  std::size_t Surface::offset (int column, int row) const {
    return m_geometry.domain ().offset (
      { static_cast<std::size_t> (column), static_cast<std::size_t> (row) });
  }

  SurfaceElevation Surface::elevation_at (int column, int row) const {
    return spatial::get<terrain::surface_elevation> (
      geometry ())[offset (column, row)];
  }

  void Surface::set_elevation (int column, int row, SurfaceElevation value) {
    spatial::get<terrain::surface_elevation> (
      geometry ())[offset (column, row)] = value;
  }

  void Surface::fill_elevation (SurfaceElevation value) {
    std::ranges::fill (spatial::get<terrain::surface_elevation> (geometry ()),
                       value);
  }

  Vec3 Surface::normal_at (int column, int row) const {
    return spatial::get<terrain::terrain_normal> (
             geometry ())[offset (column, row)]
      .numerical_value_in (mp_units::one);
  }

  Vec3 Surface::vertex (int column, int row) const {
    const Vec3 spacing = sample_spacing ();
    return Vec3 (spacing[0] * column,
                 terrain::surface_elevation_value (elevation_at (column, row)),
                 spacing[2] * row);
  }

  bool Surface::in_bounds (float x, float z) const {
    return std::isfinite (x) && std::isfinite (z);
  }

  // Both reconstructions are the bundle's own interpolation over the terrain
  // domain, which wraps; these only unwrap the quantity for callers that
  // work in plain metres.
  float Surface::interpolated_height (float x, float z) const {
    return terrain::surface_elevation_value (
      elevation_at (position (Vec3 (x, 0.0f, z))));
  }

  Vec3 Surface::interpolated_normal (float x, float z) const {
    return normal_at (position (Vec3 (x, 0.0f, z)))
      .numerical_value_in (mp_units::one);
  }

  SurfaceElevation Surface::min_elevation () const {
    return *std::ranges::min_element (
      spatial::get<terrain::surface_elevation> (geometry ()),
      {},
      terrain::surface_elevation_value);
  }

  SurfaceElevation Surface::max_elevation () const {
    return *std::ranges::max_element (
      spatial::get<terrain::surface_elevation> (geometry ()),
      {},
      terrain::surface_elevation_value);
  }

  void Surface::recompute_normals () {
    MOPPE_PROFILE_ZONE ("Surface::recompute_normals");
    auto& normal_column = spatial::get<terrain::terrain_normal> (m_geometry);
    std::ranges::fill (normal_column,
                       Vec3 (0, 0, 0) * terrain::terrain_normal[mp_units::one]);
    const Vec3 spacing = sample_spacing ();
    const auto point = [&] (int column, int row) {
      return Vec3 (spacing[0] * column,
                   terrain::surface_elevation_value (
                     elevation_at (terrain::wrap_index (column, width ()),
                                   terrain::wrap_index (row, height ()))),
                   spacing[2] * row);
    };
    const auto face = [&] (int x1, int y1, int x2, int y2, int x3, int y3) {
      return normalized (cross (point (x2, y2) - point (x1, y1),
                                point (x3, y3) - point (x1, y1)));
    };
    const auto add = [&] (int column, int row, const Vec3& value) {
      const std::size_t cell = offset (terrain::wrap_index (column, width ()),
                                       terrain::wrap_index (row, height ()));
      normal_column[cell] =
        (normal_column[cell].numerical_value_in (mp_units::one) + value) *
        terrain::terrain_normal[mp_units::one];
    };
    for (int row = 0; row < height (); ++row)
      for (int column = 0; column < width (); ++column) {
        const Vec3 left =
          face (column, row, column, row + 1, column + 1, row + 1);
        const Vec3 right =
          face (column, row, column + 1, row + 1, column + 1, row);
        add (column, row, left);
        add (column, row + 1, left);
        add (column + 1, row + 1, left);
        add (column, row, right);
        add (column + 1, row, right);
        add (column + 1, row + 1, right);
      }
    for (SurfaceNormal& value : normal_column) {
      Vec3 normal = value.numerical_value_in (mp_units::one);
      normalize (normal);
      value = normal * terrain::terrain_normal[mp_units::one];
    }
  }

  void Surface::reset_material_history () {
    std::ranges::fill (spatial::get<eroded_surface_material> (geometry ()),
                       0.0f * eroded_surface_material[mp_units::one]);
    std::ranges::fill (spatial::get<deposited_surface_material> (geometry ()),
                       0.0f * deposited_surface_material[mp_units::one]);
  }

  void Surface::record_material_change (int column, int row, float delta) {
    const std::size_t cell = offset (column, row);
    auto& eroded = spatial::get<eroded_surface_material> (geometry ());
    auto& deposited = spatial::get<deposited_surface_material> (geometry ());
    if (delta < 0.0f)
      eroded[cell] = (eroded[cell].numerical_value_in (mp_units::one) - delta) *
                     eroded_surface_material[mp_units::one];
    else
      deposited[cell] =
        (deposited[cell].numerical_value_in (mp_units::one) + delta) *
        deposited_surface_material[mp_units::one];
  }

  namespace {
    constexpr char surface_magic[4] = { 'M', 'O', 'P', '4' };
    constexpr char material_magic[4] = { 'L', 'G', 'R', '1' };
  }

  bool Surface::try_load_cache (const std::string& path) {
    std::ifstream file (path, std::ios::binary);
    if (!file)
      return false;
    auto& elevation = spatial::get<terrain::surface_elevation> (geometry ());
    char magic[4] {};
    std::int32_t stored_width = 0;
    std::int32_t stored_height = 0;
    file.read (magic, 4);
    file.read (reinterpret_cast<char*> (&stored_width), 4);
    file.read (reinterpret_cast<char*> (&stored_height), 4);
    if (!file || std::memcmp (magic, surface_magic, 4) != 0 ||
        stored_width != width () || stored_height != height ())
      return false;

    std::vector<float> values (m_geometry.domain ().size ());
    file.read (reinterpret_cast<char*> (values.data ()),
               static_cast<std::streamsize> (values.size () * sizeof (float)));
    if (!file)
      return false;
    for (std::size_t cell = 0; cell < values.size (); ++cell)
      elevation[cell] =
        SurfaceElevation (values[cell] * terrain::surface_elevation[u::m]);

    reset_material_history ();
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
        reset_material_history ();
      else {
        auto& eroded_column =
          spatial::get<eroded_surface_material> (geometry ());
        auto& deposited_column =
          spatial::get<deposited_surface_material> (geometry ());
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

  void Surface::save_cache (const std::string& path) const {
    std::ofstream file (path, std::ios::binary);
    if (!file)
      throw std::runtime_error ("can't write surface cache: " + path);
    const std::int32_t stored_width = width ();
    const std::int32_t stored_height = height ();
    file.write (surface_magic, 4);
    file.write (reinterpret_cast<const char*> (&stored_width), 4);
    file.write (reinterpret_cast<const char*> (&stored_height), 4);
    for (SurfaceElevation value :
         spatial::get<terrain::surface_elevation> (geometry ())) {
      const float scalar = terrain::surface_elevation_value (value);
      file.write (reinterpret_cast<const char*> (&scalar), sizeof (scalar));
    }
    file.write (material_magic, 4);
    for (ErodedSurfaceMaterial value :
         spatial::get<eroded_surface_material> (geometry ())) {
      const float scalar = value.numerical_value_in (mp_units::one);
      file.write (reinterpret_cast<const char*> (&scalar), sizeof (scalar));
    }
    for (DepositedSurfaceMaterial value :
         spatial::get<deposited_surface_material> (geometry ())) {
      const float scalar = value.numerical_value_in (mp_units::one);
      file.write (reinterpret_cast<const char*> (&scalar), sizeof (scalar));
    }
  }
}
