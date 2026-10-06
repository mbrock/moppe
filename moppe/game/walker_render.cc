#include <moppe/game/walker_render.hh>

#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>

namespace moppe::game {
  void
  render_walker (render::DrawList& draw, const WalkerPose& walker, float time) {
    figure::draw (draw, pose_avatar (walker, time));
  }
}
