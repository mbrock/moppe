
#ifndef MOPPE_VEHICLE_HH
#define MOPPE_VEHICLE_HH

#include <moppe/app/gl.hh>
#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>

#include <boost/format.hpp>

namespace moppe {
namespace mov {
  using namespace moppe::map;

  class Vehicle {
  public:
    Vehicle (const Vector3D& position,
	     degrees_t orientation,
	     const HeightMap& map);

    void render () const;
    void update (seconds_t dt);

    void draw_debug_text () const {
      std::string text =
	(boost::format ("Orientation: %1%.  Position: %2%.")
	 % m_orientation % m_position).str ();
      gl::draw_glut_text (GLUT_BITMAP_HELVETICA_18,
			  20, 60, text);
    }

    void set_speed (magnitude_t meters_per_second)
    { m_speed = meters_per_second; }
    void spin (degrees_t degrees)
    { m_yaw += degrees_to_radians (degrees); }

  private:
    void calculate_orientation ();
    void fall_to_ground ();

  private:
    Vector3D m_position;
    Vector3D m_orientation;

    radians_t m_yaw;
    float m_speed;

    const HeightMap& m_map;
  };
}
}

#endif
