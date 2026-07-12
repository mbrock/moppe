#ifndef MOPPE_TERRAIN_DISCRETIZATION_HH
#define MOPPE_TERRAIN_DISCRETIZATION_HH

#include <moppe/quantities.hh>
#include <moppe/terrain/field.hh>
#include <moppe/terrain/topology.hh>

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace moppe::terrain {
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

    RecipePosition2D recipe_position (std::size_t x, std::size_t y) const {
      check_grid_point (x, y);
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

    HorizontalPosition2D physical_position (std::size_t x,
                                            std::size_t y) const {
      check_grid_point (x, y);
      return { .x = static_cast<float> (x) * m_grid.spacing_x,
               .z = static_cast<float> (y) * m_grid.spacing_y };
    }

  private:
    void check_grid_point (std::size_t x, std::size_t y) const {
      if (x >= m_grid.width || y >= m_grid.height)
        throw std::out_of_range ("terrain grid point outside discretization");
    }

    RecipeDomain2D m_recipe_domain;
    TerrainGrid m_grid;
  };
}

#endif
