#include <moppe/terrain/river.hh>
#include <moppe/terrain/sediment_transport.hh>

#include <tests/test.hh>

#include <cmath>

using namespace moppe::terrain;

MOPPE_TEST (visible_river_threshold_is_a_physical_five_metre_channel) {
  const square_meters_t area = visible_river_minimum_area ();
  MOPPE_CHECK_NEAR (
    (area).numerical_value_in (u::m * u::m), 173611.109375f, 0.02f);
  MOPPE_CHECK_NEAR (
    (river_width (area)).numerical_value_in (u::m), 5.0f, 1e-5f);
}

MOPPE_TEST (a_river_carries_its_catchments_spate) {
  const square_meters_t square_kilometre = 1000000.0f * u::m * u::m;
  const auto discharge = [] (square_meters_t area) {
    return river_discharge (area).numerical_value_in (u::m * u::m * u::m /
                                                      u::s);
  };
  MOPPE_CHECK_NEAR (discharge (square_kilometre), 2.5f, 1e-5f);
  MOPPE_CHECK_NEAR (discharge (13.0f * square_kilometre), 32.5f, 1e-4f);
  // A river in regime is as wide as the root of its discharge.
  const float narrow = river_width (square_kilometre).numerical_value_in (u::m);
  const float wide =
    river_width (4.0f * square_kilometre).numerical_value_in (u::m);
  MOPPE_CHECK_NEAR (wide, 2.0f * narrow, 1e-4f);
  MOPPE_CHECK_NEAR (
    narrow / std::sqrt (discharge (square_kilometre)), 7.58947f, 1e-4f);
}

MOPPE_TEST (river_hydraulic_geometry_has_a_motorcycle_scale_hierarchy) {
  const square_meters_t stream = visible_river_minimum_area ();
  const square_meters_t tributary = 1000000.0f * u::m * u::m;
  const square_meters_t trunk = 13000000.0f * u::m * u::m;

  MOPPE_CHECK (river_width (stream) < river_width (tributary));
  MOPPE_CHECK (river_width (tributary) < river_width (trunk));
  MOPPE_CHECK (river_depth (stream) < river_depth (tributary));
  MOPPE_CHECK (river_depth (tributary) < river_depth (trunk));
  // The width keeps growing with the water: nothing holds a trunk to a
  // tributary's size.
  MOPPE_CHECK_NEAR (
    (river_width (trunk)).numerical_value_in (u::m), 43.2666f, 1e-3f);
  MOPPE_CHECK_NEAR (
    (river_depth (trunk)).numerical_value_in (u::m), 2.5f, 0.0f);
  MOPPE_CHECK_NEAR ((alluvial_valley_width (stream)).numerical_value_in (u::m),
                    22.6666679f,
                    1e-4f);
  MOPPE_CHECK_NEAR ((alluvial_valley_width (trunk)).numerical_value_in (u::m),
                    150.2220459f,
                    1e-4f);
}
