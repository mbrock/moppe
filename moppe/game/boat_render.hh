#ifndef MOPPE_GAME_BOAT_RENDER_HH
#define MOPPE_GAME_BOAT_RENDER_HH

#include <moppe/color.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <deque>
#include <functional>
#include <optional>

namespace moppe::game {
  // The boat (models/boat.blend) as baked meshes drawn through the
  // renderer: the hull, the long-tail swung and tipped about its pivot,
  // and the propeller turning on the end of its shaft. A helmsman, given
  // their outfit, sits on the after seat with a hand on the tiller,
  // skinned into the frame's draw list. Given a `rod` (game/fishing.hh),
  // the helmsman fishes with it, and it is drawn even without them, as
  // seen through their eyes; `time` keeps its float bobbing. Someone
  // riding along, given their outfit, sits on the fore seat.
  void render_boat (render::Renderer& r,
                    render::DrawList& dl,
                    const BoatPose& boat,
                    std::optional<Outfit> helmsman,
                    uint64_t motion_base,
                    const std::optional<RodPose>& rod = std::nullopt,
                    seconds_t time = 0.0f * u::s,
                    std::optional<Outfit> passenger = std::nullopt);

  // Where the boat's stern passed and how fast, kept a few seconds so
  // that its wake can be drawn spreading behind it on the water.
  struct WakeMark {
    position_t stern;   // on the water, under the transom
    direction_t across; // level, to port
    speed_t speed = 0.0f * u::m / u::s;
    seconds_t time = 0.0f * u::s;
    // How much foam there is here, so that it lies in patches.
    proportion_t foam = 1.0f * proportion[one];
  };

  class BoatWake {
  public:
    static constexpr seconds_t lasts = 7.0f * u::s;
    // Marks the stern at `time` if it has moved on and is under way in
    // water `depth` deep.
    void follow (const BoatPose& boat,
                 meters_t water_level,
                 meters_t depth,
                 seconds_t time);
    void clear () {
      m_marks.clear ();
    }
    // The two arms of the wake spreading at the Kelvin angle and the
    // churned water between them behind the propeller, fading with age;
    // `light` dims the foam with the day. `level` is the water's surface
    // at a place now, which the wake lies on as the waves pass under it.
    void draw (render::DrawList& dl,
               seconds_t time,
               proportion_t light,
               const std::function<meters_t (const position_t&)>& level) const;

  private:
    std::deque<WakeMark> m_marks;
  };

  // The boats' wakes, ours and the other player's (their jet ski's too,
  // when they have one in the world): each laid behind its boat as it
  // goes, on the water's `level`, over the `ground`, and drawn over the
  // water.
  class BoatWakes {
  public:
    void draw (render::Renderer& r,
               const BoatPose& boat,
               const JetSkiPose& jet_ski,
               const BoatPose* their_boat,
               const JetSkiPose* their_jet_ski,
               const std::function<meters_t (const position_t&)>& level,
               const std::function<meters_t (const position_t&)>& ground,
               seconds_t time,
               proportion_t daylight);

  private:
    BoatWake m_boat;
    BoatWake m_jet_ski;
    BoatWake m_their_boat;
    BoatWake m_their_jet_ski;
    render::DrawList m_dl;
  };
}

#endif
