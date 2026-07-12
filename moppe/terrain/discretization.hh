#ifndef MOPPE_TERRAIN_DISCRETIZATION_HH
#define MOPPE_TERRAIN_DISCRETIZATION_HH

#include <moppe/quantities.hh>
#include <moppe/terrain/field.hh>
#include <moppe/terrain/topology.hh>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace moppe::terrain {
  inline constexpr struct row_offset
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } row_offset;
  inline constexpr struct column_offset
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } column_offset;
  inline constexpr struct sample_offset
      : mp_units::quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } sample_offset;

  inline constexpr struct row_unit
      : mp_units::
          named_unit<"row", mp_units::one, mp_units::kind_of<row_offset>> {
  } row_unit;
  inline constexpr struct column_unit
      : mp_units::named_unit<"column",
                             mp_units::one,
                             mp_units::kind_of<column_offset>> {
  } column_unit;
  inline constexpr struct sample_unit
      : mp_units::named_unit<"sample",
                             mp_units::one,
                             mp_units::kind_of<sample_offset>> {
  } sample_unit;

  inline constexpr struct row_origin
      : mp_units::absolute_point_origin<row_offset> {
  } row_origin;
  inline constexpr struct column_origin
      : mp_units::absolute_point_origin<column_offset> {
  } column_origin;
  inline constexpr struct sample_origin
      : mp_units::absolute_point_origin<sample_offset> {
  } sample_origin;

  using RowOffset = mp_units::quantity<row_offset[row_unit], std::ptrdiff_t>;
  using ColumnOffset =
    mp_units::quantity<column_offset[column_unit], std::ptrdiff_t>;
  using SampleOffset =
    mp_units::quantity<sample_offset[sample_unit], std::ptrdiff_t>;
  using RowIndex =
    mp_units::quantity_point<row_unit, row_origin, std::ptrdiff_t>;
  using ColumnIndex =
    mp_units::quantity_point<column_unit, column_origin, std::ptrdiff_t>;
  using SampleIndex =
    mp_units::quantity_point<sample_unit, sample_origin, std::ptrdiff_t>;

  constexpr RowIndex row_index (std::ptrdiff_t value) {
    return mp_units::quantity_point { row_offset (value * row_unit),
                                      row_origin };
  }

  constexpr ColumnIndex column_index (std::ptrdiff_t value) {
    return mp_units::quantity_point { column_offset (value * column_unit),
                                      column_origin };
  }

  constexpr SampleIndex sample_index (std::ptrdiff_t value) {
    return mp_units::quantity_point { sample_offset (value * sample_unit),
                                      sample_origin };
  }

  constexpr std::ptrdiff_t row_number (RowIndex index) {
    return (index - row_origin).numerical_value_in (row_unit);
  }

  constexpr std::ptrdiff_t column_number (ColumnIndex index) {
    return (index - column_origin).numerical_value_in (column_unit);
  }

  constexpr std::ptrdiff_t sample_number (SampleIndex index) {
    return (index - sample_origin).numerical_value_in (sample_unit);
  }

  struct GridPointIndex {
    ColumnIndex column;
    RowIndex row;

    friend bool operator== (const GridPointIndex&,
                            const GridPointIndex&) = default;
  };

  constexpr GridPointIndex grid_point (ColumnIndex column, RowIndex row) {
    return { .column = column, .row = row };
  }

  constexpr GridPointIndex grid_point (std::ptrdiff_t column,
                                       std::ptrdiff_t row) {
    return grid_point (column_index (column), row_index (row));
  }

  struct RecipeDomain2D {
    std::size_t width;
    std::size_t height;
    float min_x = 0.0f;
    float max_x = 1.0f;
    float min_y = 0.0f;
    float max_y = 1.0f;

    friend bool operator== (const RecipeDomain2D&,
                            const RecipeDomain2D&) = default;
  };

  struct TerrainGrid {
    std::size_t width;
    std::size_t height;
    meters_t spacing_x = 1.0f * mp_units::si::metre;
    meters_t spacing_y = 1.0f * mp_units::si::metre;
    meters_t height_scale = 1.0f * mp_units::si::metre;
    Topology topology = Topology::Bounded;

    friend bool operator== (const TerrainGrid&, const TerrainGrid&) = default;

    float spacing_x_m () const {
      return meters_value (spacing_x);
    }
    float spacing_y_m () const {
      return meters_value (spacing_y);
    }
    float height_scale_m () const {
      return meters_value (height_scale);
    }
    square_meters_t cell_area () const {
      return spacing_x * spacing_y;
    }

    std::size_t unique_width () const noexcept {
      return topology == Topology::Torus ? width - 1 : width;
    }

    std::size_t unique_height () const noexcept {
      return topology == Topology::Torus ? height - 1 : height;
    }

    std::size_t unique_size () const noexcept {
      return unique_width () * unique_height ();
    }

    std::pair<std::size_t, std::size_t>
    coordinates (GridPointIndex index) const {
      const std::ptrdiff_t x = column_number (index.column);
      const std::ptrdiff_t y = row_number (index.row);
      if (x < 0 || y < 0 || static_cast<std::size_t> (x) >= width ||
          static_cast<std::size_t> (y) >= height)
        throw std::out_of_range ("terrain grid point outside grid");
      return { static_cast<std::size_t> (x), static_cast<std::size_t> (y) };
    }

    SampleIndex flatten (GridPointIndex index) const {
      const auto [x, y] = coordinates (index);
      return sample_index (static_cast<std::ptrdiff_t> (y * width + x));
    }

    GridPointIndex unflatten (SampleIndex index) const {
      const std::ptrdiff_t flat = sample_number (index);
      if (flat < 0 || static_cast<std::size_t> (flat) >= width * height)
        throw std::out_of_range ("sample index outside terrain grid");
      const std::size_t value = static_cast<std::size_t> (flat);
      return { .column =
                 column_index (static_cast<std::ptrdiff_t> (value % width)),
               .row = row_index (static_cast<std::ptrdiff_t> (value / width)) };
    }
  };

  struct RecipePosition2D {
    mp_units::quantity<recipe_coordinate[mp_units::one], float> u;
    mp_units::quantity<recipe_coordinate[mp_units::one], float> v;
  };

  struct HorizontalPosition2D {
    meters_t x;
    meters_t z;
  };

  // Connects the mathematical recipe domain to one concrete physical
  // sampling lattice.  The present terrain stores values at grid points;
  // cell-centre and cell-average materializations can become additional
  // conventions without changing either coordinate space.
  class TerrainDiscretization {
  public:
    TerrainDiscretization (RecipeDomain2D recipe_domain, TerrainGrid grid)
        : m_recipe_domain (recipe_domain), m_grid (grid) {
      if (recipe_domain.width != grid.width ||
          recipe_domain.height != grid.height)
        throw std::invalid_argument (
          "recipe domain and terrain grid extents differ");
      if (grid.width < 2 || grid.height < 2 ||
          recipe_domain.max_x <= recipe_domain.min_x ||
          recipe_domain.max_y <= recipe_domain.min_y ||
          grid.spacing_x <= 0.0f * mp_units::si::metre ||
          grid.spacing_y <= 0.0f * mp_units::si::metre ||
          grid.height_scale <= 0.0f * mp_units::si::metre)
        throw std::invalid_argument ("invalid terrain discretization");
      if (grid.topology == Topology::Torus &&
          (grid.width < 3 || grid.height < 3))
        throw std::invalid_argument (
          "periodic terrain needs a duplicated seam");
    }

    const RecipeDomain2D& recipe_domain () const noexcept {
      return m_recipe_domain;
    }

    const TerrainGrid& grid () const noexcept {
      return m_grid;
    }

    RecipePosition2D recipe_position (GridPointIndex index) const {
      const auto [x, y] = coordinates (index);
      const float u = std::lerp (m_recipe_domain.min_x,
                                 m_recipe_domain.max_x,
                                 static_cast<float> (x) /
                                   static_cast<float> (m_grid.width - 1));
      const float v = std::lerp (m_recipe_domain.min_y,
                                 m_recipe_domain.max_y,
                                 static_cast<float> (y) /
                                   static_cast<float> (m_grid.height - 1));
      return { .u = recipe_coordinate (u), .v = recipe_coordinate (v) };
    }

    HorizontalPosition2D physical_position (GridPointIndex index) const {
      const auto [x, y] = coordinates (index);
      return { .x = static_cast<float> (x) * m_grid.spacing_x,
               .z = static_cast<float> (y) * m_grid.spacing_y };
    }

    SampleIndex flatten (GridPointIndex index) const {
      return m_grid.flatten (index);
    }

    GridPointIndex unflatten (SampleIndex index) const {
      return m_grid.unflatten (index);
    }

  private:
    std::pair<std::size_t, std::size_t>
    coordinates (GridPointIndex index) const {
      return m_grid.coordinates (index);
    }

    RecipeDomain2D m_recipe_domain;
    TerrainGrid m_grid;
  };
}

#endif
