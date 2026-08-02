#include <moppe/game/waterfall_surface.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>

using namespace moppe;

namespace {
  map::SurfaceGeometry waterfall_test_surface () {
    map::SurfaceGeometry surface (
      terrain::TerrainDomain (5, 5, 10.0f * u::m, 10.0f * u::m));
    for (std::size_t z = 0; z < surface.domain ().height (); ++z)
      for (std::size_t x = 0; x < surface.domain ().width (); ++x) {
        const float height = z <= 1 ? 12.0f : 3.0f;
        spatial::get<terrain::surface_elevation> (
          surface[terrain::TerrainIndex { x, z }]) =
          terrain::surface_elevation_point (height * u::m);
      }
    map::rebuild_geometry (surface);
    return surface;
  }

  terrain::Waterfall test_waterfall () {
    return { .reach_id = terrain::RiverReachId { 0 },
             .lip_cell = terrain::CellIndex { 6 },
             .foot_cell = terrain::CellIndex { 11 },
             .drop = 9.0f * u::m,
             .horizontal_distance = 10.0f * u::m,
             .slope = 0.9f * terrain::terrain_slope[mp_units::one],
             .contributing_area = 400000.0f * u::m * u::m };
  }
}

MOPPE_TEST (waterfall_surface_is_empty_without_nickpoints) {
  const map::SurfaceGeometry surface = waterfall_test_surface ();
  const terrain::RiverNetwork rivers {};
  MOPPE_CHECK (game::build_waterfall_curtains (surface, rivers).empty ());
}

MOPPE_TEST (waterfall_curtain_is_small_vertical_and_flow_encoded) {
  const map::SurfaceGeometry surface = waterfall_test_surface ();
  terrain::RiverNetwork rivers;
  rivers.waterfalls.push_back (test_waterfall ());

  const render::DrawList draw =
    game::build_waterfall_curtains (surface, rivers);

  // Six cross-stream strips by six vertical strips, two triangles each.
  MOPPE_CHECK (draw.vertices ().size () == 6 * 6 * 6);
  float highest = -1000.0f;
  float lowest = 1000.0f;
  float widest_top = 0.0f;
  float widest_bottom = 0.0f;
  bool has_opaque_center = false;
  for (const render::Vertex& vertex : draw.vertices ()) {
    highest = std::max (highest, vertex.py);
    lowest = std::min (lowest, vertex.py);
    if (std::abs (vertex.py - highest) < 0.1f)
      widest_top = std::max (widest_top, std::abs (vertex.px - 10.0f));
    if (vertex.py < 4.0f)
      widest_bottom = std::max (widest_bottom, std::abs (vertex.px - 10.0f));
    MOPPE_CHECK (vertex.color.r == 255);
    MOPPE_CHECK (vertex.color.b == 255);
    has_opaque_center = has_opaque_center || vertex.color.a == 255;
  }
  MOPPE_CHECK (highest - lowest > 8.0f);
  MOPPE_CHECK (widest_bottom > widest_top);
  MOPPE_CHECK (has_opaque_center);
}

MOPPE_TEST (waterfall_mesh_cost_depends_on_falls_not_reach_length) {
  const map::SurfaceGeometry surface = waterfall_test_surface ();
  terrain::RiverNetwork rivers;
  terrain::RiverReach reach;
  reach.id = terrain::RiverReachId { 0 };
  reach.alignment.points.resize (10000);
  rivers.reaches.push_back (std::move (reach));
  rivers.waterfalls.push_back (test_waterfall ());

  const render::DrawList draw =
    game::build_waterfall_curtains (surface, rivers);
  MOPPE_CHECK (draw.vertices ().size () == 6 * 6 * 6);
}
