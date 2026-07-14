#ifndef MOPPE_GAME_CINEMATIC_FLIGHT_HH
#define MOPPE_GAME_CINEMATIC_FLIGHT_HH

#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/trail.hh>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace moppe::game {
  enum class CinematicLandmarkKind {
    Trail,
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
                         const Vec3& arrival,
                         const terrain::TrailNetwork* trail = nullptr);

  struct CinematicFlightControls {
    // Body-relative pilot trim. The automatic opening leaves these at zero;
    // a future free-camera mode can feed sticks or pointer input directly.
    float lateral = 0.0f;
    float lift = 0.0f;
    float pace = 0.0f;
  };

  // A fast bird/drone flight model wrapped around the authored gates. The
  // gates become one arc-length-parameterized flight ribbon; bounded thrust,
  // broad-arc banking, a stabilized look-ahead gimbal, and terrain anticipation
  // supply continuous motion without stopping at any individual landmark.
  class CinematicFlight {
  public:
    void start (const CinematicFlightPlan& plan, const map::HeightMap& map);
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
    float speed () const noexcept {
      return m_speed;
    }
    float bank () const noexcept {
      return m_bank;
    }
    float motion_blur () const noexcept {
      return std::clamp ((m_speed - 35.0f) / 185.0f, 0.08f, 0.72f);
    }
    Mat4 view_matrix () const;

  private:
    struct ArcSample {
      float distance;
      std::size_t segment;
      float t;
      float terrain_lift;
      float speed_limit;
    };

    struct RouteState {
      Vec3 position;
      Vec3 subject;
      float cruise_speed;
      float field_of_view;
    };

    Vec3 curve_position (std::size_t segment, float t) const;
    RouteState route_state (float distance) const;
    void build_flight_ribbon (const map::HeightMap& map);

    std::vector<CinematicFlightWaypoint> m_waypoints;
    std::vector<ArcSample> m_arc_samples;
    Vec3 m_position {};
    Vec3 m_velocity {};
    Vec3 m_look_direction { 0, 0, 1 };
    Vec3 m_manual_offset {};
    Vec3 m_manual_velocity {};
    float m_route_distance = 0.0f;
    float m_speed = 0.0f;
    float m_longitudinal_acceleration = 0.0f;
    float m_bank = 0.0f;
    float m_field_of_view = 58.0f;
    float m_elapsed = 0.0f;
    float m_final_hold = 0.0f;
    bool m_active = false;
  };
}

#endif
