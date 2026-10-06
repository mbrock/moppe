#include <moppe/game/opening.hh>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <istream>
#include <sstream>

namespace moppe::game {
  namespace {
    constexpr float opening_degree = PI / 180.0f;

    float unit_clamp (float t) {
      return std::clamp (t, 0.0f, 1.0f);
    }

    float opening_ease (float t) {
      t = unit_clamp (t);
      return t * t * (3.0f - 2.0f * t);
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
          shot.eye = Vec3 (x, y, z);
          has_eye = true;
          continue;
        }
        if (!in.number (value))
          return false;
        if (key == "heading")
          shot.heading_deg = value;
        else if (key == "pitch")
          shot.pitch_deg = value;
        else if (key == "fov")
          shot.fov_deg = value;
        else if (key == "hold")
          shot.hold = value;
        else if (key == "push")
          shot.push = value;
        else if (key == "truck")
          shot.truck = value;
        else if (key == "rise")
          shot.rise = value;
        else if (key == "pan")
          shot.pan_deg = value;
        else if (key == "tilt")
          shot.tilt_deg = value;
        else if (key == "zoom")
          shot.zoom_deg = value;
        else if (key == "sway")
          shot.sway = value;
        else if (key == "fade")
          shot.fade = value;
        else if (key == "clock")
          shot.clock = value;
        else if (key == "sun")
          shot.sun = value;
        else {
          error = "unknown shot setting: " + key;
          return false;
        }
      }
      if (!has_eye) {
        error = "shot " + shot.name + " has no eye";
        return false;
      }
      if (!(shot.hold > 0.0f) || !(shot.fov_deg > 1.0f) ||
          !(shot.fov_deg < 170.0f) || shot.fade < 0.0f) {
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
          arrival.hold = value;
        else if (key == "push")
          arrival.push = value;
        else if (key == "rise")
          arrival.rise = value;
        else if (key == "fade")
          arrival.fade = value;
        else if (key == "sway")
          arrival.sway = value;
        else {
          error = "unknown arrival setting: " + key;
          return false;
        }
      }
      if (!(arrival.hold > 0.0f)) {
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
          caption.at = value;
        else if (key == "for")
          caption.length = value;
        else if (key == "y")
          caption.y = value;
        else {
          error = "unknown caption setting: " + key;
          return false;
        }
      }
      return true;
    }

    // A deterministic phase per shot, so each held camera breathes
    // differently but identically on every launch.
    float phase_of (const std::string& name, int salt) {
      std::uint32_t hash = 2166136261u ^ static_cast<std::uint32_t> (salt);
      for (const char c : name) {
        hash ^= static_cast<unsigned char> (c);
        hash *= 16777619u;
      }
      return static_cast<float> (hash % 6283u) * 0.001f;
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

  float heading_degrees (const Vec3& forward) {
    return std::atan2 (forward[0], forward[2]) / opening_degree;
  }

  float pitch_degrees (const Vec3& forward) {
    const float horizontal =
      std::sqrt (forward[0] * forward[0] + forward[2] * forward[2]);
    return std::atan2 (forward[1], horizontal) / opening_degree;
  }

  Vec3 forward_from (float heading_deg, float pitch_deg) {
    const float h = heading_deg * opening_degree;
    const float p = pitch_deg * opening_degree;
    return Vec3 (
      std::sin (h) * std::cos (p), std::sin (p), std::cos (h) * std::cos (p));
  }

  std::string format_opening_shot (const std::string& name,
                                   const Vec3& eye,
                                   const Vec3& forward,
                                   float fov_deg,
                                   float clock,
                                   float sun) {
    char line[320];
    std::snprintf (line,
                   sizeof line,
                   "shot %s eye %.2f %.2f %.2f heading %.1f pitch %.1f "
                   "fov %.1f clock %.0f sun %.2f",
                   name.c_str (),
                   static_cast<float> (eye[0]),
                   static_cast<float> (eye[1]),
                   static_cast<float> (eye[2]),
                   heading_degrees (forward),
                   pitch_degrees (forward),
                   fov_deg,
                   clock,
                   sun);
    return line;
  }

  OpeningShot arrival_shot (const OpeningArrival& arrival,
                            const Vec3& eye,
                            const Vec3& heading,
                            float fov_deg) {
    OpeningShot shot;
    shot.name = "arrival";
    shot.heading_deg = heading_degrees (heading);
    shot.pitch_deg = 0.0f;
    shot.fov_deg = fov_deg;
    shot.hold = arrival.hold;
    const Vec3 forward = forward_from (shot.heading_deg, 0.0f);
    shot.eye = eye - forward * arrival.push + Vec3 (0, arrival.rise, 0);
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
    m_shot_time = 0.0f;
    m_elapsed = 0.0f;
    m_duration = 0.0f;
    for (const OpeningShot& shot : m_shots)
      m_duration += shot.hold;
    m_cut = true;
    if (m_active)
      pose ();
  }

  void OpeningPlayer::stop () noexcept {
    m_active = false;
    m_veil = 0.0f;
  }

  void OpeningPlayer::tick (float dt) {
    if (!m_active)
      return;
    m_cut = false;
    m_elapsed += dt;
    m_shot_time += dt;
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
    const float t = m_shot_time;
    const float progress = unit_clamp (t / shot.hold);
    // Held shots drift through their whole hold at an even pace, already in
    // motion at the cut; a settling shot eases into its final pose.
    const float travel =
      shot.settle ? 1.0f - (1.0f - progress) * (1.0f - progress) : progress;
    const float sway =
      shot.sway * (shot.settle ? (1.0f - progress) * (1.0f - progress) : 1.0f);

    const Vec3 up (0, 1, 0);
    const Vec3 base = forward_from (shot.heading_deg, shot.pitch_deg);
    Vec3 right = cross (base, up);
    right = length2 (right) > 1e-6f ? normalized (right) : Vec3 (1, 0, 0);

    const auto wave = [&] (int salt, float hz, float amplitude) {
      return amplitude *
             std::sin (2.0f * PI * hz * t + phase_of (shot.name, salt));
    };
    // A camera on a tripod in a light breeze: slow, incommensurate swells
    // of a fraction of a opening_degree, never a shake.
    const float yaw =
      sway * (wave (1, 0.071f, 0.30f) + wave (2, 0.163f, 0.11f));
    const float pitch =
      sway * (wave (3, 0.089f, 0.17f) + wave (4, 0.197f, 0.06f));
    m_roll = sway * wave (5, 0.053f, 0.12f) * opening_degree;

    m_position = shot.eye + base * (shot.push * travel) +
                 right * (shot.truck * travel) + up * (shot.rise * travel) +
                 up * (sway * wave (6, 0.061f, 0.035f)) +
                 right * (sway * wave (7, 0.043f, 0.05f));
    m_forward = forward_from (shot.heading_deg + shot.pan_deg * travel + yaw,
                              shot.pitch_deg + shot.tilt_deg * travel + pitch);
    m_fov = shot.fov_deg - shot.zoom_deg * travel;

    // Fades: the first shot rises out of black over its whole fade; later
    // ones dip through black, half the fade on either side of the cut.
    float veil = 0.0f;
    if (shot.fade > 0.0f) {
      const float span = m_shot == 0 ? shot.fade : 0.5f * shot.fade;
      veil = std::max (veil, 1.0f - opening_ease (t / span));
    }
    if (m_shot + 1 < m_shots.size ()) {
      const float next = m_shots[m_shot + 1].fade;
      if (next > 0.0f)
        veil = std::max (veil,
                         1.0f - opening_ease ((shot.hold - t) / (0.5f * next)));
    }
    m_veil = veil;
  }

  Mat4 OpeningPlayer::view_matrix () const {
    Vec3 right = cross (m_forward, Vec3 (0, 1, 0));
    right = length2 (right) > 1e-6f ? normalized (right) : Vec3 (1, 0, 0);
    Vec3 up = normalized (cross (right, m_forward));
    up = normalized (up * std::cos (m_roll) + right * std::sin (m_roll));
    return Mat4::look_at (m_position, m_position + m_forward * 100.0f, up);
  }

  std::vector<OpeningCaptionView> OpeningPlayer::captions () const {
    std::vector<OpeningCaptionView> showing;
    if (!m_active && m_shots.empty ())
      return showing;
    const OpeningShot& shot = m_shots[m_shot];
    for (const OpeningCaption& caption : shot.captions) {
      const float length =
        caption.length > 0.0f ? caption.length : shot.hold - caption.at - 1.2f;
      if (length <= 0.0f)
        continue;
      const float ramp = std::min (1.8f, 0.4f * length);
      const float t = m_shot_time - caption.at;
      const float alpha =
        opening_ease (t / ramp) * opening_ease ((length - t) / ramp);
      if (alpha > 0.002f)
        showing.push_back ({ caption.style, &caption.text, alpha, caption.y });
    }
    return showing;
  }
}
