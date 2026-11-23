#include <moppe/game/helicopter_model.hh>
#include <moppe/game/model_mesh.hh>

#include <tests/test.hh>

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
               game::helicopter_model::body.triangles.size () * 3);
  MOPPE_CHECK (!dl.state ().glass && dl.state ().depth_write);
  const auto& pane = dl.vertices ()[runs[1].first];
  MOPPE_CHECK ((pane.fogged & 1) == 0);
  MOPPE_CHECK ((pane.fogged >> 1) > 0);
}
