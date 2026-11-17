#include <moppe/game/opening.hh>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <istream>
#include <sstream>

namespace moppe::game {
  namespace {
    // One degree, in radians.
    constexpr auto opening_degree = PI / 180.0f * u::rad / u::deg;

    // How far through a span, held between its ends.
    proportion_t unit_clamp (magnitude_t t) {
      return std::clamp (scalar_value (t), 0.0f, 1.0f) * proportion[one];
    }

    proportion_t opening_ease (magnitude_t span) {
      const proportion_t t = unit_clamp (span);
      return quantity_cast<proportion> (
        t * t * (3.0f - 2.0f * t.numerical_value_in (one)));
    }

    // Splits a line into words, keeping a double-quoted string whole and
    // dropping everything after an unquoted `#`.
    bool split_words (const std::string& line,
                      std::vector<std::string>& words,
                      std::string& error) {
      words.clear ();
      std::size_t i = 0;
      while (i < line.size ()) {
        const char c = line[i];
        if (c == '#')
          break;
        if (std::isspace (static_cast<unsigned char> (c))) {
          ++i;
          continue;
        }
        if (c == '"') {
          const std::size_t close = line.find ('"', i + 1);
          if (close == std::string::npos) {
            error = "unterminated quotation";
            return false;
          }
          words.push_back (line.substr (i + 1, close - i - 1));
          i = close + 1;
          continue;
        }
        std::size_t end = i;
        while (end < line.size () &&
               !std::isspace (static_cast<unsigned char> (line[end])) &&
               line[end] != '#')
          ++end;
        words.push_back (line.substr (i, end - i));
        i = end;
      }
      return true;
    }

    // Reads key/value words from `words[first..]`, one number per key
    // (three for eye), handing each to `accept`.
    class Words {
    public:
      Words (const std::vector<std::string>& words,
             std::size_t first,
             std::string& error)
          : m_words (words), m_at (first), m_error (error) {}

      bool done () const {
        return m_at >= m_words.size ();
      }
      const std::string& key () {
        return m_words[m_at++];
      }
      // Text holds plain numbers; each key types its own.
      bool number (float& value) {
        if (done ()) {
          m_error = "missing number after " + m_words[m_at - 1];
          return false;
        }
        const std::string& word = m_words[m_at++];
        char* end = nullptr;
        value = std::strtof (word.c_str (), &end);
        if (word.empty () || *end != '\0' || !std::isfinite (value)) {
          m_error = "not a number: " + word;
          return false;
        }
        return true;
      }
      bool text (std::string& value) {
        if (done ()) {
          m_error = "missing text";
          return false;
        }
        value = m_words[m_at++];
        return true;
      }

    private:
      const std::vector<std::string>& m_words;
      std::size_t m_at;
      std::string& m_error;
    };

    bool parse_shot (const std::vector<std::string>& words,
                     OpeningShot& shot,
                     std::string& error) {
      if (words.size () < 2) {
        error = "shot needs a name";
        return false;
      }
      shot.name = words[1];
      Words in (words, 2, error);
      bool has_eye = false;
      while (!in.done ()) {
        const std::string& key = in.key ();
        float value = 0.0f;
        if (key == "eye") {
          float x, y, z;
          if (!in.number (x) || !in.number (y) || !in.number (z))
            return false;
          shot.eye = position (Vec3 (x, y, z));
          has_eye = true;
          continue;
        }
        if (!in.number (value))
          return false;
        if (key == "heading")
          shot.heading_deg = value * u::deg;
        else if (key == "pitch")
          shot.pitch_deg = value * u::deg;
        else if (key == "fov")
          shot.fov_deg = value * u::deg;
        else if (key == "hold")
          shot.hold = value * u::s;
        else if (key == "push")
          shot.push = value * u::m;
        else if (key == "truck")
          shot.truck = value * u::m;
        else if (key == "rise")
          shot.rise = value * u::m;
        else if (key == "pan")
          shot.pan_deg = value * u::deg;
        else if (key == "tilt")
          shot.tilt_deg = value * u::deg;
        else if (key == "zoom")
          shot.zoom_deg = value * u::deg;
        else if (key == "sway")
          shot.sway = value * scale_factor[one];
        else if (key == "fade")
          shot.fade = value * u::s;
        else if (key == "clock")
          shot.clock = value * u::s;
        else if (key == "sun")
          shot.sun = value * proportion[one];
        else {
          error = "unknown shot setting: " + key;
          return false;
        }
      }
      if (!has_eye) {
        error = "shot " + shot.name + " has no eye";
        return false;
      }
      if (!(shot.hold > 0.0f * u::s) || !(shot.fov_deg > 1.0f * u::deg) ||
          !(shot.fov_deg < 170.0f * u::deg) || shot.fade < 0.0f * u::s) {
        error = "shot " + shot.name + " has an impossible hold, fov, or fade";
        return false;
      }
      return true;
    }

    bool parse_arrival (const std::vector<std::string>& words,
                        OpeningArrival& arrival,
                        std::string& error) {
      Words in (words, 1, error);
      while (!in.done ()) {
        const std::string& key = in.key ();
        float value = 0.0f;
        if (!in.number (value))
          return false;
        if (key == "hold")
          arrival.hold = value * u::s;
        else if (key == "push")
          arrival.push = value * u::m;
        else if (key == "rise")
          arrival.rise = value * u::m;
        else if (key == "fade")
          arrival.fade = value * u::s;
        else if (key == "sway")
          arrival.sway = value * scale_factor[one];
        else {
          error = "unknown arrival setting: " + key;
          return false;
        }
      }
      if (!(arrival.hold > 0.0f * u::s)) {
        error = "arrival needs a positive hold";
        return false;
      }
      return true;
    }

    bool parse_caption (const std::vector<std::string>& words,
                        OpeningCaption& caption,
                        std::string& error) {
      if (words.size () < 3) {
        error = "caption wants a style and a quoted text";
        return false;
      }
      if (words[1] == "title")
        caption.style = OpeningCaption::Style::Title;
      else if (words[1] == "credit")
        caption.style = OpeningCaption::Style::Credit;
      else {
        error = "caption style is title or credit, not " + words[1];
        return false;
      }
      caption.text = words[2];
      Words in (words, 3, error);
      while (!in.done ()) {
        const std::string& key = in.key ();
        float value = 0.0f;
        if (!in.number (value))
          return false;
        if (key == "at")
          caption.at = value * u::s;
        else if (key == "for")
          caption.length = value * u::s;
        else if (key == "y")
          caption.y = value * proportion[one];
        else {
          error = "unknown caption setting: " + key;
          return false;
        }
      }
      return true;
    }

    // A deterministic phase per shot, so each held camera breathes
    // differently but identically on every launch.
    radians_t phase_of (const std::string& name, int salt) {
      std::uint32_t hash = 2166136261u ^ static_cast<std::uint32_t> (salt);
      for (const char c : name) {
        hash ^= static_cast<unsigned char> (c);
        hash *= 16777619u;
      }
      return static_cast<float> (hash % 6283u) * 0.001f * u::rad;
    }
  }

  bool OpeningReel::matches (int world_seed,
                             int world_resolution,
                             std::string_view world_profile) const {
    return (!seed || *seed == world_seed) &&
           (!resolution || *resolution == world_resolution) &&
           (profile.empty () || profile == world_profile);
  }

  std::optional<OpeningReel> parse_opening_reel (std::istream& input,
                                                 std::string& error) {
    OpeningReel reel;
    std::string line;
    std::vector<std::string> words;
    int number = 0;
    const auto fail = [&] (const std::string& why) {
      error = "line " + std::to_string (number) + ": " + why;
      return std::nullopt;
    };
    while (std::getline (input, line)) {
      ++number;
      std::string why;
      if (!split_words (line, words, why))
        return fail (why);
      if (words.empty ())
        continue;
      const std::string& head = words[0];
      if (head == "world") {
        Words in (words, 1, why);
        while (!in.done ()) {
          const std::string& key = in.key ();
          if (key == "profile") {
            if (!in.text (reel.profile))
              return fail (why);
            continue;
          }
          float value = 0.0f;
          if (!in.number (value))
            return fail (why);
          if (key == "seed")
            reel.seed = static_cast<int> (value);
          else if (key == "resolution")
            reel.resolution = static_cast<int> (value);
          else if (key == "km")
            reel.km = value * u::km;
          else
            return fail ("unknown world setting: " + key);
        }
      } else if (head == "shot") {
        if (reel.arrival)
          return fail ("shots after the arrival would never be seen");
        OpeningShot shot;
        if (!parse_shot (words, shot, why))
          return fail (why);
        reel.shots.push_back (std::move (shot));
      } else if (head == "caption") {
        OpeningCaption caption;
        if (!parse_caption (words, caption, why))
          return fail (why);
        if (reel.arrival)
          reel.arrival->captions.push_back (std::move (caption));
        else if (!reel.shots.empty ())
          reel.shots.back ().captions.push_back (std::move (caption));
        else
          return fail ("a caption belongs to the shot before it");
      } else if (head == "arrival") {
        OpeningArrival arrival;
        if (!parse_arrival (words, arrival, why))
          return fail (why);
        reel.arrival = arrival;
      } else {
        return fail ("unknown line: " + head);
      }
    }
    return reel;
  }

  degrees_t heading_degrees (const Vec3& forward) {
    return std::atan2 (forward[0], forward[2]) * u::rad / opening_degree;
  }

  degrees_t pitch_degrees (const Vec3& forward) {
    const float horizontal =
      std::sqrt (forward[0] * forward[0] + forward[2] * forward[2]);
    return std::atan2 (forward[1], horizontal) * u::rad / opening_degree;
  }

  direction_t forward_from (degrees_t heading, degrees_t pitch) {
    const float h = radians_value (heading * opening_degree);
    const float p = radians_value (pitch * opening_degree);
    return unit_direction (Vec3 (
      std::sin (h) * std::cos (p), std::sin (p), std::cos (h) * std::cos (p)));
  }

  std::string format_opening_shot (const std::string& name,
                                   const Vec3& eye,
                                   const Vec3& forward,
                                   degrees_t fov,
                                   seconds_t clock,
                                   proportion_t sun) {
    char line[320];
    std::snprintf (line,
                   sizeof line,
                   "shot %s eye %.2f %.2f %.2f heading %.1f pitch %.1f "
                   "fov %.1f clock %.0f sun %.2f",
                   name.c_str (),
                   static_cast<float> (eye[0]),
                   static_cast<float> (eye[1]),
                   static_cast<float> (eye[2]),
                   heading_degrees (forward).numerical_value_in (u::deg),
                   pitch_degrees (forward).numerical_value_in (u::deg),
                   fov.numerical_value_in (u::deg),
                   seconds_value (clock),
                   sun.numerical_value_in (one));
    return line;
  }

  OpeningShot arrival_shot (const OpeningArrival& arrival,
                            const position_t& eye,
                            const Vec3& forward,
                            degrees_t fov) {
    OpeningShot shot;
    shot.name = "arrival";
    shot.heading_deg = heading_degrees (forward);
    shot.pitch_deg = pitch_degrees (forward);
    shot.fov_deg = fov;
    shot.hold = arrival.hold;
    // The push runs along the view, as OpeningPlayer::pose moves it.
    const direction_t way = forward_from (shot.heading_deg, shot.pitch_deg);
    shot.eye = position (position_value (eye) -
                         direction_value (way) * meters_value (arrival.push) +
                         Vec3 (0, meters_value (arrival.rise), 0));
    shot.push = arrival.push;
    shot.rise = -arrival.rise;
    shot.sway = arrival.sway;
    shot.fade = arrival.fade;
    shot.captions = arrival.captions;
    shot.settle = true;
    return shot;
  }

  void OpeningPlayer::start (std::vector<OpeningShot> shots) {
    m_shots = std::move (shots);
    m_active = !m_shots.empty ();
    m_shot = 0;
    m_shot_time = 0.0f * u::s;
    m_elapsed = 0.0f * u::s;
    m_duration = 0.0f * u::s;
    for (const OpeningShot& shot : m_shots)
      m_duration += shot.hold;
    m_cut = true;
    if (m_active)
      pose ();
  }

  void OpeningPlayer::stop () noexcept {
    m_active = false;
    m_veil = 0.0f * proportion[one];
  }

  void OpeningPlayer::tick (seconds_t interval) {
    if (!m_active)
      return;
    m_cut = false;
    m_elapsed += interval;
    m_shot_time += interval;
    while (m_shot_time >= m_shots[m_shot].hold) {
      m_shot_time -= m_shots[m_shot].hold;
      if (++m_shot >= m_shots.size ()) {
        // Hold the last pose: the game takes the camera from here.
        m_shot = m_shots.size () - 1;
        m_shot_time = m_shots[m_shot].hold;
        pose ();
        stop ();
        return;
      }
      m_cut = true;
    }
    pose ();
  }

  void OpeningPlayer::pose () {
    const OpeningShot& shot = m_shots[m_shot];
    const seconds_t t = m_shot_time;
    const proportion_t progress = unit_clamp (t / shot.hold);
    // Held shots drift through their whole hold at an even pace, already in
    // motion at the cut; a settling shot eases into its final pose.
    constexpr proportion_t whole = 1.0f * proportion[one];
    const proportion_t left = whole - progress;
    const proportion_t travel =
      shot.settle ? whole - quantity_cast<proportion> (left * left) : progress;
    const scale_factor_t sway = quantity_cast<scale_factor> (
      shot.sway *
      (shot.settle ? quantity_cast<proportion> (left * left) : whole));

    const Vec3 up (0, 1, 0);
    const Vec3 base =
      direction_value (forward_from (shot.heading_deg, shot.pitch_deg));
    Vec3 right = cross (base, up);
    right = length2 (right) > 1e-6f ? normalized (right) : Vec3 (1, 0, 0);

    // Each swell's amplitude is in degrees or metres, as it moves.
    const auto wave = [&] (int salt, frequency_t hz, auto amplitude) {
      return amplitude *
             std::sin ((2.0f * PI * hz * t).numerical_value_in (one) +
                       radians_value (phase_of (shot.name, salt)));
    };
    // A camera on a tripod in a light breeze: slow, incommensurate swells
    // of a fraction of a opening_degree, never a shake.
    const degrees_t yaw = quantity_cast<isq::angular_measure> (
      sway * (wave (1, 0.071f * u::Hz, 0.30f * u::deg) +
              wave (2, 0.163f * u::Hz, 0.11f * u::deg)));
    const degrees_t pitch = quantity_cast<isq::angular_measure> (
      sway * (wave (3, 0.089f * u::Hz, 0.17f * u::deg) +
              wave (4, 0.197f * u::Hz, 0.06f * u::deg)));
    m_roll = quantity_cast<isq::angular_measure> (
      sway * wave (5, 0.053f * u::Hz, 0.12f * u::deg) * opening_degree);

    const meters_t lift = quantity_cast<isq::length> (
      sway * wave (6, 0.061f * u::Hz, 0.035f * u::m));
    const meters_t aside = quantity_cast<isq::length> (
      sway * wave (7, 0.043f * u::Hz, 0.05f * u::m));
    m_position =
      moppe::position (position_value (shot.eye) +
                       base * meters_value (meters_t (shot.push * travel)) +
                       right * meters_value (meters_t (shot.truck * travel)) +
                       up * meters_value (meters_t (shot.rise * travel)) +
                       up * meters_value (lift) + right * meters_value (aside));
    m_forward = forward_from (
      shot.heading_deg + degrees_t (shot.pan_deg * travel) + yaw,
      shot.pitch_deg + degrees_t (shot.tilt_deg * travel) + pitch);
    m_fov = shot.fov_deg - degrees_t (shot.zoom_deg * travel);

    // Fades: the first shot rises out of black over its whole fade; later
    // ones dip through black, half the fade on either side of the cut.
    proportion_t veil = 0.0f * proportion[one];
    if (shot.fade > 0.0f * u::s) {
      const seconds_t span = m_shot == 0 ? shot.fade : 0.5f * shot.fade;
      veil = std::max (veil, whole - opening_ease (t / span));
    }
    if (m_shot + 1 < m_shots.size ()) {
      const seconds_t next = m_shots[m_shot + 1].fade;
      if (next > 0.0f * u::s)
        veil = std::max (
          veil, whole - opening_ease ((shot.hold - t) / (0.5f * next)));
    }
    m_veil = veil;
  }

  Mat4 OpeningPlayer::view_matrix () const {
    const Vec3& forward = direction_value (m_forward);
    const Vec3& eye = position_value (m_position);
    const float roll = radians_value (m_roll);
    Vec3 right = cross (forward, Vec3 (0, 1, 0));
    right = length2 (right) > 1e-6f ? normalized (right) : Vec3 (1, 0, 0);
    Vec3 up = normalized (cross (right, forward));
    up = normalized (up * std::cos (roll) + right * std::sin (roll));
    return Mat4::look_at (eye, eye + forward * 100.0f, up);
  }

  std::vector<OpeningCaptionView> OpeningPlayer::captions () const {
    std::vector<OpeningCaptionView> showing;
    if (!m_active && m_shots.empty ())
      return showing;
    const OpeningShot& shot = m_shots[m_shot];
    for (const OpeningCaption& caption : shot.captions) {
      const seconds_t length = caption.length > 0.0f * u::s
                                 ? caption.length
                                 : shot.hold - caption.at - 1.2f * u::s;
      if (length <= 0.0f * u::s)
        continue;
      const seconds_t ramp = std::min (1.8f * u::s, 0.4f * length);
      const seconds_t t = m_shot_time - caption.at;
      const proportion_t alpha = quantity_cast<proportion> (
        opening_ease (t / ramp) * opening_ease ((length - t) / ramp));
      if (alpha > 0.002f * proportion[one])
        showing.push_back ({ caption.style, &caption.text, alpha, caption.y });
    }
    return showing;
  }
}
