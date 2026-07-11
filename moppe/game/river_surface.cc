#include <moppe/game/river_surface.hh>

#include <algorithm>
#include <cmath>
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

    std::vector<RibbonPoint> points;
    for (const terrain::RiverReach& reach : rivers.reaches) {
      if (reach.cells.empty ())
	continue;
      points.clear ();
      std::vector<std::uint32_t> cells = reach.cells;
      const std::uint32_t receiver =
	drainage.receiver[reach.cells.back ()];
      if (receiver != reach.cells.back ())
	cells.push_back (receiver);

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
	const std::uint32_t slope_cell = i + 1 == cells.size () && i > 0
	  ? cells[i - 1] : cell;
	const float slope = drainage.slope.values ()[slope_cell];
	points.push_back ({
	  .position = Vector3D (world_x, y, world_z),
	  .normal = map.normal (x, z),
	  .width = river_width (area),
	  .distance = distance,
	  .rapid = rapid_signal (slope),
	  .discharge = discharge_signal (area, rivers.minimum_area_m2),
	  .waterfall = rivers.waterfall_by_cell[cell]
	    != terrain::Waterfall::no_id ? 1.0f : 0.0f,
	  .water = water
	});
      }
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
		    points[i].waterfall, 1.0f);
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
