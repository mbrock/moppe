#include <moppe/game/bike_model.hh>
#include <moppe/game/boat_model.hh>
#include <moppe/game/bus_model.hh>
#include <moppe/game/caddy_model.hh>
#include <moppe/game/chick_model.hh>
#include <moppe/game/cow_model.hh>
#include <moppe/game/farm_model.hh>
#include <moppe/game/glider_model.hh>
#include <moppe/game/helicopter_model.hh>
#include <moppe/game/hen_model.hh>
#include <moppe/game/horse_model.hh>
#include <moppe/game/jeep_model.hh>
#include <moppe/game/jet_ski_model.hh>
#include <moppe/game/long_john_model.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/moose_model.hh>
#include <moppe/game/mountain_bike_model.hh>
#include <moppe/game/pig_model.hh>
#include <moppe/game/reindeer_model.hh>
#include <moppe/game/roe_deer_model.hh>
#include <moppe/game/rooster_model.hh>
#include <moppe/game/sheep_model.hh>
#include <moppe/game/snowmobile_model.hh>
#include <moppe/game/tractor_model.hh>
#include <moppe/game/trailer_model.hh>

#include <tests/test.hh>

#include <array>

namespace {
  using namespace moppe;
}

// The exported cabin has glazing inset among opaque panels. Both the
// streamed recorder and baked meshes consume these same draw runs.
MOPPE_TEST (helicopter_panes_share_a_late_run_and_keep_their_finish) {
  render::DrawList dl;
  dl.fogged (false);
  game::model_mesh::record (dl, game::helicopter_model::body);
  const auto& runs = dl.runs ();
  MOPPE_CHECK (runs.size () == 2);
  MOPPE_CHECK (!runs[0].state.glass && runs[0].state.depth_write);
  MOPPE_CHECK (runs[1].state.glass && runs[1].state.blend);
  MOPPE_CHECK (!runs[1].state.depth_write);
  MOPPE_CHECK (runs[1].count > 0 && runs[0].count > runs[1].count);
  MOPPE_CHECK (runs[0].count + runs[1].count ==
               game::helicopter_model::body.triangle_count * 3);
  MOPPE_CHECK (!dl.state ().glass && dl.state ().depth_write);
  const auto& pane = dl.vertices ()[runs[1].first];
  MOPPE_CHECK ((pane.fogged & 1) == 0);
  MOPPE_CHECK ((pane.fogged >> 1) > 0);
}

MOPPE_TEST (boat_hull_loads_compressed_bundles_and_weathered_film) {
  render::DrawList dl;
  game::model_mesh::record (dl, game::boat_model::hull);
  MOPPE_CHECK (dl.vertices ().size () ==
               game::boat_model::hull.triangle_count * 3);
  bool has_worn = false;
  bool has_plain = false;
  for (const render::Vertex& v : dl.vertices ()) {
    if (v.lit >= 2)
      has_worn = true;
    else
      has_plain = true;
  }
  MOPPE_CHECK (has_worn);
  MOPPE_CHECK (has_plain);
}

MOPPE_TEST (every_exported_model_loads_from_its_compressed_arrow_bundles) {
  const std::array<const game::model_mesh::Mesh*, 24> samples {
    &game::bike_model::chassis,    &game::boat_model::hull,
    &game::bus_model::body,        &game::caddy_model::body,
    &game::chick_model::body,      &game::cow_model::body,
    &game::farm_model::barn,       &game::glider_model::glider,
    &game::helicopter_model::body, &game::hen_model::body,
    &game::horse_model::body,      &game::jeep_model::body,
    &game::jet_ski_model::hull,    &game::long_john_model::frame,
    &game::moose_model::body,      &game::mountain_bike_model::frame,
    &game::pig_model::body,        &game::reindeer_model::body,
    &game::roe_deer_model::body,   &game::rooster_model::body,
    &game::sheep_model::body,      &game::snowmobile_model::body,
    &game::tractor_model::body,    &game::trailer_model::body,
  };
  for (const game::model_mesh::Mesh* mesh : samples) {
    render::DrawList dl;
    game::model_mesh::record (dl, *mesh);
    MOPPE_CHECK (dl.vertices ().size () == mesh->triangle_count * 3);
  }
  game::model_mesh::clear_cache ();
}
