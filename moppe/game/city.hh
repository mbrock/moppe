#ifndef MOPPE_GAME_CITY_HH
#define MOPPE_GAME_CITY_HH

#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/mov/vehicle.hh>
#include <moppe/render/renderer.hh>

#include <random>
#include <vector>

namespace moppe {
namespace game {
  // --city mode: a flat city island baked into the heightmap
  // (streets, launch ramps, stunt mounds, a beach ring into the
  // sea), buildings as solid collision boxes with drivable roofs,
  // and box-cars cruising the street grid.  Port of main.cc's City:
  // the 64 sector display lists, the terrain-draped streets and the
  // landmark furniture become baked meshes; the movers (cars,
  // pedestrians, aircraft) are recorded into the per-frame list.
  class City {
  public:
    static const float H_CITY;
    static const float PITCH;
    static const float STREET_W;
    static const float CORE;
    static const int SEC = 8;

    City ();

    // Bakes the island slab/beach/sea, six stunt mounds and thirty
    // ramp wedges into the shared heightmap (fixed seed 909; must
    // run before recompute_normals, as in the GL build), then
    // places the stations, buildings, traffic and pedestrians.
    void generate (map::RandomHeightMap& map,
		   const WorldParams& world);

    // Bakes the sector meshes, the streets and the furniture; the
    // source data is retained.
    void load (render::Renderer& r);

    // Bowl over any pedestrian the bike plows through; returns how
    // many got launched this tick.
    int update_people (const Vector3D& bike_pos,
		       const Vector3D& bike_vel, float time);

    Vector3D last_hit () const { return m_last_hit; }

    // Take the nearest still-driving car within reach: it leaves
    // traffic and becomes the player's.  Returns its kind
    // (0 civilian, 1 police, 2 fire truck), or -1.
    int take_car_near (const Vector3D& pos, float radius, float time,
		       Vector3D& out_pos, Vector3D& out_dir,
		       Vector3D& out_color);

    const std::vector<mov::Box>& obstacles () const
    { return m_boxes; }

    // Sector-culled static meshes plus the per-frame movers.
    void render (render::Renderer& r, render::DrawList& dl,
		 const FrameEnv& env);

  private:
    struct Building {
      mov::Box box;
      Vector3D color;
    };

    struct Car {
      bool along_x;
      bool active;   // false once the player has taken it
      int kind;      // 0 civilian, 1 police, 2 fire truck
      float line, dir, speed, phase, half;
      Vector3D color;
    };

    struct Person {
      bool along_x;
      float line, dir, speed, phase, size, half;
      Vector3D shirt, skin;
      // set when the bike bowls them over: launch point + velocity
      float hit_time;
      float hx, hy, hz, hvx, hvy, hvz;
    };

    int sector_of (float x, float z) const;
    bool in_airport (float x, float z, float margin) const;
    void person_pos (const Person& p, float time,
		     float& wx, float& wz) const;

    static void emit_building (render::DrawList& dl,
			       const Building& b, std::mt19937& rng);
    static void emit_interior (render::DrawList& dl,
			       const Building& b, std::mt19937& rng);
    static void draw_windows (render::DrawList& dl,
			      const Building& b, std::mt19937& rng);
    static void emit_lamp (render::DrawList& dl, const Vector3D& p);
    static void emit_can (render::DrawList& dl, const Vector3D& p);
    static void draw_station (render::DrawList& dl,
			      const mov::Box& s, bool fire);
    static void draw_car_model (render::DrawList& dl, int kind,
				const Vector3D& color, float time,
				float flash_phase);
    static void draw_heli_model (render::DrawList& dl,
				 const Vector3D& body,
				 float rotor_deg, bool flash,
				 float time);
    static void draw_plane (render::DrawList& dl);
    static void draw_person_body (render::DrawList& dl,
				  const Person& p, float swing);

    void record_streets (render::DrawList& dl) const;
    void record_furniture (render::DrawList& dl) const;

    void draw_car (render::DrawList& dl, const Car& c,
		   float time, const Vector3D& cam) const;
    void draw_person (render::DrawList& dl, const Person& p,
		      float time, const Vector3D& cam) const;
    void draw_helicopter (render::DrawList& dl, int which,
			  float time) const;
    void draw_flying_plane (render::DrawList& dl, int which,
			    float time) const;
    void draw_parked_vehicles (render::DrawList& dl,
			       float time) const;

    map::RandomHeightMap* m_map;
    Vector3D m_map_size;

    std::vector<Building> m_buildings;
    std::vector<mov::Box> m_boxes;
    std::vector<Car> m_cars;
    std::vector<Person> m_people;

    render::MeshPtr m_sectors[SEC * SEC];
    render::MeshPtr m_streets;
    render::MeshPtr m_furniture;

    float m_air_x0, m_air_x1, m_air_z0, m_air_z1;
    mov::Box m_police, m_fire;
    Vector3D m_last_hit;
  };
}
}

#endif
