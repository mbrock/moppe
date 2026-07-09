#include <moppe/game/city.hh>
#include <moppe/game/door.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
namespace game {
  const float City::H_CITY = 45.0f;
  const float City::PITCH = 64.0f;
  const float City::STREET_W = 10.0f;
  const float City::CORE = 1750.0f;
  const int City::SEC;

  namespace {
    // The glScalef+glutSolidCube(1) idiom.
    void box (render::DrawList& dl, float w, float h, float d)
    {
      dl.push ();
      dl.scale (w, h, d);
      dl.cube (1.0f);
      dl.pop ();
    }

    // Window quads face every direction; skip culling instead of
    // worrying about their winding (the GL build disabled
    // GL_CULL_FACE for the whole city).
    render::DrawState city_state ()
    {
      render::DrawState s;
      s.cull = false;
      return s;
    }
  }

  City::City ()
    : m_map (0),
      m_air_x0 (0), m_air_x1 (0), m_air_z0 (0), m_air_z1 (0)
  { }

  // -- generation ------------------------------------------------------

  void
  City::generate (map::RandomHeightMap& map, const WorldParams& world)
  {
    m_map = &map;
    m_map_size = world.map_size;
    m_buildings.clear ();
    m_boxes.clear ();
    m_cars.clear ();
    m_people.clear ();

    std::mt19937 rng (909);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);

    const float cx = m_map_size.x / 2, cz = m_map_size.z / 2;
    const float sx = map.scale ().x, sz = map.scale ().z;
    const int W = map.width (), H = map.height ();

    // -- flat slab, smoothed beach ring, shallow sea outside
    for (int gy = 0; gy < H; ++gy)
      for (int gx = 0; gx < W; ++gx) {
	const float wx = gx * sx, wz = gy * sz;
	const float d = std::sqrt ((wx - cx) * (wx - cx)
				   + (wz - cz) * (wz - cz));
	float h;
	if (d < CORE)
	  h = H_CITY;
	else if (d < CORE + 500) {
	  float t = (d - CORE) / 500;
	  t = t * t * (3 - 2 * t);
	  h = H_CITY * (1 - t) - 8 * t;
	}
	else
	  h = -8;

	map.set (gx, gy, std::max (0.0f, h) / m_map_size.y);
      }

    // -- airport zone on the southeast side: keep it clear
    m_air_x0 = cx + 330;
    m_air_x1 = cx + 1170;
    m_air_z0 = cz + 980;
    m_air_z1 = cz + 1130;

    // -- a few big smooth stunt mounds
    for (int i = 0; i < 6; ++i) {
      const float ang = 6.2832f * u (rng);
      const float dist = 400 + 900 * u (rng);
      const float mx = cx + std::cos (ang) * dist;
      const float mz = cz + std::sin (ang) * dist;
      if (in_airport (mx, mz, 80))
	continue;
      const float R = 60 + 30 * u (rng);
      const float MH = 12 + 8 * u (rng);

      for (int gy = (int) ((mz - R) / sz); gy <= (mz + R) / sz; ++gy)
	for (int gx = (int) ((mx - R) / sx); gx <= (mx + R) / sx;
	     ++gx) {
	  if (gx < 0 || gy < 0 || gx >= W || gy >= H)
	    continue;
	  const float wx = gx * sx, wz = gy * sz;
	  const float d = std::sqrt ((wx - mx) * (wx - mx)
				     + (wz - mz) * (wz - mz));
	  if (d >= R)
	    continue;
	  const float c = std::cos (1.5708f * d / R);
	  const float h = H_CITY + MH * c * c;
	  map.set (gx, gy,
		   std::max (map.get (gx, gy), h / m_map_size.y));
	}
    }

    // -- launch-ramp wedges on the streets: rise, then a drop
    for (int i = 0; i < 30; ++i) {
      const bool along_x = u (rng) < 0.5f;
      const int k = (int) ((u (rng) - 0.5f) * 44.0f);
      const float line = (along_x ? cz : cx) + k * PITCH;
      const float at = (along_x ? cx : cz)
	+ (u (rng) - 0.5f) * 2600.0f;
      const float dir = (u (rng) < 0.5f) ? 1.0f : -1.0f;

      const float rx = along_x ? at : line;
      const float rz = along_x ? line : at;
      if (std::sqrt ((rx - cx) * (rx - cx) + (rz - cz) * (rz - cz))
	  > CORE - 200)
	continue;
      if (in_airport (rx, rz, 30))
	continue;

      const float LEN = 22, WID = 9, RH = 7;
      for (int gy = (int) ((rz - LEN) / sz); gy <= (rz + LEN) / sz;
	   ++gy)
	for (int gx = (int) ((rx - LEN) / sx);
	     gx <= (rx + LEN) / sx; ++gx) {
	  if (gx < 0 || gy < 0 || gx >= W || gy >= H)
	    continue;
	  const float wx = gx * sx, wz = gy * sz;
	  const float along = (along_x ? wx - rx : wz - rz) * dir;
	  const float across = along_x ? wz - rz : wx - rx;
	  if (along < -LEN / 2 || along > LEN / 2 ||
	      std::abs (across) > WID / 2)
	    continue;
	  const float t = (along + LEN / 2) / LEN;
	  const float h = H_CITY + RH * t;
	  map.set (gx, gy,
		   std::max (map.get (gx, gy), h / m_map_size.y));
	}
    }

    // -- police and fire stations on two reserved blocks
    {
      const float px = cx + (-4 + 0.5f) * PITCH;
      const float pz = cz + (0 + 0.5f) * PITCH;
      m_police.x0 = px - 18;
      m_police.x1 = px + 18;
      m_police.z0 = pz - 13;
      m_police.z1 = pz + 13;
      m_police.top = H_CITY + 12;
      m_boxes.push_back (m_police);

      const float fx = cx + (3 + 0.5f) * PITCH;
      const float fz = cz + (1 + 0.5f) * PITCH;
      m_fire.x0 = fx - 20;
      m_fire.x1 = fx + 20;
      m_fire.z0 = fz - 14;
      m_fire.z1 = fz + 14;
      m_fire.top = H_CITY + 10;
      m_boxes.push_back (m_fire);
    }

    // -- buildings on the blocks between streets, taller downtown
    for (int bi = -27; bi <= 27; ++bi)
      for (int bj = -27; bj <= 27; ++bj) {
	if ((bi == -4 && bj == 0) || (bi == 3 && bj == 1))
	  continue; // station blocks

	const float bx = cx + (bi + 0.5f) * PITCH;
	const float bz = cz + (bj + 0.5f) * PITCH;
	const float d = std::sqrt ((bx - cx) * (bx - cx)
				   + (bz - cz) * (bz - cz));
	if (d > CORE - 150)
	  continue;
	if (in_airport (bx, bz, PITCH))
	  continue; // runways need headroom
	if (u (rng) > 0.8f)
	  continue; // plaza

	const float hw = (14 + 20 * u (rng)) / 2;
	const float hd = (14 + 20 * u (rng)) / 2;
	const float room = PITCH / 2 - STREET_W / 2 - 3;
	const float ox = (u (rng) - 0.5f) * 2 * (room - hw);
	const float oz = (u (rng) - 0.5f) * 2 * (room - hd);

	const float hmax =
	  18 + 110 * std::exp (-(d / 800) * (d / 800));
	const float bh =
	  std::max (12.0f, hmax * (0.35f + 0.65f * u (rng)));

	Building b;
	b.box.x0 = bx + ox - hw;
	b.box.x1 = bx + ox + hw;
	b.box.z0 = bz + oz - hd;
	b.box.z1 = bz + oz + hd;
	b.box.top = H_CITY + bh;

	static const float palette[5][3] = {
	  { 0.62f, 0.62f, 0.64f }, // concrete
	  { 0.72f, 0.65f, 0.55f }, // sandstone
	  { 0.55f, 0.32f, 0.26f }, // brick
	  { 0.35f, 0.50f, 0.62f }, // glass
	  { 0.80f, 0.80f, 0.78f }, // white
	};
	const int p = (int) (u (rng) * 5) % 5;
	const float j = 0.9f + 0.2f * u (rng);
	b.color = Vector3D (palette[p][0] * j, palette[p][1] * j,
			    palette[p][2] * j);

	m_buildings.push_back (b);
	m_boxes.push_back (b.box);
      }

    // -- traffic: plenty of cars, plus police and fire trucks
    for (int i = 0; i < 316; ++i) {
      Car c;
      c.along_x = u (rng) < 0.5f;
      c.active = true;
      const int k = (int) ((u (rng) - 0.5f) * 40.0f);
      c.dir = (u (rng) < 0.5f) ? 1.0f : -1.0f;
      c.line = (c.along_x ? cz : cx) + k * PITCH + c.dir * 2.6f;
      c.half = std::sqrt (std::max (
	  1.0f, (CORE - 120) * (CORE - 120)
		    - (k * PITCH) * (k * PITCH)));
      c.phase = 3000 * u (rng);

      if (i < 10)
	c.kind = 1; // police
      else if (i < 16)
	c.kind = 2; // fire truck
      else
	c.kind = 0;

      c.speed = (c.kind == 1)   ? 16 + 6 * u (rng)
		: (c.kind == 2) ? 12 + 4 * u (rng)
				: 8 + 8 * u (rng);

      static const float carpal[5][3] = {
	{ 0.8f, 0.15f, 0.1f }, { 0.15f, 0.3f, 0.7f },
	{ 0.85f, 0.85f, 0.85f }, { 0.9f, 0.75f, 0.1f },
	{ 0.1f, 0.5f, 0.45f },
      };
      const int p = (int) (u (rng) * 5) % 5;
      c.color = (c.kind == 1)
		    ? Vector3D (0.92f, 0.92f, 0.95f)
		    : (c.kind == 2)
			  ? Vector3D (0.85f, 0.1f, 0.08f)
			  : Vector3D (carpal[p][0], carpal[p][1],
				      carpal[p][2]);
      m_cars.push_back (c);
    }

    // -- pedestrians strolling the sidewalks
    for (int i = 0; i < 260; ++i) {
      Person p;
      p.along_x = u (rng) < 0.5f;
      const int k = (int) ((u (rng) - 0.5f) * 40.0f);
      p.dir = (u (rng) < 0.5f) ? 1.0f : -1.0f;
      const float side = (u (rng) < 0.5f) ? 1.0f : -1.0f;
      p.line = (p.along_x ? cz : cx) + k * PITCH
	+ side * (STREET_W / 2 + 1.6f);
      p.half = std::sqrt (std::max (
	  1.0f, (CORE - 160) * (CORE - 160)
		    - (k * PITCH) * (k * PITCH)));
      p.speed = 1.1f + 0.9f * u (rng);
      p.phase = 3000 * u (rng);
      p.size = 0.9f + 0.2f * u (rng);
      p.hit_time = 0;
      p.hx = p.hy = p.hz = 0;
      p.hvx = p.hvy = p.hvz = 0;

      static const float shirts[6][3] = {
	{ 0.85f, 0.2f, 0.15f }, { 0.2f, 0.4f, 0.8f },
	{ 0.95f, 0.9f, 0.85f }, { 0.9f, 0.7f, 0.15f },
	{ 0.3f, 0.65f, 0.35f }, { 0.55f, 0.3f, 0.6f },
      };
      const int s = (int) (u (rng) * 6) % 6;
      p.shirt = Vector3D (shirts[s][0], shirts[s][1], shirts[s][2]);
      const float tone = u (rng);
      p.skin = Vector3D (0.55f + 0.38f * tone,
			 0.40f + 0.32f * tone,
			 0.30f + 0.28f * tone);
      m_people.push_back (p);
    }
  }

  // -- simulation ------------------------------------------------------

  int
  City::update_people (const Vector3D& bike_pos,
		       const Vector3D& bike_vel, float time)
  {
    if (!m_map)
      return 0;

    const float speed = bike_vel.length ();
    int hits = 0;

    for (size_t i = 0; i < m_people.size (); ++i) {
      Person& p = m_people[i];

      if (p.hit_time > 0) {
	if (time - p.hit_time > 5.0f)
	  p.hit_time = 0; // dusts off and walks on
	continue;
      }
      if (speed < 5.0f)
	continue;

      float wx, wz;
      person_pos (p, time, wx, wz);
      const float ground = m_map->interpolated_height (wx, wz);

      const float dx = bike_pos.x - wx, dz = bike_pos.z - wz;
      if (dx * dx + dz * dz > 2.5f * 2.5f)
	continue;
      if (bike_pos.y > ground + 3.5f)
	continue; // jumped clean over them

      p.hit_time = time;
      p.hx = wx;
      p.hy = ground + 1.0f;
      p.hz = wz;
      p.hvx = bike_vel.x * 0.55f;
      p.hvz = bike_vel.z * 0.55f;
      p.hvy = 6.5f + 0.12f * speed;
      m_last_hit = Vector3D (wx, ground + 1, wz);
      ++hits;
    }
    return hits;
  }

  int
  City::take_car_near (const Vector3D& pos, float radius, float time,
		       Vector3D& out_pos, Vector3D& out_dir,
		       Vector3D& out_color)
  {
    if (!m_map)
      return -1;

    const float cx = m_map_size.x / 2, cz = m_map_size.z / 2;
    for (size_t i = 0; i < m_cars.size (); ++i) {
      Car& c = m_cars[i];
      if (!c.active)
	continue;

      const float s =
	std::fmod (c.phase + c.speed * time, 2 * c.half) - c.half;
      const float wx = c.along_x ? cx + c.dir * s : c.line;
      const float wz = c.along_x ? c.line : cz + c.dir * s;
      const float dx = pos.x - wx, dz = pos.z - wz;
      if (dx * dx + dz * dz > radius * radius)
	continue;

      c.active = false;
      out_pos = Vector3D (
	  wx, m_map->interpolated_height (wx, wz) + 1.2f, wz);
      out_dir = c.along_x ? Vector3D (c.dir, 0, 0)
			  : Vector3D (0, 0, c.dir);
      out_color = c.color;
      return c.kind;
    }
    return -1;
  }

  int
  City::sector_of (float x, float z) const
  {
    int sx = (int) (x / (m_map_size.x / SEC));
    int sz = (int) (z / (m_map_size.z / SEC));
    sx = std::max (0, std::min (SEC - 1, sx));
    sz = std::max (0, std::min (SEC - 1, sz));
    return sz * SEC + sx;
  }

  bool
  City::in_airport (float x, float z, float margin) const
  {
    return x > m_air_x0 - margin && x < m_air_x1 + margin &&
	   z > m_air_z0 - margin && z < m_air_z1 + margin;
  }

  void
  City::person_pos (const Person& p, float time,
		    float& wx, float& wz) const
  {
    const float cx = m_map_size.x / 2, cz = m_map_size.z / 2;
    const float s =
      std::fmod (p.phase + p.speed * time, 2 * p.half) - p.half;
    wx = p.along_x ? cx + p.dir * s : p.line;
    wz = p.along_x ? p.line : cz + p.dir * s;
  }

  // -- baking ----------------------------------------------------------

  // Compile buildings, streets and furniture into meshes (the port
  // of load_gl; runs once, after generate)
  void
  City::load (render::Renderer& r)
  {
    if (!m_map)
      return;

    std::mt19937 wrng (4321); // window lighting pattern
    const float cxx = m_map_size.x / 2, czz = m_map_size.z / 2;

    // gather lamp and trash can spots up front so they can be
    // bucketed into sectors along with the buildings
    std::vector<Vector3D> lamps, cans;

    int lamp_index = 0;
    for (int axis = 0; axis < 2; ++axis)
      for (int k = -20; k <= 20; ++k) {
	const float line = (axis ? cxx : czz) + k * PITCH;
	for (float s = -CORE; s <= CORE; s += 96) {
	  const float side = (++lamp_index % 2) ? 1.0f : -1.0f;
	  const float off = line + side * (STREET_W / 2 + 0.8f);
	  const float wx = axis ? off : cxx + s;
	  const float wz = axis ? czz + s : off;

	  if (std::sqrt ((wx - cxx) * (wx - cxx)
			 + (wz - czz) * (wz - czz)) > CORE - 100)
	    continue;
	  if (in_airport (wx, wz, 10))
	    continue;
	  lamps.push_back (Vector3D (
	      wx, m_map->interpolated_height (wx, wz), wz));
	}
      }

    for (int ki = -20; ki <= 20; ++ki)
      for (int kj = -20; kj <= 20; ++kj) {
	if ((ki + 2 * kj) % 3 != 0)
	  continue;
	const float wx = cxx + ki * PITCH + STREET_W / 2 + 1.0f;
	const float wz = czz + kj * PITCH + STREET_W / 2 + 1.2f;
	if (std::sqrt ((wx - cxx) * (wx - cxx)
		       + (wz - czz) * (wz - czz)) > CORE - 120)
	  continue;
	if (in_airport (wx, wz, 10))
	  continue;
	cans.push_back (Vector3D (
	    wx, m_map->interpolated_height (wx, wz), wz));
      }

    // Static geometry is bucketed into SEC x SEC sector meshes so
    // distant chunks of the city can be skipped whole
    for (int s = 0; s < SEC * SEC; ++s) {
      render::DrawList dl;
      dl.state (city_state ());

      for (size_t i = 0; i < m_buildings.size (); ++i) {
	const Building& b = m_buildings[i];
	if (sector_of ((b.box.x0 + b.box.x1) / 2,
		       (b.box.z0 + b.box.z1) / 2) != s)
	  continue;
	emit_building (dl, b, wrng);
      }

      for (size_t i = 0; i < lamps.size (); ++i)
	if (sector_of (lamps[i].x, lamps[i].z) == s)
	  emit_lamp (dl, lamps[i]);

      for (size_t i = 0; i < cans.size (); ++i)
	if (sector_of (cans[i].x, cans[i].z) == s)
	  emit_can (dl, cans[i]);

      m_sectors[s] = dl.empty ()
	? render::MeshPtr () : r.create_mesh (dl);
    }

    {
      render::DrawList dl;
      dl.state (city_state ());
      record_streets (dl);
      m_streets = dl.empty ()
	? render::MeshPtr () : r.create_mesh (dl);
    }

    {
      render::DrawList dl;
      dl.state (city_state ());
      record_furniture (dl);
      m_furniture = dl.empty ()
	? render::MeshPtr () : r.create_mesh (dl);
    }
  }

  // streets draped over the terrain (so they follow ramps)
  void
  City::record_streets (render::DrawList& dl) const
  {
    const float cx = m_map_size.x / 2, cz = m_map_size.z / 2;
    dl.normal (Vector3D (0, 1, 0));
    for (int axis = 0; axis < 2; ++axis)
      for (int k = -27; k <= 27; ++k) {
	const float line = (axis ? cx : cz) + k * PITCH;
	if (std::abs (k * PITCH) > CORE)
	  continue;

	dl.color (0.16f, 0.16f, 0.18f);
	dl.begin (render::Prim::QuadStrip);
	for (float s = -CORE; s <= CORE; s += 16) {
	  const float wx = axis ? line : cx + s;
	  const float wz = axis ? cz + s : line;
	  if (std::sqrt ((wx - cx) * (wx - cx)
			 + (wz - cz) * (wz - cz)) > CORE - 30) {
	    // the GL build restarted the strip mid-loop
	    dl.end ();
	    dl.begin (render::Prim::QuadStrip);
	    continue;
	  }
	  const float y =
	    m_map->interpolated_height (wx, wz) + 0.10f;
	  if (axis) {
	    dl.vertex (line - STREET_W / 2, y, wz);
	    dl.vertex (line + STREET_W / 2, y, wz);
	  }
	  else {
	    dl.vertex (wx, y, line - STREET_W / 2);
	    dl.vertex (wx, y, line + STREET_W / 2);
	  }
	}
	dl.end ();
      }
  }

  // The airport and the stations: always-visible landmarks
  void
  City::record_furniture (render::DrawList& dl) const
  {
    draw_station (dl, m_police, false);
    draw_station (dl, m_fire, true);

    // -- the airport: runway, centerline, apron, parked jet
    {
      const float ry = H_CITY + 0.12f;
      const float z0 = (m_air_z0 + m_air_z1) / 2 - 25;
      const float z1 = z0 + 50;

      dl.normal (Vector3D (0, 1, 0));
      dl.color (0.14f, 0.14f, 0.15f);
      dl.begin (render::Prim::Quads);
      dl.vertex (m_air_x0, ry, z0);
      dl.vertex (m_air_x1, ry, z0);
      dl.vertex (m_air_x1, ry, z1);
      dl.vertex (m_air_x0, ry, z1);
      // apron by the runway start
      dl.vertex (m_air_x0, ry, z0 - 60);
      dl.vertex (m_air_x0 + 130, ry, z0 - 60);
      dl.vertex (m_air_x0 + 130, ry, z0);
      dl.vertex (m_air_x0, ry, z0);
      dl.end ();

      // dashed centerline
      dl.color (0.9f, 0.9f, 0.9f);
      dl.begin (render::Prim::Quads);
      for (float x = m_air_x0 + 20; x < m_air_x1 - 20; x += 30) {
	const float mid = (z0 + z1) / 2;
	dl.vertex (x, ry + 0.03f, mid - 0.5f);
	dl.vertex (x + 12, ry + 0.03f, mid - 0.5f);
	dl.vertex (x + 12, ry + 0.03f, mid + 0.5f);
	dl.vertex (x, ry + 0.03f, mid + 0.5f);
      }
      dl.end ();

      // one jet parked on the apron
      dl.push ();
      dl.translate (m_air_x0 + 65, H_CITY + 2.2f, z0 - 30);
      dl.rotate_deg (90, 0, 1, 0);
      dl.scale (1.6f, 1.6f, 1.6f);
      draw_plane (dl);
      dl.pop ();
    }
  }

  void
  City::emit_building (render::DrawList& dl, const Building& b,
		       std::mt19937& rng)
  {
    const float w = b.box.x1 - b.box.x0;
    const float d = b.box.z1 - b.box.z0;
    const float h = b.box.top - H_CITY;

    dl.color (b.color);
    dl.push ();
    dl.translate ((b.box.x0 + b.box.x1) / 2, H_CITY + h / 2,
		  (b.box.z0 + b.box.z1) / 2);
    dl.scale (w, h, d);
    dl.cube (1.0f);
    dl.pop ();

    // darker roof cap
    dl.color (b.color.x * 0.5f, b.color.y * 0.5f, b.color.z * 0.5f);
    dl.push ();
    dl.translate ((b.box.x0 + b.box.x1) / 2, b.box.top + 0.25f,
		  (b.box.z0 + b.box.z1) / 2);
    dl.scale (w + 0.6f, 0.5f, d + 0.6f);
    dl.cube (1.0f);
    dl.pop ();

    draw_windows (dl, b, rng);

    // the front door (walkable on foot -- see Walker::collide)
    const float dx = Door::center_x (b.box);
    const float dhw = Door::draw_half_width ();
    dl.color (0.09f, 0.08f, 0.09f);
    dl.normal (Vector3D (0, 0, 1));
    dl.begin (render::Prim::Quads);
    dl.vertex (dx - dhw, H_CITY, b.box.z1 + 0.06f);
    dl.vertex (dx + dhw, H_CITY, b.box.z1 + 0.06f);
    dl.vertex (dx + dhw, H_CITY + Door::height (), b.box.z1 + 0.06f);
    dl.vertex (dx - dhw, H_CITY + Door::height (), b.box.z1 + 0.06f);
    dl.end ();

    emit_interior (dl, b, rng);
  }

  // Somebody lives here: rug, table and chairs, sofa, TV, a
  // bookshelf full of colorful books, and a warm floor lamp
  void
  City::emit_interior (render::DrawList& dl, const Building& b,
		       std::mt19937& rng)
  {
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const float ix = (b.box.x0 + b.box.x1) / 2;
    const float iz = (b.box.z0 + b.box.z1) / 2;
    const float f = H_CITY;

    // rug
    dl.color (0.45f + 0.4f * u (rng), 0.25f + 0.3f * u (rng),
	      0.3f + 0.3f * u (rng));
    dl.push ();
    dl.translate (ix, f + 0.04f, iz + 1.5f);
    box (dl, 5.0f, 0.07f, 3.6f);
    dl.pop ();

    // table with four legs
    dl.color (0.5f, 0.36f, 0.2f);
    dl.push ();
    dl.translate (ix, f + 0.75f, iz + 1.5f);
    box (dl, 1.8f, 0.1f, 1.1f);
    dl.pop ();
    for (int lx = -1; lx <= 1; lx += 2)
      for (int lz = -1; lz <= 1; lz += 2) {
	dl.push ();
	dl.translate (ix + lx * 0.75f, f + 0.35f,
		      iz + 1.5f + lz * 0.4f);
	box (dl, 0.1f, 0.7f, 0.1f);
	dl.pop ();
      }

    // two chairs
    dl.color (0.4f, 0.28f, 0.16f);
    for (int s = -1; s <= 1; s += 2) {
      dl.push ();
      dl.translate (ix + s * 1.6f, f + 0.45f, iz + 1.5f);
      box (dl, 0.5f, 0.08f, 0.5f);
      dl.pop ();
      dl.push ();
      dl.translate (ix + s * 1.85f, f + 0.8f, iz + 1.5f);
      box (dl, 0.08f, 0.8f, 0.5f);
      dl.pop ();
    }

    // sofa by the west wall, TV opposite
    dl.color (0.2f + 0.5f * u (rng), 0.3f + 0.3f * u (rng), 0.55f);
    dl.push ();
    dl.translate (b.box.x0 + 1.6f, f + 0.4f, iz - 1.5f);
    box (dl, 1.0f, 0.8f, 2.6f);
    dl.pop ();
    dl.push ();
    dl.translate (b.box.x0 + 1.15f, f + 0.85f, iz - 1.5f);
    box (dl, 0.3f, 1.0f, 2.6f);
    dl.pop ();

    dl.color (0.05f, 0.05f, 0.06f);
    dl.push ();
    dl.translate (b.box.x1 - 1.4f, f + 1.3f, iz - 1.5f);
    box (dl, 0.15f, 1.2f, 2.0f);
    dl.pop ();

    // bookshelf on the back wall
    dl.color (0.35f, 0.24f, 0.14f);
    dl.push ();
    dl.translate (ix - 2.0f, f + 1.1f, b.box.z0 + 0.6f);
    box (dl, 2.2f, 2.2f, 0.5f);
    dl.pop ();
    for (int shelf = 0; shelf < 2; ++shelf)
      for (int bk = 0; bk < 5; ++bk) {
	dl.color (0.3f + 0.65f * u (rng), 0.25f + 0.6f * u (rng),
		  0.3f + 0.6f * u (rng));
	dl.push ();
	dl.translate (ix - 2.7f + bk * 0.36f,
		      f + 0.65f + shelf * 0.8f, b.box.z0 + 0.62f);
	box (dl, 0.24f, 0.55f, 0.32f);
	dl.pop ();
      }

    // a warm lamp so it feels like home
    dl.color (0.3f, 0.3f, 0.32f);
    dl.push ();
    dl.translate (ix + 2.5f, f + 0.8f, iz - 2.5f);
    box (dl, 0.1f, 1.6f, 0.1f);
    dl.pop ();
    dl.lit (false);
    dl.color (1.0f, 0.85f, 0.5f);
    dl.push ();
    dl.translate (ix + 2.5f, f + 1.75f, iz - 2.5f);
    dl.sphere (0.28f, 8, 6);
    dl.pop ();
    dl.lit (true);
  }

  // A grid of window quads on each facade, mostly dark glass with
  // the occasional lit one
  void
  City::draw_windows (render::DrawList& dl, const Building& b,
		      std::mt19937& rng)
  {
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const float h = b.box.top - H_CITY;

    dl.lit (false);
    for (int f = 0; f < 4; ++f) {
      const bool xf = f < 2; // facade faces +-x
      const int sign = (f % 2) ? -1 : 1;

      const float w =
	xf ? b.box.z1 - b.box.z0 : b.box.x1 - b.box.x0;
      const int nc = (int) ((w - 3.0f) / 4.2f);
      const int nf = (int) ((h - 3.0f) / 4.2f);
      if (nc < 1 || nf < 1)
	continue;

      const float wall = xf ? (sign > 0 ? b.box.x1 : b.box.x0)
			    : (sign > 0 ? b.box.z1 : b.box.z0);
      const float off = wall + sign * 0.08f;
      const float a0 =
	(xf ? b.box.z0 + b.box.z1 : b.box.x0 + b.box.x1) / 2
	- (nc - 1) * 4.2f / 2;

      dl.normal (Vector3D (xf ? (float) sign : 0.0f, 0.0f,
			   xf ? 0.0f : (float) sign));
      dl.begin (render::Prim::Quads);
      for (int fy = 0; fy < nf; ++fy)
	for (int c = 0; c < nc; ++c) {
	  if (u (rng) < 0.13f)
	    dl.color (1.0f, 0.9f, 0.55f); // someone's home
	  else {
	    const float t = 0.8f + 0.4f * u (rng);
	    dl.color (0.16f * t, 0.22f * t, 0.30f * t);
	  }

	  const float y0 = H_CITY + 2.2f + fy * 4.2f;
	  const float y1 = y0 + 2.4f;
	  const float a = a0 + c * 4.2f;
	  const float aa = a - 1.0f, ab = a + 1.0f;

	  if (xf) {
	    dl.vertex (off, y0, aa);
	    dl.vertex (off, y0, ab);
	    dl.vertex (off, y1, ab);
	    dl.vertex (off, y1, aa);
	  }
	  else {
	    dl.vertex (aa, y0, off);
	    dl.vertex (ab, y0, off);
	    dl.vertex (ab, y1, off);
	    dl.vertex (aa, y1, off);
	  }
	}
      dl.end ();
    }
    dl.lit (true);
  }

  void
  City::emit_lamp (render::DrawList& dl, const Vector3D& p)
  {
    dl.color (0.25f, 0.26f, 0.28f);
    dl.push ();
    dl.translate (p.x, p.y + 2.3f, p.z);
    box (dl, 0.16f, 4.6f, 0.16f);
    dl.pop ();

    // warm lamp head, always lit
    dl.lit (false);
    dl.color (1.0f, 0.88f, 0.55f);
    dl.push ();
    dl.translate (p.x, p.y + 4.7f, p.z);
    dl.sphere (0.32f, 8, 6);
    dl.pop ();
    dl.lit (true);
  }

  void
  City::emit_can (render::DrawList& dl, const Vector3D& p)
  {
    dl.color (0.25f, 0.35f, 0.28f);
    dl.push ();
    dl.translate (p.x, p.y + 0.45f, p.z);
    box (dl, 0.6f, 0.9f, 0.6f);
    dl.pop ();

    dl.color (0.16f, 0.2f, 0.17f);
    dl.push ();
    dl.translate (p.x, p.y + 0.95f, p.z);
    box (dl, 0.68f, 0.1f, 0.68f);
    dl.pop ();
  }

  // A station house: colored hall, stripe, doors, beacon, and a
  // helipad with an H on the police roof
  void
  City::draw_station (render::DrawList& dl, const mov::Box& s,
		      bool fire)
  {
    const float w = s.x1 - s.x0, d = s.z1 - s.z0;
    const float h = s.top - H_CITY;
    const float mx = (s.x0 + s.x1) / 2, mz = (s.z0 + s.z1) / 2;

    if (fire)
      dl.color (0.72f, 0.18f, 0.12f);
    else
      dl.color (0.55f, 0.62f, 0.72f);
    dl.push ();
    dl.translate (mx, H_CITY + h / 2, mz);
    box (dl, w, h, d);
    dl.pop ();

    // stripe around the walls
    if (fire)
      dl.color (0.95f, 0.85f, 0.2f);
    else
      dl.color (0.15f, 0.3f, 0.8f);
    dl.push ();
    dl.translate (mx, H_CITY + h * 0.62f, mz);
    box (dl, w + 0.4f, 1.0f, d + 0.4f);
    dl.pop ();

    // garage doors on the street face
    dl.color (0.12f, 0.13f, 0.15f);
    const int doors = fire ? 3 : 1;
    for (int i = 0; i < doors; ++i) {
      dl.push ();
      dl.translate (mx + (i - (doors - 1) / 2.0f) * 7.5f,
		    H_CITY + 2.3f, s.z1 - 0.1f);
      box (dl, fire ? 5.5f : 4.2f, 4.6f, 0.6f);
      dl.pop ();
    }

    // beacon pole on a corner
    dl.color (0.2f, 0.2f, 0.22f);
    dl.push ();
    dl.translate (s.x0 + 2, s.top + 1.0f, s.z0 + 2);
    box (dl, 0.14f, 2.0f, 0.14f);
    dl.pop ();

    dl.lit (false);
    if (fire)
      dl.color (1.0f, 0.4f, 0.1f);
    else
      dl.color (0.25f, 0.5f, 1.0f);
    dl.push ();
    dl.translate (s.x0 + 2, s.top + 2.2f, s.z0 + 2);
    dl.sphere (0.35f, 8, 6);
    dl.pop ();
    dl.lit (true);

    if (!fire) {
      // helipad: dark disc with a white H
      dl.normal (Vector3D (0, 1, 0));
      dl.color (0.13f, 0.13f, 0.14f);
      dl.begin (render::Prim::TriangleFan);
      dl.vertex (mx, s.top + 0.06f, mz);
      for (int i = 0; i <= 20; ++i) {
	const float a = 6.2832f * i / 20;
	dl.vertex (mx + 5.5f * std::cos (a), s.top + 0.06f,
		   mz + 5.5f * std::sin (a));
      }
      dl.end ();

      dl.color (0.92f, 0.92f, 0.95f);
      for (int sgn = -1; sgn <= 1; sgn += 2) {
	dl.push ();
	dl.translate (mx + sgn * 1.3f, s.top + 0.10f, mz);
	box (dl, 0.5f, 0.06f, 4.0f);
	dl.pop ();
      }
      dl.push ();
      dl.translate (mx, s.top + 0.10f, mz);
      box (dl, 2.1f, 0.06f, 0.5f);
      dl.pop ();
    }
  }

  // -- movers ----------------------------------------------------------

  void
  City::render (render::Renderer& r, render::DrawList& dl,
		const FrameEnv& env)
  {
    if (!m_map)
      return;

    // Only sectors within sight range get drawn at all; beyond
    // ~2km the fog has swallowed them anyway
    const float sec_size = m_map_size.x / SEC;
    const float reach = 1900.0f + sec_size * 0.71f;
    for (int sz = 0; sz < SEC; ++sz)
      for (int sx = 0; sx < SEC; ++sx) {
	const float dx = env.camera_pos.x - (sx + 0.5f) * sec_size;
	const float dz = env.camera_pos.z - (sz + 0.5f) * sec_size;
	if (dx * dx + dz * dz >= reach * reach)
	  continue;
	const render::MeshPtr& mesh = m_sectors[sz * SEC + sx];
	if (mesh)
	  r.draw_mesh (*mesh, Mat4 ());
      }

    if (m_streets)
      r.draw_mesh (*m_streets, Mat4 ());
    if (m_furniture)
      r.draw_mesh (*m_furniture, Mat4 ());

    dl.state (city_state ());

    for (size_t i = 0; i < m_cars.size (); ++i)
      draw_car (dl, m_cars[i], env.time, env.camera_pos);
    for (size_t i = 0; i < m_people.size (); ++i)
      draw_person (dl, m_people[i], env.time, env.camera_pos);

    draw_flying_plane (dl, 0, env.time);
    draw_flying_plane (dl, 1, env.time);
    for (int i = 0; i < 3; ++i)
      draw_helicopter (dl, i, env.time);
    draw_parked_vehicles (dl, env.time);

    dl.state (render::DrawState ());
  }

  void
  City::draw_car (render::DrawList& dl, const Car& c, float time,
		  const Vector3D& cam) const
  {
    if (!c.active)
      return; // the player drove off with this one

    const float cx = m_map_size.x / 2, cz = m_map_size.z / 2;
    const float s =
      std::fmod (c.phase + c.speed * time, 2 * c.half) - c.half;
    const float wx = c.along_x ? cx + c.dir * s : c.line;
    const float wz = c.along_x ? c.line : cz + c.dir * s;

    const float ddx = cam.x - wx, ddz = cam.z - wz;
    if (ddx * ddx + ddz * ddz > 900.0f * 900.0f)
      return; // too far to matter

    const float y = m_map->interpolated_height (wx, wz);

    dl.push ();
    dl.translate (wx, y, wz);
    if (c.along_x)
      dl.rotate_deg (c.dir > 0 ? 90 : -90, 0, 1, 0);
    else if (c.dir < 0)
      dl.rotate_deg (180, 0, 1, 0);

    draw_car_model (dl, c.kind, c.color, time, c.phase);
    dl.pop ();
  }

  // The vehicle model at the origin, facing +z
  void
  City::draw_car_model (render::DrawList& dl, int kind,
			const Vector3D& color, float time,
			float flash_phase)
  {
    const bool truck = (kind == 2);

    dl.color (color);
    dl.push ();
    dl.translate (0, truck ? 0.8f : 0.55f, 0);
    box (dl, truck ? 2.1f : 1.7f, truck ? 1.5f : 0.85f,
	 truck ? 5.6f : 3.6f);
    dl.pop ();

    dl.color (0.2f, 0.25f, 0.3f);
    dl.push ();
    if (truck) {
      dl.translate (0, 1.6f, 1.9f);
      box (dl, 1.9f, 0.7f, 1.4f);
    }
    else {
      dl.translate (0, 1.15f, -0.2f);
      box (dl, 1.5f, 0.6f, 1.9f);
    }
    dl.pop ();

    if (truck) {
      // the ladder on top
      dl.color (0.75f, 0.76f, 0.78f);
      dl.push ();
      dl.translate (0, 1.75f, -0.8f);
      dl.rotate_deg (-6, 1, 0, 0);
      box (dl, 0.5f, 0.12f, 4.4f);
      dl.pop ();
    }

    dl.color (0.08f, 0.08f, 0.1f);
    for (int lx = -1; lx <= 1; lx += 2)
      for (int lz = -1; lz <= 1; lz += 2) {
	dl.push ();
	dl.translate (lx * (truck ? 1.0f : 0.8f), 0.3f,
		      lz * (truck ? 1.7f : 1.2f));
	box (dl, 0.25f, 0.6f, 0.65f);
	dl.pop ();
      }

    // flashing light bar for police and fire
    if (kind != 0) {
      dl.lit (false);
      const bool phase_a =
	std::fmod (time * 3.0f + flash_phase, 1.0f) < 0.5f;
      for (int s = -1; s <= 1; s += 2) {
	const bool blue = (s > 0) == phase_a;
	if (blue)
	  dl.color (0.2f, 0.4f, 1.0f);
	else
	  dl.color (1.0f, 0.15f, 0.1f);
	dl.push ();
	dl.translate (s * 0.35f, truck ? 2.05f : 1.55f,
		      truck ? 2.0f : -0.2f);
	box (dl, 0.4f, 0.22f, 0.35f);
	dl.pop ();
      }
      dl.lit (true);
    }
  }

  // A helicopter at the origin, facing +z
  void
  City::draw_heli_model (render::DrawList& dl, const Vector3D& body,
			 float rotor_deg, bool flash, float time)
  {
    dl.color (body);
    dl.push ();
    dl.translate (0, 0, 0.3f);
    dl.scale (1.1f, 0.95f, 1.7f);
    dl.sphere (1.0f, 10, 8);
    dl.pop ();

    dl.push ();
    dl.translate (0, 0.15f, -2.6f);
    box (dl, 0.24f, 0.3f, 3.4f);
    dl.pop ();

    dl.push ();
    dl.translate (0, 0.7f, -4.1f);
    box (dl, 0.15f, 1.0f, 0.5f);
    dl.pop ();

    // skids
    dl.color (0.2f, 0.2f, 0.22f);
    for (int s = -1; s <= 1; s += 2) {
      dl.push ();
      dl.translate (s * 0.6f, -1.05f, 0.2f);
      box (dl, 0.09f, 0.09f, 2.4f);
      dl.pop ();
    }

    // main rotor, two crossed blades
    dl.color (0.12f, 0.12f, 0.14f);
    dl.push ();
    dl.translate (0, 1.05f, 0.3f);
    dl.rotate_deg (rotor_deg, 0, 1, 0);
    box (dl, 8.0f, 0.06f, 0.3f);
    dl.rotate_deg (90, 0, 1, 0);
    box (dl, 8.0f, 0.06f, 0.3f);
    dl.pop ();

    // tail rotor
    dl.push ();
    dl.translate (0.2f, 0.3f, -4.1f);
    dl.rotate_deg (rotor_deg * 3, 1, 0, 0);
    box (dl, 0.06f, 1.3f, 0.16f);
    dl.pop ();

    if (flash) {
      dl.lit (false);
      if (std::fmod (time * 2.5f, 1.0f) < 0.5f)
	dl.color (0.25f, 0.5f, 1.0f);
      else
	dl.color (1.0f, 0.15f, 0.1f);
      dl.push ();
      dl.translate (0, -0.6f, 0.9f);
      dl.sphere (0.18f, 6, 5);
      dl.pop ();
      dl.lit (true);
    }
  }

  void
  City::draw_helicopter (render::DrawList& dl, int which,
			 float time) const
  {
    const float cx = m_map_size.x / 2, cz = m_map_size.z / 2;
    static const float R[3] = { 500, 800, 1100 };
    static const float ALT[3] = { 130, 170, 210 };
    static const float W[3] = { 0.075f, 0.06f, 0.05f };

    const Vector3D colors[3] = {
      Vector3D (0.92, 0.93, 0.96), // police
      Vector3D (0.80, 0.15, 0.10), // rescue
      Vector3D (0.95, 0.80, 0.15), // traffic reporter
    };

    const float a = time * W[which] + which * 2.1f;

    dl.push ();
    dl.translate (cx + std::cos (a) * R[which],
		  H_CITY + ALT[which],
		  cz + std::sin (a) * R[which]);
    dl.rotate_deg (-a * 57.2958f, 0, 1, 0);
    dl.rotate_deg (-12, 0, 0, 1);
    dl.scale (1.3f, 1.3f, 1.3f);
    draw_heli_model (dl, colors[which], time * 1080, which < 2,
		     time);
    dl.pop ();
  }

  void
  City::draw_parked_vehicles (render::DrawList& dl, float time) const
  {
    // police cars out front, flashers going
    for (int i = 0; i < 2; ++i) {
      dl.push ();
      dl.translate (m_police.x0 + 9 + i * 12, H_CITY,
		    m_police.z1 + 6);
      dl.rotate_deg (i ? 100.0f : 80.0f, 0, 1, 0);
      draw_car_model (dl, 1, Vector3D (0.92, 0.92, 0.95), time,
		      0.5f * i);
      dl.pop ();
    }

    // the fire truck by its hall
    dl.push ();
    dl.translate ((m_fire.x0 + m_fire.x1) / 2, H_CITY,
		  m_fire.z1 + 7);
    dl.rotate_deg (90, 0, 1, 0);
    draw_car_model (dl, 2, Vector3D (0.85, 0.1, 0.08), time, 0.3f);
    dl.pop ();

    // the police helicopter idling on its pad
    dl.push ();
    dl.translate ((m_police.x0 + m_police.x1) / 2,
		  m_police.top + 1.16f,
		  (m_police.z0 + m_police.z1) / 2);
    dl.rotate_deg (35, 0, 1, 0);
    draw_heli_model (dl, Vector3D (0.92, 0.93, 0.96), time * 80,
		     false, time);
    dl.pop ();
  }

  void
  City::draw_person_body (render::DrawList& dl, const Person& p,
			  float swing)
  {
    // legs swinging in opposite phase
    dl.color (0.18f, 0.18f, 0.24f);
    for (int leg = -1; leg <= 1; leg += 2) {
      dl.push ();
      dl.translate (leg * 0.09f, 0.78f, 0);
      dl.rotate_deg (swing * leg, 1, 0, 0);
      dl.translate (0, -0.38f, 0);
      box (dl, 0.13f, 0.76f, 0.13f);
      dl.pop ();
    }

    // torso
    dl.color (p.shirt);
    dl.push ();
    dl.translate (0, 1.14f, 0);
    box (dl, 0.38f, 0.6f, 0.22f);
    dl.pop ();

    // arms, counter-swinging
    for (int arm = -1; arm <= 1; arm += 2) {
      dl.push ();
      dl.translate (arm * 0.25f, 1.38f, 0);
      dl.rotate_deg (-swing * arm * 0.7f, 1, 0, 0);
      dl.translate (0, -0.25f, 0);
      box (dl, 0.09f, 0.5f, 0.09f);
      dl.pop ();
    }

    // head
    dl.color (p.skin);
    dl.push ();
    dl.translate (0, 1.62f, 0);
    dl.sphere (0.13f, 8, 6);
    dl.pop ();
  }

  void
  City::draw_person (render::DrawList& dl, const Person& p,
		     float time, const Vector3D& cam) const
  {
    {
      float wx, wz;
      person_pos (p, time, wx, wz);
      const float ddx = cam.x - wx, ddz = cam.z - wz;
      if (ddx * ddx + ddz * ddz > 450.0f * 450.0f)
	return; // a pixel at best
    }

    // Bowled over: fly in an arc, tumbling, then lie flat for a
    // moment before getting up again
    if (p.hit_time > 0) {
      const float t = time - p.hit_time;
      const float x = p.hx + p.hvx * t;
      const float z = p.hz + p.hvz * t;
      float y = p.hy + p.hvy * t - 4.9f * t * t;

      const float ground = m_map->interpolated_height (x, z);
      const bool flat = y < ground + 0.35f;
      if (flat)
	y = ground + 0.35f;

      dl.push ();
      dl.translate (x, y, z);
      if (flat)
	dl.rotate_deg (90, 1, 0, 0);
      else
	dl.rotate_deg (t * 540.0f, 1, 0.4f, 0.2f);
      dl.scale (p.size, p.size, p.size);
      dl.translate (0, -0.9f, 0); // tumble about the body center
      draw_person_body (dl, p, 0);
      dl.pop ();
      return;
    }

    float wx, wz;
    person_pos (p, time, wx, wz);
    const float y = m_map->interpolated_height (wx, wz);

    dl.push ();
    dl.translate (wx, y, wz);
    if (p.along_x)
      dl.rotate_deg (p.dir > 0 ? 90 : -90, 0, 1, 0);
    else if (p.dir < 0)
      dl.rotate_deg (180, 0, 1, 0);
    dl.scale (p.size, p.size, p.size);

    draw_person_body (dl, p,
		      30.0f * std::sin (time * 6.5f + p.phase));
    dl.pop ();
  }

  // A stylized jet: fuselage, swept wings, tailplane, red fin
  void
  City::draw_plane (render::DrawList& dl)
  {
    dl.color (0.92f, 0.93f, 0.95f);
    dl.push ();
    dl.scale (1.1f, 1.1f, 8.5f);
    dl.sphere (1.0f, 10, 8);
    dl.pop ();

    dl.push ();
    dl.translate (0, -0.2f, 0.5f);
    box (dl, 17.0f, 0.28f, 2.8f);
    dl.pop ();

    dl.push ();
    dl.translate (0, 0.3f, -6.8f);
    box (dl, 5.5f, 0.22f, 1.5f);
    dl.pop ();

    dl.color (0.85f, 0.15f, 0.1f);
    dl.push ();
    dl.translate (0, 1.4f, -6.9f);
    box (dl, 0.22f, 2.4f, 1.7f);
    dl.pop ();
  }

  void
  City::draw_flying_plane (render::DrawList& dl, int which,
			   float time) const
  {
    const float cx = m_map_size.x / 2, cz = m_map_size.z / 2;
    const float r = which ? 1300.0f : 900.0f;
    const float alt = which ? H_CITY + 380 : H_CITY + 280;
    const float w = which ? 0.042f : 0.055f;
    const float a = time * w + which * 3.14159f;

    dl.push ();
    dl.translate (cx + std::cos (a) * r, alt,
		  cz + std::sin (a) * r);
    dl.rotate_deg (-a * 57.2958f, 0, 1, 0);
    dl.rotate_deg (-15, 0, 0, 1); // bank into the turn
    dl.scale (1.6f, 1.6f, 1.6f);
    draw_plane (dl);
    dl.pop ();
  }
}
}
