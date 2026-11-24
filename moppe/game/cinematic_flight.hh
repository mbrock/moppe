#ifndef MOPPE_GAME_CINEMATIC_FLIGHT_HH
#define MOPPE_GAME_CINEMATIC_FLIGHT_HH

#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/watercourse.hh>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace moppe::game {
  enum class CinematicLandmarkKind {
    Trail,
    Valley,
    Waterfall,
    Lake,
    Saddle,
    Sanctuary,
    Peak,
    Arrival
  };

  std::string_view
  cinematic_landmark_name (CinematicLandmarkKind kind) noexcept;

  struct CinematicLandmark {
    CinematicLandmarkKind kind;
    terrain::CellIndex cell;
    float score;
    position_t position;
  };

  struct CinematicFlightWaypoint {
    position_t position;
    position_t subject;
    speed_t cruise_speed = 90.0f * u::m / u::s;
    degrees_t field_of_view = 58.0f * u::deg;
  };

  // How a flight carries itself over the ground: the opening's is a fast,
  // high swoop between landmarks; a river's is low and slow enough to watch
  // the water.
  struct CinematicFlightStyle {
    // The route keeps this far over the ground beneath it, climbing before
    // a rise over about ANTICIPATION metres, and the aircraft itself never
    // comes lower than FLOOR.
    meters_t clearance = 28.0f * u::m;
    meters_t floor = 22.0f * u::m;
    meters_t anticipation = 140.0f * u::m;
    // The slowest a tight bend brings it down to.
    speed_t slowest_turn = 42.0f * u::m / u::s;
    // The stretch of route ahead its gimbal aims along, as the least and
    // most to that stretch's near and far ends.
    meters_t look_near_least = 40.0f * u::m;
    meters_t look_near_most = 110.0f * u::m;
    meters_t look_far_least = 240.0f * u::m;
    meters_t look_far_most = 460.0f * u::m;
    // How much of its aim is the waypoints' subject and not the route.
    proportion_t subject_weight = 0.50f * one;
    // The steepest it banks into a turn.
    radians_t bank = 0.17f * u::rad;
    // Arc-length smoothing scale for the subject look target along the ribbon.
    meters_t subject_smoothing = 110.0f * u::m;
  };

  struct CinematicFlightPlan {
    std::vector<CinematicLandmark> landmarks;
    std::vector<CinematicFlightWaypoint> waypoints;
    CinematicFlightStyle style;

    bool empty () const noexcept {
      return waypoints.size () < 2;
    }
  };

  // A tree beside a flight's way: where it stands, and how tall.
  struct RiverFlightTree {
    plan_position_t at;
    meters_t height;
  };

  // The planner reads both the continuous height field and the discrete
  // hydrology graph. Its output is deliberately plain data: documentary
  // playback, an editor camera, or a player-steerable aircraft can all use the
  // same landscape interpretation without inheriting the opening cinematic.
  // TREES lifts the flight ribbon cleanly over forest crowns; DURATION paces
  // the tour when positive; SANCTUARIES adds a peak-sanctuary sweep when one
  // stands distinct from the main summit.
  CinematicFlightPlan
  plan_cinematic_flight (const map::SurfaceGeometry& surface,
                         const terrain::FloodField& flood,
                         const terrain::LakeCensus& census,
                         const terrain::DrainageGraph& drainage,
                         const terrain::RiverNetwork& rivers,
                         const position_t& arrival,
                         const terrain::TrailNetwork* trail = nullptr,
                         std::span<const RiverFlightTree> trees = {},
                         seconds_t duration = 0.0f * u::s,
                         std::span<const position_t> sanctuaries = {});

  // The world's greatest river to reach the sea: its mouth's reach first,
  // then upward the larger branch at every confluence and the larger
  // inflow of every lake, to the reach where it first shows. Empty when
  // no river reaches the sea.
  std::vector<const terrain::RiverReach*>
  greatest_river_course (std::span<const terrain::WaterBody> bodies,
                         const terrain::RiverNetwork& rivers);

  // A flight down the world's greatest watercourse: from the top of its
  // catchment down the dry valley to where the stream first shows, along
  // it through its lakes, and out over the sea. It flies low over open
  // water and clear of the crowns where trees stand by the stream: TREES
  // gives each tree's place and height. SECONDS is how long the
  // whole flight should take; zero or less leaves it at a watching pace.
  CinematicFlightPlan plan_river_flight (const map::SurfaceGeometry& surface,
                                         std::span<const RiverFlightTree> trees,
                                         const terrain::WaterSheets& water,
                                         const terrain::LakeCensus& census,
                                         const terrain::DrainageGraph& drainage,
                                         const terrain::RiverNetwork& rivers,
                                         seconds_t seconds);

  struct CinematicFlightControls {
    // Body-relative pilot trim. The automatic opening leaves these at zero;
    // a future free-camera mode can feed sticks or pointer input directly.
    control_signal_t lateral = 0.0f * one;
    control_signal_t lift = 0.0f * one;
    control_signal_t pace = 0.0f * one;
  };

  // A fast bird/drone flight model wrapped around the authored gates. The
  // gates become one arc-length-parameterized flight ribbon; bounded thrust,
  // broad-arc banking, a stabilized look-ahead gimbal, and terrain anticipation
  // supply continuous motion without stopping at any individual landmark.
  class CinematicFlight {
  public:
    void start (const CinematicFlightPlan& plan,
                const map::SurfaceGeometry& surface);
    void stop () noexcept;
    // The flight model is a tuned drone's, worked in plain metres and
    // seconds inside; what it is told and what it reports carry units.
    void tick (seconds_t step,
               const map::SurfaceGeometry& surface,
               const CinematicFlightControls& controls = {});

    bool active () const noexcept {
      return m_active;
    }
    seconds_t elapsed () const noexcept {
      return m_elapsed;
    }
    proportion_t route_progress () const noexcept {
      if (m_arc_samples.empty () ||
          m_arc_samples.back ().distance <= 0.0f * u::m)
        return 0.0f * one;
      return std::clamp<proportion_t> (m_route_distance /
                                         m_arc_samples.back ().distance,
                                       0.0f * one,
                                       1.0f * one);
    }
    const position_t& position () const noexcept {
      return m_position;
    }
    direction_t forward () const;
    degrees_t field_of_view () const noexcept {
      return m_field_of_view;
    }
    speed_t speed () const noexcept {
      return m_speed;
    }
    radians_t bank () const noexcept {
      return m_bank;
    }
    proportion_t motion_blur () const noexcept {
      return std::clamp<proportion_t> ((m_speed - 35.0f * u::m / u::s) /
                                         (185.0f * u::m / u::s),
                                       0.08f * one,
                                       0.72f * one);
    }
    Mat4 view_matrix () const;

  private:
    struct ArcSample {
      meters_t distance;
      position_t position;
      position_t subject;
      speed_t cruise_speed;
      degrees_t field_of_view;
    };

    struct RouteState {
      position_t position;
      position_t subject;
      speed_t cruise_speed;
      degrees_t field_of_view;
    };

    position_t curve_position (std::size_t segment, blend_weight_t t) const;
    RouteState route_state (meters_t distance) const;
    void build_flight_ribbon (const map::SurfaceGeometry& surface);

    std::vector<CinematicFlightWaypoint> m_waypoints;
    CinematicFlightStyle m_style;
    std::vector<ArcSample> m_arc_samples;
    position_t m_position {};
    velocity_t m_velocity {};
    direction_t m_look_direction = unit_direction (Vec3 (0, 0, 1));
    angular_velocity_t m_look_rate {};
    displacement_t m_manual_offset {};
    velocity_t m_manual_velocity {};
    meters_t m_route_distance = 0.0f * u::m;
    speed_t m_speed = 0.0f * u::m / u::s;
    metres_per_second_squared_t m_longitudinal_acceleration =
      0.0f * u::m / u::s2;
    radians_t m_bank = 0.0f * u::rad;
    radians_per_second_t m_bank_rate = 0.0f * u::rad / u::s;
    degrees_t m_field_of_view = 58.0f * u::deg;
    seconds_t m_elapsed = 0.0f * u::s;
    seconds_t m_final_hold = 0.0f * u::s;
    bool m_active = false;
  };
}

#endif
