#ifndef MOPPE_GAME_BOAT_RENDER_HH
#define MOPPE_GAME_BOAT_RENDER_HH

#include <moppe/color.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <deque>
#include <optional>

namespace moppe::game {
  // The boat (models/boat.blend) as baked meshes drawn through the
  // renderer: the hull, the long-tail swung and tipped about its pivot,
  // and the propeller turning on the end of its shaft. A helmsman, given
  // their outfit, sits on the after seat with a hand on the tiller,
  // skinned into the frame's draw list.
  void render_boat (render::Renderer& r,
                    render::DrawList& dl,
                    const BoatPose& boat,
                    std::optional<Outfit> helmsman,
                    uint64_t motion_base);

  // Where the boat's stern passed and how fast, kept a few seconds so
  // that its wake can be drawn spreading behind it on the water.
  struct WakeMark {
    Vec3 stern;     // on the water, under the transom
    Vec3 across;    // level, to port
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
    void follow (const BoatPose& boat,
                 float water_level,
                 float depth,
                 float time);
    void clear () {
      m_marks.clear ();
    }
    // The two arms of the wake spreading at the Kelvin angle and the
    // churned water between them behind the propeller, fading with age;
    // `light` dims the foam with the day.
    void draw (render::DrawList& dl, float time, float light) const;

  private:
    std::deque<WakeMark> m_marks;
  };
}

#endif
