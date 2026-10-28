#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/ground_patch.hh>

#include <algorithm>
#include <cmath>
#include <vector>

namespace moppe::mov {
  namespace {
    const map::SurfaceGeometry* roads_surface = nullptr;
    const terrain::RoadSurface* attached_roads = nullptr;
  }

  void attach_roads (const map::SurfaceGeometry& surface,
                     const terrain::RoadSurface* roads) {
    roads_surface = roads ? &surface : nullptr;
    attached_roads = roads;
  }

  float ground_height (const map::SurfaceGeometry& surface, float x, float z) {
    const float field = terrain::surface_elevation_value (
      spatial::sample<terrain::surface_elevation> (
        surface, moppe::position (Vec3 (x, 0.0f, z))));
    return &surface == roads_surface ? attached_roads->ground (x, z, field)
                                     : field;
  }

  GroundPatch::GroundPatch (const map::SurfaceGeometry& surface, Params params)
      : m_surface (surface), m_params (params),
        m_dx (0.5f * surface.domain ().spacing_x ().numerical_value_in (u::m)),
        m_dz (0.5f * surface.domain ().spacing_z ().numerical_value_in (u::m)) {
  }

  GroundPatch::~GroundPatch () {
    forget ();
  }

  void GroundPatch::origin_for (const Vec3& p, int& column, int& row) const {
    const int half = (m_params.samples - 1) / 2;
    column = static_cast<int> (std::floor (p[0] / m_dx)) - half;
    row = static_cast<int> (std::floor (p[2] / m_dz)) - half;
  }

  Vec3 GroundPatch::centre () const {
    const float half = 0.5f * (m_params.samples - 1);
    return Vec3 ((m_column + half) * m_dx, 0.0f, (m_row + half) * m_dz);
  }

  void GroundPatch::forget () {
    m_obstacles = m_ground = b3_nullBodyId;
    if (m_height_field)
      b3DestroyHeightField (m_height_field);
    m_height_field = nullptr;
  }

  void GroundPatch::destroy () {
    if (B3_IS_NON_NULL (m_obstacles))
      b3DestroyBody (m_obstacles);
    if (B3_IS_NON_NULL (m_ground))
      b3DestroyBody (m_ground);
    forget ();
  }

  void GroundPatch::build (b3WorldId world,
                           const TrunkField* trunks,
                           int column,
                           int row) {
    const int n = m_params.samples;
    m_column = column;
    m_row = row;
    std::vector<float> heights (n * n);
    float lowest = 1e30f, highest = -1e30f;
    for (int j = 0; j < n; ++j)
      for (int i = 0; i < n; ++i) {
        const float h =
          surface_elevation_at (m_surface,
                                static_cast<float> (column + i) * m_dx,
                                static_cast<float> (row + j) * m_dz);
        heights[j * n + i] = h;
        lowest = std::min (lowest, h);
        highest = std::max (highest, h);
      }
    // Quantization spans only this patch, so the height steps stay at a
    // millimetre or so even on a mountainside.
    b3HeightFieldDef field {};
    field.heights = heights.data ();
    field.scale = { m_dx, 1.0f, m_dz };
    field.countX = n;
    field.countZ = n;
    field.globalMinimumHeight = lowest - 1.0f;
    field.globalMaximumHeight = highest + 1.0f;
    m_height_field = b3CreateHeightField (&field);

    b3BodyDef body = b3DefaultBodyDef ();
    body.type = b3_staticBody;
    body.position = { column * m_dx, 0.0f, row * m_dz };
    m_ground = b3CreateBody (world, &body);
    b3ShapeDef shape = b3DefaultShapeDef ();
    shape.baseMaterial.friction = m_params.ground_friction;
    shape.filter.categoryBits = m_params.ground_bit;
    shape.filter.maskBits = m_params.ground_mask;
    b3CreateHeightFieldShape (m_ground, &shape, m_height_field);
    add_bridges (shape);

    if (!trunks)
      return;
    const std::vector<Trunk> nearby =
      trunks->gather (centre (), m_params.trunk_reach);
    if (nearby.empty ())
      return;
    b3BodyDef obstacle_body = b3DefaultBodyDef ();
    obstacle_body.type = b3_staticBody;
    m_obstacles = b3CreateBody (world, &obstacle_body);
    b3ShapeDef obstacle = b3DefaultShapeDef ();
    obstacle.baseMaterial.friction = m_params.obstacle_friction;
    obstacle.baseMaterial.restitution = m_params.obstacle_restitution;
    obstacle.filter.categoryBits = m_params.obstacle_bit;
    obstacle.filter.maskBits = m_params.obstacle_mask;
    for (const Trunk& trunk : nearby) {
      b3Capsule capsule;
      capsule.center1 = to_b3 (trunk.root + trunk.axis * trunk.radius);
      capsule.center2 = to_b3 (trunk.root + trunk.axis * trunk.height);
      capsule.radius = trunk.radius;
      b3CreateCapsuleShape (m_obstacles, &obstacle, &capsule);
    }
  }

  // A bridge's deck is a solid slab and its railings are walls: the height
  // field, a sample every metre or so, would round a deck a few samples
  // wide off into slopes down to the river, and has no railings at all.
  void GroundPatch::add_bridges (const b3ShapeDef& ground) {
    if (&m_surface != roads_surface || attached_roads->empty ())
      return;
    const float period_x =
      m_surface.domain ().period_x ().numerical_value_in (u::m);
    const float period_z =
      m_surface.domain ().period_z ().numerical_value_in (u::m);
    const Vec3 origin (m_column * m_dx, 0.0f, m_row * m_dz);
    const Vec3 middle = centre ();
    const float reach = 0.5f * (m_params.samples - 1) * std::max (m_dx, m_dz);
    const float half = attached_roads->formation ().deck_half_width;
    b3ShapeDef rail = ground;
    rail.filter.categoryBits = m_params.obstacle_bit;
    rail.filter.maskBits = m_params.obstacle_mask;
    rail.baseMaterial.friction = m_params.obstacle_friction;
    rail.baseMaterial.restitution = 0.1f;
    const auto rows = attached_roads->segment_rows ();
    for (std::size_t s = 0; 2 * s + 1 < rows.size (); ++s) {
      const auto& [ax, az, bx, bz] = rows[2 * s];
      const auto& [ha, hb, along, kind] = rows[2 * s + 1];
      if (kind != 2.0f)
        continue;
      // The segment's image nearest the patch.
      const float mx = 0.5f * (ax + bx), mz = 0.5f * (az + bz);
      const float ox = period_x * std::round ((middle[0] - mx) / period_x);
      const float oz = period_z * std::round ((middle[2] - mz) / period_z);
      const Vec3 a (ax + ox, ha, az + oz), b (bx + ox, hb, bz + oz);
      const float length = moppe::length (b - a);
      if (length < 1e-3f ||
          std::max (std::abs (mx + ox - middle[0]),
                    std::abs (mz + oz - middle[2])) > reach + length)
        continue;
      const Vec3 x = (b - a) / length;
      const Vec3 z = normalized (cross (x, Vec3 (0.0f, 1.0f, 0.0f)));
      const Vec3 y = cross (z, x);
      const b3Matrix3 frame { to_b3 (x), to_b3 (y), to_b3 (z) };
      const b3Quat turn = b3MakeQuatFromMatrix (&frame);
      const Vec3 mid = (a + b) * 0.5f - origin;
      // Overlapping its neighbours a little, so no seam opens at a bend.
      const float hl = 0.5f * length + 0.35f;
      const b3BoxHull deck = b3MakeTransformedBoxHull (
        hl, 0.25f, half + 0.12f, { to_b3 (mid - y * 0.25f), turn });
      b3CreateHullShape (m_ground, &ground, &deck.base);
      for (const float side : { -1.0f, 1.0f }) {
        const b3BoxHull wall = b3MakeTransformedBoxHull (
          hl,
          0.55f,
          0.08f,
          { to_b3 (mid + y * 0.55f + z * (side * (half + 0.3f))), turn });
        b3CreateHullShape (m_ground, &rail, &wall.base);
      }
    }
  }

  void GroundPatch::recentre (b3WorldId world,
                              const TrunkField* trunks,
                              const Vec3& p) {
    const Vec3 middle = centre ();
    if (std::abs (p[0] - middle[0]) < m_params.recentre_distance &&
        std::abs (p[2] - middle[2]) < m_params.recentre_distance)
      return;
    int column = 0, row = 0;
    origin_for (p, column, row);
    destroy ();
    build (world, trunks, column, row);
  }
}
