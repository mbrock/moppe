#ifndef MOPPE_GAME_CINEMATIC_FLIGHT_HH
#define MOPPE_GAME_CINEMATIC_FLIGHT_HH

#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <cstdint>
#include <string_view>
#include <vector>

namespace moppe::game {
  enum class CinematicLandmarkKind {
    Valley,
    Waterfall,
    Lake,
    Saddle,
    Peak,
    Arrival
  };

  std::string_view
  cinematic_landmark_name (CinematicLandmarkKind kind) noexcept;

  struct CinematicLandmark {
    CinematicLandmarkKind kind;
    terrain::CellIndex cell;
    float score;
    Vec3 position;
  };

  struct CinematicFlightWaypoint {
    Vec3 position;
    Vec3 subject;
    float cruise_speed = 90.0f;
    float field_of_view = 58.0f;
  };

  struct CinematicFlightPlan {
    std::vector<CinematicLandmark> landmarks;
    std::vector<CinematicFlightWaypoint> waypoints;

    bool empty () const noexcept {
      return waypoints.size () < 2;
    }
  };

  // The planner reads both the continuous height field and the discrete
  // hydrology graph. Its output is deliberately plain data: documentary
  // playback, an editor camera, or a player-steerable aircraft can all use the
  // same landscape interpretation without inheriting the opening cinematic.
  CinematicFlightPlan
  plan_cinematic_flight (const map::HeightMap& map,
                         const terrain::FloodField& flood,
                         const terrain::LakeCensus& census,
                         const terrain::DrainageGraph& drainage,
                         const terrain::RiverNetwork& rivers,
                         const Vec3& arrival);

  struct CinematicFlightControls {
    // Body-relative pilot trim. The automatic opening leaves these at zero;
    // a future free-camera mode can feed sticks or pointer input directly.
    float lateral = 0.0f;
    float lift = 0.0f;
    float pace = 0.0f;
  };

  // A small rotorcraft flight model wrapped around the authored gates. The
  // route supplies intent; bounded acceleration and jerk, coordinated bank,
  // a stabilized camera gimbal, and terrain look-ahead supply motion.
  class CinematicFlight {
  public:
    void start (const CinematicFlightPlan& plan);
    void stop () noexcept;
    void tick (float dt,
               const map::HeightMap& map,
               const CinematicFlightControls& controls = {});

    bool active () const noexcept {
      return m_active;
    }
    float elapsed () const noexcept {
      return m_elapsed;
    }
    const Vec3& position () const noexcept {
      return m_position;
    }
    Vec3 forward () const;
    float field_of_view () const noexcept {
      return m_field_of_view;
    }
    std::size_t waypoint_index () const noexcept {
      return m_waypoint;
    }
    std::size_t waypoint_count () const noexcept {
      return m_waypoints.size ();
    }
    float gate_distance () const noexcept {
      return m_waypoint < m_waypoints.size ()
               ? length (m_waypoints[m_waypoint].position + m_manual_offset -
                         m_position)
               : 0.0f;
    }
    Mat4 view_matrix () const;

  private:
    std::vector<CinematicFlightWaypoint> m_waypoints;
    std::size_t m_waypoint = 0;
    Vec3 m_position {};
    Vec3 m_velocity {};
    Vec3 m_acceleration {};
    Vec3 m_subject {};
    Vec3 m_manual_offset {};
    float m_bank = 0.0f;
    float m_field_of_view = 58.0f;
    float m_elapsed = 0.0f;
    float m_final_hold = 0.0f;
    bool m_active = false;
  };
}

#endif
