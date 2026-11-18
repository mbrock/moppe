// G, for whoever is lost or stuck: back to the road by the nearest bus
// stop, with the machine they drive. The view does not cut there. It
// rises out of play's view, looking down on where they were, flies over
// high enough to clear the ground between, and comes down into play's
// view again where they have been set down, which it hands back as it
// ends.
#ifndef MOPPE_GAME_WAY_BACK_HH
#define MOPPE_GAME_WAY_BACK_HH

#include <moppe/game/frame_view.hh>
#include <moppe/gfx/math.hh>

#include <cstddef>
#include <optional>

namespace moppe::game {
  class BusLine;
  class GameSession;
  struct WorldGround;

  // Where the way back sets one down by a stop, in the copy of the world
  // nearest where they were: a machine on the road a little before the
  // stop, on the left half, which the buses (all going one way, on the
  // right) leave free; someone on foot on the verge behind the stop's
  // sign, where its board shows when the next bus comes. Both face the
  // way the buses go, so the stop is ahead on the right.
  struct RoadsidePlace {
    std::size_t stop = 0;
    position_t machine {};
    position_t feet {};
    direction_t heading = unit_direction (Vec3 (0, 0, 1));
  };

  // The place by the stop nearest `from`; none when the world has no bus.
  [[nodiscard]] std::optional<RoadsidePlace> roadside_near (
    const BusLine& line, const WorldGround& ground, const position_t& from);

  // Whether the way back takes the player as they are: not riding the bus
  // or along in the other player's machine, nor lying in the hammock.
  [[nodiscard]] bool can_take_way_back (const GameSession& session);

  // Whether they arrive on foot: walking, gliding without the bike, or
  // driving something afloat, which stays where it floats.
  [[nodiscard]] bool arrives_on_foot (const GameSession& session);

  // Moves the player to `place`, and the machine they drive with them,
  // stopped and out of trouble; a rope or a slung load is let go and the
  // fishing line comes in.
  void set_down (GameSession& session, const RoadsidePlace& place);

  class WayBack {
  public:
    // Begins the flight from play's view `from`, which looks at `subject`,
    // to `place`, where the player arrives at `to`.
    void start (const FrameCameraReading& from,
                const position_t& subject,
                const position_t& to,
                const RoadsidePlace& place,
                const WorldGround& ground);
    void stop () noexcept {
      m_active = false;
    }
    // Moves the flight on by `dt`. True on the one step it passes over
    // the top, where the player is to be set down.
    bool tick (seconds_t dt);

    bool active () const noexcept {
      return m_active;
    }
    const RoadsidePlace& place () const noexcept {
      return m_place;
    }
    // The view, given play's own just now: after the top the flight
    // comes down into it.
    FrameCameraReading camera (const FrameCameraReading& play,
                               const WorldGround& ground) const;
    proportion_t motion_blur () const;

  private:
    // How far through the flight, 0 to 1.
    proportion_t progress () const;

    FrameCameraReading m_from {};
    position_t m_subject {};
    position_t m_to {};
    // The top of the flight, over the middle of the way, and the level
    // way across.
    position_t m_top {};
    displacement_t m_across {};
    RoadsidePlace m_place {};
    seconds_t m_elapsed {};
    seconds_t m_length {};
    bool m_arrived = false;
    bool m_active = false;
  };
}

#endif
