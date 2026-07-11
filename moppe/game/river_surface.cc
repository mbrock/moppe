#include <moppe/game/river_surface.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace moppe::game {
  namespace {
    struct RibbonPoint {
      Vector3D position;
      Vector3D normal;
      float width;
      float distance;
      float rapid;
      float discharge;
      float waterfall;
      float opacity;
      bool water;
    };

    int minimum_image_delta (int delta, int period) {
      if (delta > period / 2)
	delta -= period;
      else if (delta < -period / 2)
	delta += period;
      return delta;
    }

    float river_width (float area) {
      return std::clamp (0.008f * std::sqrt (area), 1.2f, 12.0f);
    }

    float discharge_signal (float area, float minimum_area) {
      const float ratio = std::max (1.0f, area / minimum_area);
      return std::clamp (std::log2 (ratio) / 10.0f, 0.0f, 1.0f);
    }

    float rapid_signal (float slope) {
      return std::clamp ((slope - 0.035f) / 0.24f, 0.0f, 1.0f);
    }

    Vector3D limited_tangent (const Vector3D& value, float limit) {
      Vector3D tangent (value.x, 0.0f, value.z);
      const float length = tangent.length ();
      if (length > limit && length > 1e-6f)
	tangent *= limit / length;
      return tangent;
    }

    float interpolate (float from, float to, float t) {
      return from + (to - from) * t;
    }

    std::vector<RibbonPoint>
    smooth_centerline (const std::vector<RibbonPoint>& raw,
		       const map::HeightMap& map)
    {
      constexpr int subdivisions = 2;
      std::vector<RibbonPoint> result;
      if (raw.empty ())
	return result;
      result.reserve (1 + (raw.size () - 1) * subdivisions);
      result.push_back (raw.front ());
      for (std::size_t segment = 0; segment + 1 < raw.size (); ++segment) {
	const RibbonPoint& before = raw[segment == 0 ? 0 : segment - 1];
	const RibbonPoint& from = raw[segment];
	const RibbonPoint& to = raw[segment + 1];
	const RibbonPoint& after = raw
	  [segment + 2 < raw.size () ? segment + 2 : segment + 1];
	const Vector3D edge = to.position - from.position;
	const float edge_length = std::hypot (edge.x, edge.z);
	const Vector3D from_tangent = limited_tangent
	  ((to.position - before.position) * 0.5f, edge_length);
	const Vector3D to_tangent = limited_tangent
	  ((after.position - from.position) * 0.5f, edge_length);
	for (int step = 1; step <= subdivisions; ++step) {
	  const float t = static_cast<float> (step) / subdivisions;
	  const float t2 = t * t;
	  const float t3 = t2 * t;
	  const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
	  const float h10 = t3 - 2.0f * t2 + t;
	  const float h01 = -2.0f * t3 + 3.0f * t2;
	  const float h11 = t3 - t2;
	  Vector3D position = from.position * h00 + from_tangent * h10
	    + to.position * h01 + to_tangent * h11;
	  const bool touches_water = from.water || to.water;
	  position.y = touches_water
	    ? interpolate (from.position.y, to.position.y, t)
	    : map.interpolated_height (position.x, position.z) + 0.10f;
	  result.push_back ({
	    .position = position,
	    .normal = touches_water ? Vector3D (0, 1, 0)
	      : map.interpolated_normal (position.x, position.z).normalized (),
	    .width = interpolate (from.width, to.width, t),
	    .distance = 0.0f,
	    .rapid = interpolate (from.rapid, to.rapid, t),
	    .discharge = interpolate (from.discharge, to.discharge, t),
	    .waterfall = interpolate (from.waterfall, to.waterfall, t),
	    .opacity = interpolate (from.opacity, to.opacity, t),
	    .water = touches_water
	  });
	}
      }
      result.front ().normal = raw.front ().normal;
      result.back ().normal = raw.back ().normal;
      result.front ().water = raw.front ().water;
      result.back ().water = raw.back ().water;
      float distance = 0.0f;
      result.front ().distance = 0.0f;
      for (std::size_t i = 1; i < result.size (); ++i) {
	const Vector3D delta = result[i].position - result[i - 1].position;
	distance += std::hypot (delta.x, delta.z);
	result[i].distance = distance;
      }
      return result;
    }
  }

  float
  visible_river_minimum_area (const terrain::TerrainGrid& grid) noexcept
  {
    // At the normal five-metre terrain spacing this begins visible flow at
    // roughly 2.4 hectares: enough catchment to read as a persistent channel
    // while leaving smaller runoff paths to the analysis overlays.
    return 1024.0f * grid.spacing_x * grid.spacing_y;
  }

  render::DrawList
  build_river_ribbons (const map::HeightMap& map,
		       const terrain::FloodField& flood,
		       const terrain::LakeCensus& census,
		       const terrain::DrainageGraph& drainage,
		       const terrain::RiverNetwork& rivers)
  {
    render::DrawList draw;
    render::DrawState state;
    state.blend = true;
    state.depth_write = false;
    state.cull = false;
    draw.state (state);
    draw.lit (false);
    draw.fogged (true);

    const int width = static_cast<int> (drainage.width ());
    const int height = static_cast<int> (drainage.height ());
    const Vector3D scale = map.scale ();
    const bool periodic = map.periodic ();
    const auto water_cell = [&] (std::uint32_t cell) {
      return census.body[cell] != terrain::LakeCensus::dry
	|| flood.ocean[cell];
    };

    std::vector<RibbonPoint> raw_points;
    for (const terrain::RiverReach& reach : rivers.reaches) {
      if (reach.cells.empty ())
	continue;
      raw_points.clear ();
      std::vector<std::uint32_t> cells = reach.cells;
      const std::uint32_t receiver =
	drainage.receiver[reach.cells.back ()];
      if (receiver != reach.cells.back ())
	cells.push_back (receiver);
      std::size_t first_water = cells.size ();
      if (water_cell (cells.back ())) {
	first_water = cells.size () - 1;
	for (int step = 0; step < 1; ++step) {
	  const std::uint32_t next = drainage.receiver[cells.back ()];
	  if (next == cells.back () || !water_cell (next))
	    break;
	  int dx = static_cast<int> (next % drainage.width ())
	    - static_cast<int> (cells.back () % drainage.width ());
	  int dz = static_cast<int> (next / drainage.width ())
	    - static_cast<int> (cells.back () / drainage.width ());
	  if (periodic) {
	    dx = minimum_image_delta (dx, width);
	    dz = minimum_image_delta (dz, height);
	  }
	  if (std::abs (dx) > 1 || std::abs (dz) > 1)
	    break;
	  cells.push_back (next);
	}
      }

      const std::size_t water_points = first_water < cells.size ()
	? cells.size () - first_water : 0;

      float world_x = static_cast<float> (cells[0] % width) * scale.x;
      float world_z = static_cast<float> (cells[0] / width) * scale.z;
      float distance = 0.0f;
      for (std::size_t i = 0; i < cells.size (); ++i) {
	const std::uint32_t cell = cells[i];
	const int x = static_cast<int> (cell % width);
	const int z = static_cast<int> (cell / width);
	if (i > 0) {
	  const int previous_x = static_cast<int> (cells[i - 1] % width);
	  const int previous_z = static_cast<int> (cells[i - 1] / width);
	  int dx = x - previous_x;
	  int dz = z - previous_z;
	  if (periodic) {
	    dx = minimum_image_delta (dx, width);
	    dz = minimum_image_delta (dz, height);
	  }
	  const float step_x = static_cast<float> (dx) * scale.x;
	  const float step_z = static_cast<float> (dz) * scale.z;
	  world_x += step_x;
	  world_z += step_z;
	  distance += std::hypot (step_x, step_z);
	}
	const bool water = water_cell (cell);
	const float y = water
	  ? flood.water_level.values ()[cell] * scale.y + 0.06f
	  : map.get (x, z) * scale.y + 0.10f;
	const float area = drainage.contributing_area.values ()[cell];
	const float mouth_step = i >= first_water
	  ? static_cast<float> (i - first_water) : 0.0f;
	const float mouth_fraction = water_points > 1 && i >= first_water
	  ? mouth_step / static_cast<float> (water_points - 1) : 0.0f;
	const bool joins_downstream_reach = i + 1 == cells.size ()
	  && reach.downstream_reach != terrain::RiverReach::no_id;
	const std::uint32_t slope_cell = i + 1 == cells.size () && i > 0
	  ? cells[i - 1] : cell;
	const float slope = drainage.slope.values ()[slope_cell];
	raw_points.push_back ({
	  .position = Vector3D (world_x, y, world_z),
	  .normal = map.normal (x, z),
	  .width = river_width (area) * (water ? 1.35f + 0.55f * mouth_fraction
					     : 1.0f),
	  .distance = distance,
	  .rapid = rapid_signal (slope),
	  .discharge = discharge_signal (area, rivers.minimum_area_m2),
	  .waterfall = rivers.waterfall_by_cell[cell]
	    != terrain::Waterfall::no_id ? 1.0f : 0.0f,
	  .opacity = joins_downstream_reach ? 0.12f
	    : water ? (water_points > 1
		       ? 0.78f * (1.0f - mouth_fraction) : 0.0f)
	    : 1.0f,
	  .water = water
	});
      }
      const std::vector<RibbonPoint> points = smooth_centerline
	(raw_points, map);
      if (points.size () < 2)
	continue;

      draw.begin (render::Prim::QuadStrip);
      for (std::size_t i = 0; i < points.size (); ++i) {
	const Vector3D before = points[i == 0 ? 0 : i - 1].position;
	const Vector3D after = points[i + 1 == points.size () ? i : i + 1]
	  .position;
	Vector3D tangent (after.x - before.x, 0.0f, after.z - before.z);
	if (tangent.length () < 1e-5f)
	  tangent = Vector3D (1, 0, 0);
	else
	  tangent = tangent.normalized ();
	const Vector3D across (-tangent.z, 0.0f, tangent.x);
	const float half_width = 0.5f * points[i].width;
	Vector3D left = points[i].position - across * half_width;
	Vector3D right = points[i].position + across * half_width;
	if (!points[i].water) {
	  left.y = map.interpolated_height (left.x, left.z) + 0.10f;
	  right.y = map.interpolated_height (right.x, right.z) + 0.10f;
	}
	draw.color (points[i].rapid, points[i].discharge,
		    points[i].waterfall, points[i].opacity);
	draw.normal (points[i].normal);
	draw.uv (0.0f, points[i].distance);
	draw.vertex (left);
	draw.uv (1.0f, points[i].distance);
	draw.vertex (right);
      }
      draw.end ();
    }
    return draw;
  }

  void
  RiverSurface::rebuild (render::Renderer& renderer,
			 const map::HeightMap& map,
			 const terrain::FloodField& flood,
			 const terrain::LakeCensus& census,
			 const terrain::DrainageGraph& drainage,
			 const terrain::RiverNetwork& rivers)
  {
    const render::DrawList draw = build_river_ribbons
      (map, flood, census, drainage, rivers);
    m_mesh = renderer.create_mesh (draw);
    m_period = map.size ();
    m_periodic = map.periodic ();
  }

  void
  RiverSurface::clear ()
  {
    m_mesh.reset ();
  }

  void
  RiverSurface::draw (render::Renderer& renderer,
		      const Vector3D& camera) const
  {
    if (!m_mesh)
      return;
    if (!m_periodic) {
      renderer.draw_rivers (*m_mesh, Mat4 ());
      return;
    }
    const float base_x = std::floor (camera.x / m_period.x) * m_period.x;
    const float base_z = std::floor (camera.z / m_period.z) * m_period.z;
    for (int z = -1; z <= 1; ++z)
      for (int x = -1; x <= 1; ++x)
	renderer.draw_rivers
	  (*m_mesh, Mat4::translation
	    (Vector3D (base_x + x * m_period.x, 0.0f,
		       base_z + z * m_period.z)));
  }
}
