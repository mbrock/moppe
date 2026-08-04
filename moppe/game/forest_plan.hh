#ifndef MOPPE_GAME_FOREST_PLAN_HH
#define MOPPE_GAME_FOREST_PLAN_HH

#include <moppe/game/foliage_kind.hh>
#include <moppe/map/surface.hh>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace moppe::game {
  using ForestForm = FoliageKind;

  using TreeSizeFactor =
    mp_units::quantity<tree_size_factor[mp_units::one], float>;

  struct ForestSite {
    position_t position {};
    terrain::TerrainNormal normal {};
    map::ForestCover cover {};
    map::SurfaceMoisture moisture {};
    TreeSizeFactor size = 1.0f * tree_size_factor[mp_units::one];
    std::uint32_t seed = 0;
    ForestForm form = ForestForm::broadleaf;
  };

  struct ForestPlan {
    std::vector<ForestSite> sites;
    spatial_extent_t period {};
  };

  // Convert the continuous canopy field into stable individuals on a
  // jittered grid. Positions and identities depend only on world seed and
  // lattice cell, so revisiting an area never produces a different forest.
  [[nodiscard]] ForestPlan
  plan_global_forest (const map::SurfaceGeometry& surface,
                      const map::SurfaceReadings& readings,
                      std::uint32_t seed,
                      meters_t spacing = 12.0f * u::m);

  // The plan is renderer-independent and expensive to derive over a large
  // surface, so finished worlds persist it beside their typed fields.
  void save_forest_plan (const ForestPlan& plan,
                         std::uint32_t seed,
                         const std::string& path);
  [[nodiscard]] std::optional<ForestPlan>
  try_load_forest_plan (const std::string& path,
                        std::uint32_t seed,
                        const spatial_extent_t& expected_period);
}

#endif
