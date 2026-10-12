#include <moppe/terrain/channel.hh>

#include <tests/test.hh>

#include <cmath>
#include <cstddef>
#include <vector>

using namespace moppe::terrain;

namespace {
  constexpr int wide = 15;
  constexpr int axis = 7;

  // A valley five metres a sample, falling three in a hundred toward the
  // sea at its foot, its sides rising five in a hundred from the axis.
  // BED lowers or raises the axis row by row.
  template <typename Bed>
  std::vector<float> valley (int rows, Bed&& bed) {
    std::vector<float> heights (static_cast<std::size_t> (wide) * rows);
    for (int row = 0; row < rows; ++row)
      for (int x = 0; x < wide; ++x) {
        const float fall = 0.15f * static_cast<float> (rows - 2 - row);
        const float side = 0.25f * static_cast<float> (std::abs (x - axis));
        heights[static_cast<std::size_t> (row) * wide + x] =
          row == rows - 1 ? -3.0f
                          : 1.0f + fall + side +
                              (std::abs (x - axis) <= 1 ? bed (row) : 0.0f);
      }
    return heights;
  }

  struct Formed {
    std::vector<float> before;
    std::vector<float> after;
    // The water that stood before the rivers had beds, and after.
    FloodField flood;
    FloodField flooded;
  };

  Formed form (int rows,
               const std::vector<float>& heights,
               const ChannelFormation& parameters = {}) {
    const TerrainDomain grid (static_cast<std::size_t> (wide),
                              static_cast<std::size_t> (rows),
                              5.0f * mp_units::si::metre,
                              5.0f * mp_units::si::metre);
    const ElevationMap terrain = make_elevation_map (grid, heights);
    FloodField flood = analyze_standing_water (terrain, 0.0f);
    LakeCensus census = census_lakes (flood);
    const DrainageGraph drainage = analyze_wet_drainage (flood, census);
    const RiverNetwork rivers = extract_river_network (
      flood,
      census,
      drainage,
      200.0f * mp_units::si::metre * mp_units::si::metre);
    const ChannelFormationResult result =
      form_channels (terrain, flood, census, drainage, rivers, parameters);
    std::vector<float> after;
    for (const SurfaceElevation height : result.heights)
      after.push_back (surface_elevation_value (height));
    FloodField flooded =
      analyze_standing_water (make_elevation_map (grid, after), 0.0f);
    return {
      heights, std::move (after), std::move (flood), std::move (flooded)
    };
  }

  std::size_t at (int row, int x = axis) {
    return static_cast<std::size_t> (row) * wide + x;
  }
}

MOPPE_TEST (a_rivers_bed_only_falls_through_a_hollow_and_its_sill) {
  // Three rows of the axis lie a metre below the sill after them: a hollow
  // the flood would stand a pond in.
  constexpr int rows = 26;
  const Formed formed = form (rows, valley (rows, [] (int row) {
                                return row >= 10 && row <= 12 ? -1.2f : 0.0f;
                              }));
  MOPPE_CHECK (formed.flood.water_depth_m (at (11)) > 0.5f);
  MOPPE_CHECK (formed.before[at (13)] > formed.before[at (12)]);

  // The river's bed now falls all the way to the sea: nowhere along it
  // does water stand any more.
  for (int row = 3; row + 2 < rows; ++row)
    for (int x = axis - 1; x <= axis + 1; ++x)
      MOPPE_CHECK (formed.flooded.water_depth_m (at (row, x)) < 0.06f);
  // It got there from both sides: the hollow filled part of the way, its
  // sill cut the rest, and neither moved further than a river moves its
  // bed.
  MOPPE_CHECK (formed.after[at (11)] > formed.before[at (11)] + 0.2f);
  MOPPE_CHECK (formed.after[at (13)] < formed.before[at (13)] - 0.2f);
  MOPPE_CHECK (formed.after[at (13)] > formed.before[at (13)] - 1.6f);
  // The valley's sides stand as they did.
  for (int row = 0; row + 1 < rows; ++row) {
    MOPPE_CHECK_NEAR (
      formed.after[at (row, 1)], formed.before[at (row, 1)], 0.0f);
    MOPPE_CHECK_NEAR (formed.after[at (row, wide - 2)],
                      formed.before[at (row, wide - 2)],
                      0.0f);
  }
}

MOPPE_TEST (a_lake_keeps_its_sill_and_a_pool_loses_it) {
  // A basin two metres deep across the valley's floor, forty metres long:
  // a lake by any brook's measure.
  constexpr int rows = 34;
  const auto basin = [] (int row) {
    return row >= 10 && row <= 17 ? -2.0f : 0.0f;
  };
  std::vector<float> heights = valley (rows, basin);
  for (int row = 10; row <= 17; ++row)
    for (int x = axis - 3; x <= axis + 3; ++x)
      heights[at (row, x)] = heights[at (row)];
  const Formed kept = form (rows, heights);
  const float level = kept.flood.water_level_m (at (13));
  MOPPE_CHECK (kept.flood.water_depth_m (at (13)) > 1.0f);
  // The lake's floor and its sill are where they were, and below the sill
  // the river has a bed.
  MOPPE_CHECK_NEAR (kept.after[at (13)], kept.before[at (13)], 0.0f);
  MOPPE_CHECK_NEAR (kept.after[at (18)], kept.before[at (18)], 1e-3f);
  MOPPE_CHECK (kept.before[at (18)] >= level - 1e-3f);
  MOPPE_CHECK (kept.after[at (24)] < kept.before[at (24)] - 0.1f);

  // Called a pool, the same water loses its sill to the river.
  ChannelFormation parameters;
  parameters.lake_widths = 1e6f;
  const Formed cut = form (rows, heights, parameters);
  MOPPE_CHECK (cut.after[at (18)] < cut.before[at (18)] - 0.2f);
}
