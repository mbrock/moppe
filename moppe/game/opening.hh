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
  //   world seed 123 resolution 2048 profile play [km 10]
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
  // the view play begins with (from behind the walker, or through their
  // eyes), approached from `push` metres behind and `rise` metres above.

  using kilometres_t = quantity<si::kilo<si::metre>, float>;

  struct OpeningCaption {
    enum class Style { Title, Credit };
    Style style = Style::Title;
    std::string text;
    // Seconds after the shot begins that the caption starts fading in, and
    // how long it stays (fades included); a negative length runs it to
    // shortly before the shot ends.
    seconds_t at = 1.4f * u::s;
    seconds_t length = -1.0f * u::s;
    // Height of the line in the frame, 0 at the top and 1 at the bottom;
    // negative places it where its style usually sits.
    proportion_t y = -1.0f * proportion[one];
  };

  struct OpeningShot {
    std::string name;
    position_t eye = position (Vec3 ());
    degrees_t heading_deg = 0.0f * u::deg;
    degrees_t pitch_deg = 0.0f * u::deg;
    degrees_t fov_deg = 55.0f * u::deg;
    seconds_t hold = 7.0f * u::s;
    meters_t push = 0.0f * u::m;
    meters_t truck = 0.0f * u::m;
    meters_t rise = 0.0f * u::m;
    degrees_t pan_deg = 0.0f * u::deg;
    degrees_t tilt_deg = 0.0f * u::deg;
    degrees_t zoom_deg = 0.0f * u::deg;
    scale_factor_t sway = 1.0f * scale_factor[one];
    seconds_t fade = 0.0f * u::s;
    // Seconds into play at the cut.
    std::optional<seconds_t> clock;
    std::optional<proportion_t> sun;
    std::vector<OpeningCaption> captions;
    // Settles into its final pose with an ease instead of drifting through
    // it; the arrival lands this way in the player's eyes.
    bool settle = false;
  };

  struct OpeningArrival {
    seconds_t hold = 6.0f * u::s;
    meters_t push = 5.0f * u::m;
    meters_t rise = 1.2f * u::m;
    seconds_t fade = 0.0f * u::s;
    scale_factor_t sway = 0.6f * scale_factor[one];
    std::vector<OpeningCaption> captions;
  };

  struct OpeningReel {
    // The world the shots were composed in; unset fields match any world.
    std::optional<int> seed;
    std::optional<int> resolution;
    std::string profile;
    // How many kilometres across; unset, the default world's five.
    std::optional<kilometres_t> km;
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
  degrees_t heading_degrees (const Vec3& forward);
  degrees_t pitch_degrees (const Vec3& forward);
  direction_t forward_from (degrees_t heading, degrees_t pitch);

  // One pasteable `shot` line for a camera pose.
  std::string format_opening_shot (const std::string& name,
                                   const Vec3& eye,
                                   const Vec3& forward,
                                   degrees_t fov,
                                   seconds_t clock,
                                   proportion_t sun);

  // The final shot of a reel, flying the last few metres into the view
  // play begins with (eye and forward) at its field of view.
  OpeningShot arrival_shot (const OpeningArrival& arrival,
                            const position_t& eye,
                            const Vec3& forward,
                            degrees_t fov);

  struct OpeningCaptionView {
    OpeningCaption::Style style;
    const std::string* text;
    // How far the caption has faded in.
    proportion_t alpha;
    proportion_t y;
  };

  // Plays a reel: the camera of the current shot, the black veil of a
  // fade, and the captions showing.
  class OpeningPlayer {
  public:
    void start (std::vector<OpeningShot> shots);
    void stop () noexcept;
    void tick (seconds_t interval);

    bool active () const noexcept {
      return m_active;
    }
    // Aims the reel's last shot, the arrival, at a player who has moved
    // since it was planned; it lasts as long as it did.
    void replace_arrival (OpeningShot shot) {
      if (m_shots.empty ())
        return;
      shot.hold = m_shots.back ().hold;
      m_shots.back () = std::move (shot);
    }
    seconds_t elapsed () const noexcept {
      return m_elapsed;
    }
    seconds_t duration () const noexcept {
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
    seconds_t shot_time () const noexcept {
      return m_shot_time;
    }
    // True for the first tick of every shot, when world time may jump.
    bool cut () const noexcept {
      return m_cut;
    }

    const position_t& position () const noexcept {
      return m_position;
    }
    const direction_t& forward () const noexcept {
      return m_forward;
    }
    degrees_t field_of_view () const noexcept {
      return m_fov;
    }
    Mat4 view_matrix () const;
    // Opacity of the black veil over the frame, for fades.
    proportion_t veil () const noexcept {
      return m_veil;
    }
    std::vector<OpeningCaptionView> captions () const;

  private:
    void pose ();

    std::vector<OpeningShot> m_shots;
    std::size_t m_shot = 0;
    seconds_t m_shot_time = 0.0f * u::s;
    seconds_t m_elapsed = 0.0f * u::s;
    seconds_t m_duration = 0.0f * u::s;
    bool m_active = false;
    bool m_cut = false;
    position_t m_position = moppe::position (Vec3 ());
    direction_t m_forward = unit_direction (Vec3 (0, 0, 1));
    radians_t m_roll = 0.0f * u::rad;
    degrees_t m_fov = 55.0f * u::deg;
    proportion_t m_veil = 0.0f * proportion[one];
  };
}

#endif
