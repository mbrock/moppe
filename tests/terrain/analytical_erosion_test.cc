#include <moppe/terrain/analytical_erosion.hh>

#include <tests/test.hh>

#include <array>

using namespace moppe::terrain;

namespace {
  constexpr std::array downhill_plane { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f,
                                        0.1f, 0.2f, 0.3f, 0.4f, 0.5f,
                                        0.2f, 0.3f, 0.4f, 0.5f, 0.6f,
                                        0.3f, 0.4f, 0.5f, 0.6f, 0.7f,
                                        0.4f, 0.5f, 0.6f, 0.7f, 0.8f };

  TerrainView plane () {
    return TerrainView ({ .width = 5,
                          .height = 5,
                          .spacing_x = 10.0f * mp_units::si::metre,
                          .spacing_y = 10.0f * mp_units::si::metre,
                          .height_scale = 100.0f * mp_units::si::metre },
                        downhill_plane);
  }
}

MOPPE_TEST (zero_age_analytical_erosion_is_identity) {
  const AnalyticalErosionResult result =
    erode_analytically (plane (),
                        { .duration = 0.0f * mp_units::astronomy::Julian_year,
                          .sea_level = -1.0f });

  MOPPE_CHECK (result.heights.size () == downhill_plane.size ());
  for (std::size_t i = 0; i < downhill_plane.size (); ++i)
    MOPPE_CHECK_NEAR (result.heights[i], downhill_plane[i], 1e-7f);
  MOPPE_CHECK_NEAR (
    static_cast<float> (result.report.mean_absolute_change_m), 0.0f, 0.0f);
}

MOPPE_TEST (analytical_stream_power_lowers_a_fixed_drainage_tree) {
  const AnalyticalErosion parameters {
    .duration = 100000.0f * mp_units::astronomy::Julian_year,
    .uplift_rate =
      0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year,
    .sea_level = -1.0f
  };
  const AnalyticalErosionResult first =
    erode_analytically (plane (), parameters);
  const AnalyticalErosionResult second =
    erode_analytically (plane (), parameters);

  MOPPE_CHECK (first.heights == second.heights);
  MOPPE_CHECK (first.report.fixed_boundaries == 1);
  MOPPE_CHECK_NEAR (first.heights[0], downhill_plane[0], 0.0f);
  MOPPE_CHECK (first.report.lowered_volume_m3 > 0.0);
  MOPPE_CHECK_NEAR (
    static_cast<float> (first.report.raised_volume_m3), 0.0f, 1e-5f);
  for (std::size_t i = 0; i < downhill_plane.size (); ++i)
    MOPPE_CHECK (first.heights[i] <= downhill_plane[i] + 1e-7f);
}

MOPPE_TEST (uplift_competes_with_analytical_stream_power) {
  const AnalyticalErosionResult eroded = erode_analytically (
    plane (),
    { .duration = 100000.0f * mp_units::astronomy::Julian_year,
      .uplift_rate =
        0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year,
      .sea_level = -1.0f });
  const AnalyticalErosionResult uplifted = erode_analytically (
    plane (),
    { .duration = 100000.0f * mp_units::astronomy::Julian_year,
      .uplift_rate =
        0.001f * mp_units::si::metre / mp_units::astronomy::Julian_year,
      .sea_level = -1.0f });

  MOPPE_CHECK (uplifted.heights[24] > eroded.heights[24]);
  MOPPE_CHECK_NEAR (uplifted.heights[0], downhill_plane[0], 0.0f);
}
