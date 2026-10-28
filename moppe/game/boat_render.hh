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
                    float time = 0.0f,
                    std::optional<Outfit> passenger = std::nullopt);

  // Where the boat's stern passed and how fast, kept a few seconds so
  // that its wake can be drawn spreading behind it on the water.
  struct WakeMark {
    Vec3 stern;  // on the water, under the transom
    Vec3 across; // level, to port
    float speed = 0.0f;
    float time = 0.0f;
    // How much foam there is here, 0..1, so that it lies in patches.
    float foam = 1.0f;
  };

  class BoatWake {
  public:
    static constexpr float lasts = 7.0f;
    // Marks the stern at `time` if it has moved on and is under way in
    // water `depth` deep.
    void
    follow (const BoatPose& boat, float water_level, float depth, float time);
    void clear () {
      m_marks.clear ();
    }
    // The two arms of the wake spreading at the Kelvin angle and the
    // churned water between them behind the propeller, fading with age;
    // `light` dims the foam with the day. `level` is the water's surface
    // at a place now, which the wake lies on as the waves pass under it.
    void draw (render::DrawList& dl,
               float time,
               float light,
               const std::function<float (const Vec3&)>& level) const;

  private:
    std::deque<WakeMark> m_marks;
  };
}

#endif
