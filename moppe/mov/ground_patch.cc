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
