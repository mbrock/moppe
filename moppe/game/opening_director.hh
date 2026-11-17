// The opening as a launch plays it (opening.hh): the authored shot list
// (data/opening.txt) when it was composed in this world, and otherwise one
// generated still of the trailhead; either ends in the player's eyes.
// MOPPE_OPENING names another shot list, `generated` for the still,
// `flight` for the old drone flight through the planned landmarks, which
// the terrain survey still samples, `river` for a flight down the greatest
// watercourse from its divide to the sea (at a watching pace, or in
// MOPPE_FLYOVER_SECONDS), or `none` to begin in play.
#ifndef MOPPE_GAME_OPENING_DIRECTOR_HH
#define MOPPE_GAME_OPENING_DIRECTOR_HH

#include <moppe/game/cinematic_flight.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/opening.hh>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace moppe::render {
  class DrawList;
  class TextList;
}

namespace moppe::game {
  class GameSession;
  class GeneratedWorld;
  class Hud;
  class SkyClock;
  struct SkyReading;
  struct WorldGround;

  class OpeningDirector {
  public:
    // Plans the opening of a world just activated, with the player
    // placed at its start.
    void plan (const GeneratedWorld& world,
               const WorldGround& ground,
               const SkyReading& sky,
               GameSession& session,
               const position_t& home_base,
               const position_t& spawn);

    void start (GameSession& session,
                const map::SurfaceGeometry& surface,
                SkyClock& sky);

    // Forgets the plan, for another world.
    void forget ();

    // Stops whatever plays.
    void stop () noexcept {
      m_flight.stop ();
      m_reel.stop ();
    }

    bool active () const noexcept {
      return m_flight.active () || m_reel.active ();
    }

    // One simulation step while the opening plays. `left` says it has
    // ended or been left, for play to take up the view (the input asking
    // to leave is spent); `played` that the step was the opening's.
    struct Step {
      bool left = false;
      bool played = false;
    };
    Step step (InputFrame& input,
               seconds_t interval,
               GameSession& session,
               const map::SurfaceGeometry& surface,
               SkyClock& sky);

    // The camera while the opening plays.
    std::optional<FrameCameraReading> camera () const;

    proportion_t motion_blur () const {
      return m_flight.active () ? m_flight.motion_blur ()
                                : 0.0f * proportion[one];
    }

    seconds_t elapsed () const {
      return m_reel.active ()     ? m_reel.elapsed ()
             : m_flight.active () ? m_flight.elapsed ()
                                  : 0.0f * u::s;
    }

    // Whether the shot on screen shows the player before it settles.
    bool reveal_player () const {
      return m_reel.active () && !m_reel.shot ().settle;
    }

    // Whether a cut has come since the last frame drawn: the temporal
    // history of the last view would only smear into the new one.
    bool take_cut ();

    // The reel's last shot follows a player who has moved since it was
    // planned.
    void reaim (GameSession& session, const map::SurfaceGeometry& surface);

    // The opening's fades are a black veil over the frame, and its
    // captions are set over that.
    void draw_titles (const Hud& hud,
                      render::DrawList& dl,
                      render::TextList& text,
                      int width_pts,
                      int height_pts) const;

  private:
    // Reads the authored shots, keeping them only if they were composed
    // in this world and every camera stands above its ground.
    static std::optional<OpeningReel> authored (const char* choice,
                                                const GeneratedWorld& world,
                                                const WorldGround& ground,
                                                const SkyReading& sky);

    // Any world's opening: one long still looking back at the trailhead
    // from up the trail, with the titles over it.
    static std::vector<OpeningShot> generated (const WorldGround& ground,
                                               const position_t& home_base,
                                               const position_t& spawn);

    void plan_reel (const char* choice,
                    const GeneratedWorld& world,
                    const WorldGround& ground,
                    const SkyReading& sky,
                    GameSession& session,
                    const position_t& home_base,
                    const position_t& spawn);

    // The opening's last shot lands where play takes up the view: at
    // the following camera's place behind the player, or in their eyes.
    OpeningShot arrival (GameSession& session,
                         const map::SurfaceGeometry& surface) const;

    // Each shot may set the world's clock at its cut, choosing its sky.
    void follow_clock (GameSession& session, SkyClock& sky);

    CinematicFlightPlan m_flight_plan;
    CinematicFlight m_flight;
    std::vector<OpeningShot> m_shots;
    OpeningArrival m_arrival;
    OpeningPlayer m_reel;
    std::size_t m_shot_seen = SIZE_MAX;
    std::size_t m_drawn_shot = SIZE_MAX;
  };
}

#endif
