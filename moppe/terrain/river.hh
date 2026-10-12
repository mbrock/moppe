#ifndef MOPPE_TERRAIN_RIVER_HH
#define MOPPE_TERRAIN_RIVER_HH

#include <moppe/terrain/domain.hh>

namespace moppe::terrain {
  using cubic_meters_per_second_t =
    mp_units::quantity<mp_units::si::metre * mp_units::si::metre *
                         mp_units::si::metre / mp_units::si::second,
                       float>;

  // Hydraulic geometry for the visible water surface. Orogeny owns the
  // valley shape; these laws only decide how much water its drainage axis
  // carries and how wide a river that much water makes for itself.
  //
  // The discharge is a catchment's in spate, the stage that shapes a
  // channel: every square kilometre sends two and a half cubic metres a
  // second down its river. Where that water stands is not a law here but a
  // reading of the terrain (watercourse.hh): the stage that carries the
  // discharge through each cross-section of the valley.
  cubic_meters_per_second_t
  river_discharge (square_meters_t contributing_area) noexcept;

  // The width a gravel river in regime gives itself, growing as the square
  // root of its discharge. Running water spreads no wider than a little
  // over this however flat its valley floor.
  meters_t river_width (square_meters_t contributing_area) noexcept;
  // The depth of the sheet a nickpoint's curtain falls as.
  meters_t river_depth (square_meters_t contributing_area) noexcept;

  // A visible river begins at a physical five-metre bankfull width. Terrain
  // resolution may change how finely that water is painted, but must not
  // silently change which tributaries and confluences exist in the world.
  square_meters_t visible_river_minimum_area () noexcept;
}

#endif
