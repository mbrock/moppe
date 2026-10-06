#ifndef MOPPE_GAME_OPENING_HH
#define MOPPE_GAME_OPENING_HH

#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace moppe::game {
  // The opening is authored, not planned: a few held views of one known
  // world, each drifting a little, joined by cuts or a short dip through
  // black, with the title and a credit fading over them, and ending in the
  // player's own eyes beside the parked bike.
  //
  // A shot list is a small text file (data/opening.txt).  `P` in play
  // prints the current camera as a `shot` line that can be pasted into it.
  // Lines, `#` starting a comment:
  //
  //   world seed 123 resolution 2048 profile play
  //   shot NAME eye X Y Z heading DEG pitch DEG fov DEG [hold S]
  //        [push M] [truck M] [rise M] [pan DEG] [tilt DEG] [zoom DEG]
  //        [sway K] [clock S] [fade S] [sun H]
  //   caption title|credit "TEXT" [at S] [for S] [y FRACTION]
  //   arrival [hold S] [push M] [rise M] [fade S] [sway K]
  //
  // heading is the compass bearing of the view in degrees, atan2(x, z) of
  // the forward vector; pitch is up from the horizon.  push, truck, and
  // rise move the camera that many metres forward, right, and up over the
  // shot; pan, tilt, and zoom turn and narrow it by degrees.  sway scales
  // the gentle breathing drift of a held camera (1 by default, 0 locked
  // off).  clock sets world time (and so clouds and wind) at the cut; fade
  // dips through black for that many seconds into the shot instead of a
  // hard cut (on the first shot it is the fade up from black).  sun records
  // the sun height the pose was taken under; the opening does not relight.
  // A caption belongs to the shot before it.  arrival ends the opening in
  // the player's first-person view, approached from `push` metres behind
  // and `rise` metres above.

  struct OpeningCaption {
    enum class Style { Title, Credit };
    Style style = Style::Title;
    std::string text;
    // Seconds after the shot begins that the caption starts fading in, and
    // how long it stays (fades included); a negative length runs it to
    // shortly before the shot ends.
    float at = 1.4f;
    float length = -1.0f;
    // Height of the line in the frame, 0 at the top and 1 at the bottom;
    // negative places it where its style usually sits.
    float y = -1.0f;
  };

  struct OpeningShot {
    std::string name;
    Vec3 eye {};
    float heading_deg = 0.0f;
    float pitch_deg = 0.0f;
    float fov_deg = 55.0f;
    float hold = 7.0f;
    float push = 0.0f;
    float truck = 0.0f;
    float rise = 0.0f;
    float pan_deg = 0.0f;
    float tilt_deg = 0.0f;
    float zoom_deg = 0.0f;
    float sway = 1.0f;
    float fade = 0.0f;
    std::optional<float> clock;
    std::optional<float> sun;
    std::vector<OpeningCaption> captions;
    // Settles into its final pose with an ease instead of drifting through
    // it; the arrival lands this way in the player's eyes.
    bool settle = false;
  };

  struct OpeningArrival {
    float hold = 6.0f;
    float push = 5.0f;
    float rise = 1.2f;
    float fade = 0.0f;
    float sway = 0.6f;
    std::vector<OpeningCaption> captions;
  };

  struct OpeningReel {
    // The world the shots were composed in; unset fields match any world.
    std::optional<int> seed;
    std::optional<int> resolution;
    std::string profile;
    std::vector<OpeningShot> shots;
    std::optional<OpeningArrival> arrival;

    bool matches (int world_seed,
                  int world_resolution,
                  std::string_view world_profile) const;
  };

  // Parses a shot list; on failure returns nothing and says why, with the
  // line number.
  std::optional<OpeningReel> parse_opening_reel (std::istream& input,
                                                 std::string& error);

  // A view's compass heading and pitch, in degrees, from its forward
  // vector, and back.
  float heading_degrees (const Vec3& forward);
  float pitch_degrees (const Vec3& forward);
  Vec3 forward_from (float heading_deg, float pitch_deg);

  // One pasteable `shot` line for a camera pose.
  std::string format_opening_shot (const std::string& name,
                                   const Vec3& eye,
                                   const Vec3& forward,
                                   float fov_deg,
                                   float clock,
                                   float sun);

  // The final shot of a reel, flying the last few metres into the
  // player's first-person view (eye and level heading) at its field of
  // view.
  OpeningShot arrival_shot (const OpeningArrival& arrival,
                            const Vec3& eye,
                            const Vec3& heading,
                            float fov_deg);

  struct OpeningCaptionView {
    OpeningCaption::Style style;
    const std::string* text;
    float alpha;
    float y;
  };

  // Plays a reel: the camera of the current shot, the black veil of a
  // fade, and the captions showing.
  class OpeningPlayer {
  public:
    void start (std::vector<OpeningShot> shots);
    void stop () noexcept;
    void tick (float dt);

    bool active () const noexcept {
      return m_active;
    }
    float elapsed () const noexcept {
      return m_elapsed;
    }
    float duration () const noexcept {
      return m_duration;
    }
    // Index of the shot on screen; it changes at each cut.
    std::size_t shot_index () const noexcept {
      return m_shot;
    }
    const OpeningShot& shot () const {
      return m_shots[m_shot];
    }
    // Seconds into the current shot.
    float shot_time () const noexcept {
      return m_shot_time;
    }
    // True for the first tick of every shot, when world time may jump.
    bool cut () const noexcept {
      return m_cut;
    }

    const Vec3& position () const noexcept {
      return m_position;
    }
    const Vec3& forward () const noexcept {
      return m_forward;
    }
    float field_of_view () const noexcept {
      return m_fov;
    }
    Mat4 view_matrix () const;
    // Opacity of the black veil over the frame, for fades.
    float veil () const noexcept {
      return m_veil;
    }
    std::vector<OpeningCaptionView> captions () const;

  private:
    void pose ();

    std::vector<OpeningShot> m_shots;
    std::size_t m_shot = 0;
    float m_shot_time = 0.0f;
    float m_elapsed = 0.0f;
    float m_duration = 0.0f;
    bool m_active = false;
    bool m_cut = false;
    Vec3 m_position {};
    Vec3 m_forward { 0, 0, 1 };
    float m_roll = 0.0f;
    float m_fov = 55.0f;
    float m_veil = 0.0f;
  };
}

#endif
