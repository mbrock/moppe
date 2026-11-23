#include <tests/test.hh>

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/local_evolution.hh>
#include <moppe/terrain/world_recipe.hh>

#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace {
  using namespace moppe;
  using namespace moppe::terrain;

  struct Landscape {
    std::array<float, 3> elevation;
    float dry_fraction;
    float largest_catchment;
    size_t large_mouths;
  };

  Landscape measure (const map::SurfaceGeometry& surface,
                     SurfaceElevation sea) {
    const auto flood = analyze_standing_water (surface, sea);
    const auto drainage = analyze_wet_drainage (flood, census_lakes (flood));
    auto heights = spatial::get<surface_elevation> (surface);
    std::ranges::sort (heights);
    auto percentile = [&] (float fraction) {
      return surface_elevation_value (
        heights[size_t ((heights.size () - 1) * fraction)]);
    };
    Landscape result { { percentile (.1f), percentile (.5f), percentile (.9f) },
                       float (heights.end () -
                              std::upper_bound (
                                heights.begin (), heights.end (), sea)) /
                         heights.size (),
                       0,
                       0 };
    for (size_t i = 0; i < heights.size (); ++i) {
      auto j = drainage.receiver[i].value;
      if (flood.ocean[i] || !flood.ocean[j])
        continue;
      auto area =
        drainage.contributing_area_at (i).numerical_value_in (u::m * u::m);
      result.largest_catchment = std::max (result.largest_catchment, area);
      if (area > 100000)
        ++result.large_mouths;
    }
    return result;
  }
}

MOPPE_TEST (metal_flow_preserves_relief_watersheds_and_material_balance) {
  if (!MTLCreateSystemDefaultDevice ())
    return;
  for (unsigned seed : { 123u, 731u }) {
    auto extent = spatial_extent_in_metres (Vec3 (10000, 320, 10000));
    auto recipe = make_world_recipe (
      extent, 129, { seed }, 50 * u::m, TerrainGenerationProfile::Fast);
    map::SurfaceGeometry cpu (TerrainDomain (129, 129, extent));
    auto uplift =
      map::initialize_terrain (cpu, recipe.seed (), recipe.water_datum ());
    auto metal = cpu;
    auto p = recipe.evolution ();
    evolve_local (cpu, uplift, p, EvolutionBackend::Cpu);
    auto budget = evolve_local (metal, uplift, p, EvolutionBackend::Metal);
    auto a = measure (cpu, p.sea_level), b = measure (metal, p.sea_level);
    for (size_t i = 0; i < a.elevation.size (); ++i)
      MOPPE_CHECK (std::abs (a.elevation[i] - b.elevation[i]) < 8.f);
    MOPPE_CHECK (std::abs (a.dry_fraction - b.dry_fraction) < .05f);
    MOPPE_CHECK (b.large_mouths > 0 && a.large_mouths > 0);
    MOPPE_CHECK (b.large_mouths >= a.large_mouths / 2 &&
                 b.large_mouths <= 2 * a.large_mouths);
    MOPPE_CHECK (b.largest_catchment > .4f * a.largest_catchment &&
                 b.largest_catchment < 2.5f * a.largest_catchment);
    MOPPE_CHECK (budget.drainage_area_error.value () == 0 * u::m * u::m);
    MOPPE_CHECK (budget.exported_sediment > 0 * u::m * u::m * u::m);
    MOPPE_CHECK (abs (budget.balance_error) < 1e-5 * budget.tectonic_uplift);
  }
}

MOPPE_TEST (metal_routes_closed_flat_torus_to_its_minimum) {
  if (!MTLCreateSystemDefaultDevice ())
    return;
  TerrainDomain d (33, 21, 5 * u::m, 7 * u::m);
  map::SurfaceGeometry surface (d);
  auto& h = spatial::get<surface_elevation> (surface);
  std::ranges::fill (h, surface_elevation_point (65 * u::m));
  h[35] = surface_elevation_point (60 * u::m);
  std::vector<meters_per_julian_year_t> uplift (d.size ());
  StreamPowerEvolution p;
  p.duration = p.time_step;
  auto report = evolve_local (surface, uplift, p, EvolutionBackend::Metal);
  MOPPE_CHECK (report.drainage_area_error.value () == 0 * u::m * u::m);
  MOPPE_CHECK (abs (report.balance_error) < 1 * u::m * u::m * u::m);
}

MOPPE_TEST (metal_multigrid_transfer_preserves_the_final_volume) {
  if (!MTLCreateSystemDefaultDevice ())
    return;
  auto extent = spatial_extent_in_metres (Vec3 (10000, 320, 10000));
  auto recipe = make_world_recipe (
    extent, 1024, { 123 }, 50 * u::m, TerrainGenerationProfile::Fast);
  map::SurfaceGeometry surface (TerrainDomain (1024, 1024, extent));
  const auto uplift =
    map::initialize_terrain (surface, recipe.seed (), recipe.water_datum ());
  const auto initial = spatial::get<surface_elevation> (surface);
  const auto budget = evolve_local (
    surface, uplift, recipe.evolution (), EvolutionBackend::MetalMultigrid);
  const auto& heights = spatial::get<surface_elevation> (surface);
  double change_m = 0;
  for (size_t i = 0; i < heights.size (); ++i)
    change_m += double (surface_elevation_value (heights[i])) -
                double (surface_elevation_value (initial[i]));
  const auto change = change_m * u::m * surface.domain ().cell_area ();
  MOPPE_CHECK (budget.drainage_area_error.value () == 0 * u::m * u::m);
  MOPPE_CHECK (budget.exported_sediment > 0 * u::m * u::m * u::m);
  MOPPE_CHECK (abs (change + budget.exported_sediment -
                    budget.tectonic_uplift) < 1e-5 * budget.tectonic_uplift);
}
