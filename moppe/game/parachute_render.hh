#ifndef MOPPE_GAME_PARACHUTE_RENDER_HH
#define MOPPE_GAME_PARACHUTE_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>

namespace moppe::game {
  // The hiker's personal parachute: a 12-gore coral-red and ivory-cream
  // billowing dome canopy with an apex vent, suspension lines, risers, and
  // yellow steering toggles held overhead. Set `show_rider` to false in
  // first-person view so looking up shows the canopy without drawing the body.
  void render_parachute (render::DrawList& dl,
                         const ParachutePose& parachute,
                         seconds_t time,
                         Outfit outfit = Outfit::blue_jacket,
                         bool show_rider = true);
}

#endif
