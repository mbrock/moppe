#ifndef MOPPE_GAME_GAME_SESSION_HH
#define MOPPE_GAME_GAME_SESSION_HH

#include <moppe/game/fleet.hh>
#include <moppe/game/game_state.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>

#include <optional>
#include <span>

namespace moppe::game {
  // Observable application-side effects of an ordinary simulation step.
  // The application decides how to realize these, keeping platform services
  // outside the simulation seam.
  struct GameSessionAdvanceResult {
    bool say_ouchies = false;
  };

  struct Surroundings;

  // How the view follows whatever the player is doing, the same for every
  // way of getting about: the eyes and the way ahead they look along, and
  // for the following cameras the point on the subject they swing about,
  // how it moves, and how far behind and how high above it they stand.
  struct CameraRig {
    position_t eye = position (Vec3 ());
    direction_t ahead = unit_direction (Vec3 (0, 0, 1));
    position_t pivot = position (Vec3 ());
    velocity_t velocity = moppe::velocity (Vec3 ());
    meters_t distance = 6.5f * u::m;
    // As near as the ground may pull the camera in.
    meters_t nearest = 2.6f * u::m;
    degrees_t elevation = 18.0f * u::deg;
  };

  // How fast the subject goes, as the speedometer reads it.
  using kilometres_per_hour_t = quantity<si::kilo<si::metre> / si::hour, float>;

  // The mutable state of one playable session on a completed world.  The
  // world retains the surface; the bike and the glider borrow it for their
  // physical readings. A checkpoint is therefore meaningful only on the same
  // completed world.
  class GameSession {
  public:
    using State = GameState;

    GameSession (const WorldParams& world, const map::SurfaceGeometry& surface);
    GameSession (const GameSession&) = delete;
    GameSession& operator= (const GameSession&) = delete;
    GameSession (GameSession&&) = delete;
    GameSession& operator= (GameSession&&) = delete;

    GameLogicState& logic () noexcept {
      return m_logic;
    }
    const GameLogicState& logic () const noexcept {
      return m_logic;
    }

    // Every machine this player has (game/fleet.hh).
    Fleet& fleet () noexcept {
      return m_fleet;
    }
    const Fleet& fleet () const noexcept {
      return m_fleet;
    }

    mov::Vehicle& bike () noexcept {
      return m_fleet.bike;
    }
    const mov::Vehicle& bike () const noexcept {
      return m_fleet.bike;
    }

    mov::Vehicle& mountain_bike () noexcept {
      return m_fleet.mountain_bike;
    }
    const mov::Vehicle& mountain_bike () const noexcept {
      return m_fleet.mountain_bike;
    }
    mov::Vehicle& long_john () noexcept {
      return m_fleet.long_john;
    }
    const mov::Vehicle& long_john () const noexcept {
      return m_fleet.long_john;
    }
    // The bike being ridden, the motocross, the mountainbike, or the Long
    // John, or the motocross when none is.
    mov::Vehicle& ridden_bike () noexcept {
      return m_logic.m_doing == Doing::driving (Machine::mountain_bike)
               ? m_fleet.mountain_bike
             : m_logic.m_doing == Doing::driving (Machine::long_john)
               ? m_fleet.long_john
               : m_fleet.bike;
    }
    const mov::Vehicle& ridden_bike () const noexcept {
      return m_logic.m_doing == Doing::driving (Machine::mountain_bike)
               ? m_fleet.mountain_bike
             : m_logic.m_doing == Doing::driving (Machine::long_john)
               ? m_fleet.long_john
               : m_fleet.bike;
    }

    mov::Car& jeep () noexcept {
      return m_fleet.jeep;
    }
    const mov::Car& jeep () const noexcept {
      return m_fleet.jeep;
    }

    mov::Helicopter& helicopter () noexcept {
      return m_fleet.helicopter;
    }
    const mov::Helicopter& helicopter () const noexcept {
      return m_fleet.helicopter;
    }

    mov::Tractor& tractor () noexcept {
      return m_fleet.tractor;
    }
    const mov::Tractor& tractor () const noexcept {
      return m_fleet.tractor;
    }

    mov::Car& caddy () noexcept {
      return m_fleet.caddy;
    }
    const mov::Car& caddy () const noexcept {
      return m_fleet.caddy;
    }

    mov::Boat& boat () noexcept {
      return m_fleet.boat;
    }
    const mov::Boat& boat () const noexcept {
      return m_fleet.boat;
    }

    mov::JetSki& jet_ski () noexcept {
      return m_fleet.jet_ski;
    }
    const mov::JetSki& jet_ski () const noexcept {
      return m_fleet.jet_ski;
    }

    mov::Glider& glider () noexcept {
      return m_glider;
    }
    const mov::Glider& glider () const noexcept {
      return m_glider;
    }

    Walker& walker () noexcept {
      return m_walker;
    }
    const Walker& walker () const noexcept {
      return m_walker;
    }

    ChaseCamera& camera () noexcept {
      return m_camera;
    }
    const ChaseCamera& camera () const noexcept {
      return m_camera;
    }

    Stars& stars () noexcept {
      return m_stars;
    }
    const Stars& stars () const noexcept {
      return m_stars;
    }

    Dust& dust () noexcept {
      return m_dust;
    }
    const Dust& dust () const noexcept {
      return m_dust;
    }

    // The animals and the barns' poultry, planned with the world
    // (rebuild) and stepped with the session.
    Animals& animals () noexcept {
      return m_animals;
    }
    const Animals& animals () const noexcept {
      return m_animals;
    }
    Hens& hens () noexcept {
      return m_hens;
    }
    const Hens& hens () const noexcept {
      return m_hens;
    }

    position_t subject_position () const;
    direction_t subject_heading () const;
    kilometres_per_hour_t subject_speed_kmh () const;
    bool can_deploy_glider (const map::SurfaceGeometry& terrain) const;
    bool can_drop_bike () const;
    // What the mount key would take, on foot: of the machines, the
    // nearest within its reach (MachineKind::reach).
    std::optional<Machine> mount_within_reach () const;
    // Where someone stepping out of the boat, or off the jet ski, stands:
    // over whichever side, or the bow, has the shallowest water there, as
    // last reckoned.
    position_t boat_landing () const {
      return m_boat_landing;
    }
    // Each machine as the bumpers see it, and a blow to it
    // (game/rescue.hh).
    MachineBody machine_body (Machine machine) const;
    void push_machine (Machine machine,
                       const position_t& at,
                       const impulse_t& impulse);
    // On foot beside a stuck machine, which the mount key would take up a
    // rope to haul rather than get into.
    std::optional<Machine> haul_within_reach () const;
    // The machine on the rope, and where the rope is tied to it.
    std::optional<Machine> hauling () const;
    position_t rope_end () const;
    // Driving a machine that is stuck.
    bool stuck () const;
    // Flying, the machine the hook is over and the line would reach
    // (game/sling.hh), which the mount key would hook on.
    std::optional<Machine> hook_within_reach () const;
    // Flying low and slow enough to step out.
    bool can_step_out_of_helicopter () const;
    void clear_controls ();
    // Begin standing beside the parked bike, seen from behind.
    void start_on_foot ();
    CameraRig camera_rig () const;
    // Moves the camera the way the current view follows the player over
    // `dt`; a following camera reset beforehand stands straight at its
    // place (settle_camera).
    void follow_camera (const map::SurfaceGeometry& surface, seconds_t dt);
    // Puts the camera straight where the current view rests, with no
    // easing: after a cinematic hands the view back.
    void settle_camera (const map::SurfaceGeometry& surface);
    // Changes how the player gets about, and with it the view: mounting
    // takes up the view last used for riding (from behind, unless Tab
    // chose another) and stepping off the one last used on foot.
    void set_mode (Doing mode);

    State state () const;
    void restore (const State& state);

    // Rooms the following camera keeps inside of while the subject is in
    // one (the farmhouses'), in a world of the given period.
    void set_rooms (std::vector<ChaseCamera::Room> rooms,
                    plan_extent_t period) {
      m_rooms = std::move (rooms);
      m_room_period = period;
    }

    void set_boat_landing (const position_t& at) {
      m_boat_landing = at;
    }

  private:
    std::vector<ChaseCamera::Room> m_rooms;
    plan_extent_t m_room_period = plan_extent (0.0f * u::m, 0.0f * u::m);
    GameLogicState m_logic;
    Fleet m_fleet;
    mov::Glider m_glider;
    position_t m_boat_landing = position (Vec3 ());
    Walker m_walker;
    ChaseCamera m_camera;
    Stars m_stars;
    Dust m_dust;
    Animals m_animals;
    Hens m_hens;
  };

  // What the hands do with the machines, when game/affordances.hh says
  // a button does it: open the glider over the airborne bike, or let the
  // bike go from under it; take up a rope to a stuck machine, or let it
  // go; get in, or step out; and from the helicopter, hook a machine on
  // the line, or let it go.
  void deploy_glider (GameSession& session,
                      const map::SurfaceGeometry& terrain);
  void drop_bike (GameSession& session);
  void take_rope (GameSession& session, Machine machine);
  void let_go_rope (GameSession& session);
  void get_in (GameSession& session, Machine machine);
  void step_out (GameSession& session, Machine machine);
  void hook_load (GameSession& session, Machine load);
  void let_load_go (GameSession& session);

  // What the wheels roll over, read from the world by the caller: how much
  // loose dust the ground gives up and its colour, and how thickly fallen
  // leaves lie there to be thrown up.
  struct GroundCover {
    proportion_t snow {};
    proportion_t dust = 1.0f * proportion[one];
    DisplayColor dust_color { 0.60f, 0.52f, 0.40f };
    DisplayColor clod_color { 0.42f, 0.34f, 0.24f };
    proportion_t leaves = 0.0f * proportion[one];
  };

  // `strangers` are the other player's machines, which ours bump into
  // and are pushed off by, though only the other game moves them. With
  // `around`, the step also tends the clock, the camp, the line, the pan,
  // the basket, the beacons, and the animals (surroundings.hh).
  GameSessionAdvanceResult
  advance_game_session (const WorldParams& world,
                        const map::SurfaceGeometry& surface,
                        GameSession& session,
                        const InputFrame& input,
                        seconds_t dt,
                        const mov::TrunkField* trunks = nullptr,
                        const GroundCover& ground = {},
                        const terrain::WaterSheets* water = nullptr,
                        std::span<const MachineBody> strangers = {},
                        const Surroundings* around = nullptr);
}

#endif
