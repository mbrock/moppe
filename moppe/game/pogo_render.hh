#ifndef MOPPE_GAME_POGO_RENDER_HH
#define MOPPE_GAME_POGO_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>

namespace moppe::game {
  // The mechanical pogo stick (chrome telescoping rod, animated 3D helical
  // spring, candy-orange housing, footpegs, T-bar with grips, rubber bounce
  // tip) and the hiker riding it, crouching into each bounce. Set
  // `show_rider` to false in first-person view so only the stick draws.
  void render_pogo (render::DrawList& dl,
                    const PogoPose& pogo,
                    seconds_t time,
                    Outfit outfit = Outfit::blue_jacket,
                    bool show_rider = true);
}

#endif
