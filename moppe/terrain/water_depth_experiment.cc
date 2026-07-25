#include <moppe/map/surface.hh>
#include <moppe/map/terrain_generation.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/world_recipe.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
  using namespace moppe;
  using namespace moppe::terrain;

  constexpr float world_width_m = 5000.0f;
  constexpr float world_height_m = 320.0f;
  constexpr float sea_level_m = 50.0f;

  struct DepthPopulation {
    std::size_t bodies = 0;
    std::vector<float> depths_m;
  };

  constexpr std::size_t category_count = 4;

  std::size_t category_index (WaterBodyClass classification) {
    switch (classification) {
    case WaterBodyClass::Sea:
      return 0;
    case WaterBodyClass::Lake:
      return 1;
    case WaterBodyClass::Pond:
      return 2;
    case WaterBodyClass::Puddle:
      return 3;
    }
    throw std::logic_error ("unknown water body classification");
  }

  constexpr std::array<std::string_view, category_count> category_names {
    "sea", "lake", "pond", "puddle"
  };

  float percentile (const std::vector<float>& sorted, float proportion) {
    if (sorted.empty ())
      return 0.0f;
    const float index = proportion * static_cast<float> (sorted.size () - 1);
    const std::size_t lower = static_cast<std::size_t> (index);
    const std::size_t upper = std::min (lower + 1, sorted.size () - 1);
    const float fraction = index - static_cast<float> (lower);
    return std::lerp (sorted[lower], sorted[upper], fraction);
  }

  double shallow_fraction (const std::vector<float>& sorted,
                           float threshold_m) {
    if (sorted.empty ())
      return 0.0;
    return static_cast<double> (
             std::upper_bound (sorted.begin (), sorted.end (), threshold_m) -
             sorted.begin ()) /
           static_cast<double> (sorted.size ());
  }

  TerrainGenerationProfile parse_profile (std::string_view text) {
    if (text == "fast")
      return TerrainGenerationProfile::Fast;
    if (text == "play")
      return TerrainGenerationProfile::Play;
    if (text == "research")
      return TerrainGenerationProfile::Research;
    throw std::invalid_argument ("profile must be fast, play, or research");
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    const int resolution = argc > 1 ? std::stoi (argv[1]) : 1025;
    const int seed_count = argc > 2 ? std::stoi (argv[2]) : 12;
    const auto first_seed =
      static_cast<std::uint32_t> (argc > 3 ? std::stoul (argv[3]) : 123);
    const TerrainGenerationProfile profile =
      parse_profile (argc > 4 ? argv[4] : "fast");
    if (resolution < 2 || seed_count < 1)
      throw std::invalid_argument (
        "resolution must be at least 2 and seed count must be positive");

    std::array<DepthPopulation, category_count> populations;
    const float sea_level = sea_level_m;
    const double cell_area_m2 = world_width_m * world_width_m /
                                static_cast<double> (resolution * resolution);

    std::cout << "seed,sea_bodies,lakes,ponds,puddles,sea_cells,lake_cells,"
                 "pond_cells,puddle_cells\n";
    for (int offset = 0; offset < seed_count; ++offset) {
      const std::uint32_t seed =
        first_seed + static_cast<std::uint32_t> (offset);
      map::Surface map (resolution,
                        resolution,
                        Vec3 (world_width_m, world_height_m, world_width_m));
      const WorldRecipe recipe =
        make_world_recipe (spatial_extent_in_metres (Vec3 (
                             world_width_m, world_height_m, world_width_m)),
                           resolution,
                           Seed { seed },
                           sea_level_m * u::m,
                           profile);
      const auto uplift =
        map::initialize_terrain (map, recipe.seed (), recipe.water_datum ());
      map::evolve_terrain (map, uplift, recipe.evolution ());

      const FloodField flood =
        analyze_standing_water (map.geometry (), sea_level);
      const LakeCensus census = census_lakes (flood);
      std::array<std::size_t, category_count> bodies {};
      std::array<std::size_t, category_count> cells {};
      for (const WaterBody& body : census.bodies) {
        const std::size_t category = category_index (body.classification);
        ++bodies[category];
        ++populations[category].bodies;
      }
      const std::span<const StandingWaterDepth> depths = flood.water_depths ();
      for (std::size_t cell = 0; cell < depths.size (); ++cell) {
        const WaterBodyId body = census.body[cell];
        if (body == LakeCensus::dry)
          continue;
        const std::size_t category =
          category_index (census.bodies[body].classification);
        ++cells[category];
        populations[category].depths_m.push_back (
          depths[cell].numerical_value_in (u::m));
      }
      std::cout << seed;
      for (const std::size_t value : bodies)
        std::cout << ',' << value;
      for (const std::size_t value : cells)
        std::cout << ',' << value;
      std::cout << '\n';
    }

    std::cout << "\nkind,bodies,cells,mean_area_fraction,area_km2,volume_Mm3,"
                 "mean_depth_m,p10_m,p25_m,p50_m,p75_m,p90_m,p99_m,max_m,"
                 "fraction_le_0.05m,fraction_le_0.10m,fraction_le_0.25m,"
                 "fraction_le_0.50m,fraction_le_1m,fraction_le_2m,"
                 "fraction_le_5m,fraction_le_10m,fraction_le_20m\n";
    std::cout << std::fixed << std::setprecision (4);
    const double cells_per_world =
      static_cast<double> (resolution - 1) * (resolution - 1);
    for (std::size_t category = 0; category < category_count; ++category) {
      DepthPopulation& population = populations[category];
      std::sort (population.depths_m.begin (), population.depths_m.end ());
      const double cells = static_cast<double> (population.depths_m.size ());
      const double depth_sum = std::accumulate (
        population.depths_m.begin (), population.depths_m.end (), 0.0);
      const double area_m2 = cells * cell_area_m2;
      const double volume_m3 = depth_sum * cell_area_m2;
      std::cout << category_names[category] << ',' << population.bodies << ','
                << population.depths_m.size () << ','
                << cells / (cells_per_world * seed_count) << ','
                << area_m2 / 1e6 << ',' << volume_m3 / 1e6 << ','
                << (cells > 0.0 ? depth_sum / cells : 0.0) << ','
                << percentile (population.depths_m, 0.10f) << ','
                << percentile (population.depths_m, 0.25f) << ','
                << percentile (population.depths_m, 0.50f) << ','
                << percentile (population.depths_m, 0.75f) << ','
                << percentile (population.depths_m, 0.90f) << ','
                << percentile (population.depths_m, 0.99f) << ','
                << percentile (population.depths_m, 1.0f) << ','
                << shallow_fraction (population.depths_m, 0.05f) << ','
                << shallow_fraction (population.depths_m, 0.10f) << ','
                << shallow_fraction (population.depths_m, 0.25f) << ','
                << shallow_fraction (population.depths_m, 0.50f) << ','
                << shallow_fraction (population.depths_m, 1.0f) << ','
                << shallow_fraction (population.depths_m, 2.0f) << ','
                << shallow_fraction (population.depths_m, 5.0f) << ','
                << shallow_fraction (population.depths_m, 10.0f) << ','
                << shallow_fraction (population.depths_m, 20.0f) << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "water-depth experiment: " << error.what () << '\n';
    std::cerr << "usage: terrain-water-depth-experiment "
                 "[resolution] [seed-count] [first-seed] "
                 "[fast|play|research] [coastline] "
                 "[bathymetric-relief-m]\n";
    return -1;
  }
  return 0;
}
