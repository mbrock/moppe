#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/ground_patch.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace moppe::mov {
  namespace {
    const map::SurfaceGeometry* roads_surface = nullptr;
    const terrain::RoadSurface* the_roads = nullptr;
  }

  void attach_roads (const map::SurfaceGeometry& surface,
                     const terrain::RoadSurface* roads) {
    roads_surface = roads ? &surface : nullptr;
    the_roads = roads;
  }

  const terrain::RoadSurface*
  attached_roads (const map::SurfaceGeometry& surface) {
    return &surface == roads_surface ? the_roads : nullptr;
  }

  float ground_height (const map::SurfaceGeometry& surface, float x, float z) {
    const float field = terrain::surface_elevation_value (
      spatial::sample<terrain::surface_elevation> (
        surface, moppe::position (Vec3 (x, 0.0f, z))));
    if (&surface != roads_surface)
      return field;
    return terrain::surface_elevation_value (
      the_roads->ground (moppe::plan_position (x, z),
                         terrain::surface_elevation_point (field * u::m)));
  }

  Vec3 ground_normal (const map::SurfaceGeometry& surface, float x, float z) {
    Vec3 n = spatial::sample<terrain::terrain_normal> (
               surface, moppe::position (Vec3 (x, 0.0f, z)))
               .numerical_value_in (one);
    if (&surface == roads_surface) {
      // Where a road's tread lies over any of the points round it, the
      // ground's slope there is the road's, taken across them.
      constexpr float h = 0.25f;
      const auto field = [&] (float px, float pz) {
        return terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            surface, moppe::position (Vec3 (px, 0.0f, pz))));
      };
      const float east = ground_height (surface, x + h, z);
      const float west = ground_height (surface, x - h, z);
      const float south = ground_height (surface, x, z + h);
      const float north = ground_height (surface, x, z - h);
      if (east != field (x + h, z) || west != field (x - h, z) ||
          south != field (x, z + h) || north != field (x, z - h))
        n =
          Vec3 ((west - east) / (2.0f * h), 1.0f, (north - south) / (2.0f * h));
    }
    return length2 (n) > 1e-8f ? normalized (n) : Vec3 (0, 1, 0);
  }

  GroundPatch::GroundPatch (const map::SurfaceGeometry& surface, Params params)
      : m_surface (surface), m_params (params),
        m_dx (0.5f * surface.domain ().spacing_x ()),
        m_dz (0.5f * surface.domain ().spacing_z ()) {}

  GroundPatch::~GroundPatch () {
    forget ();
  }

  void GroundPatch::origin_for (const Vec3& p, int& column, int& row) const {
    const int half = (m_params.samples - 1) / 2;
    column =
      static_cast<int> (std::floor (scalar_value (p[0] * u::m / m_dx))) - half;
    row =
      static_cast<int> (std::floor (scalar_value (p[2] * u::m / m_dz))) - half;
  }

  plan_position_t GroundPatch::corner () const {
    return plan_position (m_column * m_dx, m_row * m_dz);
  }

  plan_position_t GroundPatch::centre () const {
    const float half = 0.5f * (m_params.samples - 1);
    return plan_position ((m_column + half) * m_dx, (m_row + half) * m_dz);
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
        const float h = surface_elevation_at (
          m_surface,
          meters_value (static_cast<float> (column + i) * m_dx),
          meters_value (static_cast<float> (row + j) * m_dz));
        heights[j * n + i] = h;
        lowest = std::min (lowest, h);
        highest = std::max (highest, h);
      }
    // Quantization spans only this patch, so the height steps stay at a
    // millimetre or so even on a mountainside.
    b3HeightFieldDef field {};
    field.heights = heights.data ();
    field.scale = { meters_value (m_dx), 1.0f, meters_value (m_dz) };
    field.countX = n;
    field.countZ = n;
    field.globalMinimumHeight = lowest - 1.0f;
    field.globalMaximumHeight = highest + 1.0f;
    m_height_field = b3CreateHeightField (&field);

    b3BodyDef body = b3DefaultBodyDef ();
    body.type = b3_staticBody;
    body.position = to_b3 (lift (plan_value (corner ())));
    m_ground = b3CreateBody (world, &body);
    b3ShapeDef shape = b3DefaultShapeDef ();
    shape.baseMaterial.friction =
      m_params.ground_friction.numerical_value_in (one);
    shape.filter.categoryBits = m_params.ground_bit;
    shape.filter.maskBits = m_params.ground_mask;
    b3CreateHeightFieldShape (m_ground, &shape, m_height_field);
    // The obstacles are trunks and railings: the body that holds them,
    // at the world's origin, is made only where some stand, after the
    // ground, in the same order every time.
    const auto obstacles = [&] {
      if (B3_IS_NULL (m_obstacles)) {
        b3BodyDef obstacle_body = b3DefaultBodyDef ();
        obstacle_body.type = b3_staticBody;
        m_obstacles = b3CreateBody (world, &obstacle_body);
      }
      return m_obstacles;
    };
    add_bridges (shape, obstacles);

    if (!trunks)
      return;
    const std::vector<Trunk> nearby =
      trunks->gather (at_height (centre (), 0.0f * u::m), m_params.trunk_reach);
    if (nearby.empty ())
      return;
    obstacles ();
    b3ShapeDef obstacle = b3DefaultShapeDef ();
    obstacle.baseMaterial.friction =
      m_params.obstacle_friction.numerical_value_in (one);
    obstacle.baseMaterial.restitution = m_params.obstacle_restitution;
    obstacle.filter.categoryBits = m_params.obstacle_bit;
    obstacle.filter.maskBits = m_params.obstacle_mask;
    for (const Trunk& trunk : nearby) {
      b3Capsule capsule;
      capsule.center1 = to_b3 (position_value (trunk.point (trunk.radius)));
      capsule.center2 = to_b3 (position_value (trunk.point (trunk.height)));
      capsule.radius = meters_value (trunk.radius);
      b3CreateCapsuleShape (m_obstacles, &obstacle, &capsule);
    }
  }

  // A bridge's deck is a solid slab and its railings are walls: the height
  // field, a sample every metre or so, would round a deck a few samples
  // wide off into slopes down to the river, and has no railings at all.
  void GroundPatch::add_bridges (const b3ShapeDef& ground,
                                 const std::function<b3BodyId ()>& obstacles) {
    if (&m_surface != roads_surface || the_roads->empty ())
      return;
    const float period_x =
      m_surface.domain ().period_x ().numerical_value_in (u::m);
    const float period_z =
      m_surface.domain ().period_z ().numerical_value_in (u::m);
    const Vec3 origin = lift (plan_value (corner ()));
    const Vec3 middle = lift (plan_value (centre ()));
    const meters_t reach =
      0.5f * (m_params.samples - 1) * std::max (m_dx, m_dz);
    const float half = meters_value (the_roads->formation ().deck_half_width);
    b3ShapeDef rail = ground;
    rail.filter.categoryBits = m_params.obstacle_bit;
    rail.filter.maskBits = m_params.obstacle_mask;
    rail.baseMaterial.friction =
      m_params.obstacle_friction.numerical_value_in (one);
    rail.baseMaterial.restitution = 0.1f;
    const auto rows = the_roads->segment_rows ();
    constexpr std::size_t stride = terrain::RoadSurface::rows_per_segment;
    for (std::size_t s = 0; stride * s + 1 < rows.size (); ++s) {
      const auto& [ax, az, bx, bz] = rows[stride * s];
      const auto& [ha, hb, along, kind] = rows[stride * s + 1];
      if (kind != 2.0f)
        continue;
      // The segment's image nearest the patch.
      const float mx = 0.5f * (ax + bx), mz = 0.5f * (az + bz);
      const float ox = terrain::whole_periods (middle[0] - mx, period_x);
      const float oz = terrain::whole_periods (middle[2] - mz, period_z);
      const Vec3 a (ax + ox, ha, az + oz), b (bx + ox, hb, bz + oz);
      const float length = moppe::length (b - a);
      if (length < 1e-3f || std::max (std::abs (mx + ox - middle[0]),
                                      std::abs (mz + oz - middle[2])) >
                              meters_value (reach) + length)
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
          { to_b3 (origin + mid + y * 0.55f + z * (side * (half + 0.3f))),
            turn });
        b3CreateHullShape (obstacles (), &rail, &wall.base);
      }
    }
    // A guardrail is a wall from below the road's edge to the beam's top,
    // its face at the beam's, slippery so that a car glances along it.
    b3ShapeDef guard = rail;
    guard.baseMaterial.friction = 0.15f;
    guard.baseMaterial.restitution = 0.05f;
    constexpr float depth = 0.083f, thick = 0.1f;
    const float top = 0.16f;
    const float bottom =
      -meters_value (terrain::RoadSurface::guardrail_height) - 0.4f;
    for (const terrain::Guardrail& run : the_roads->guardrails ())
      for (std::size_t i = 1; i < run.posts.size (); ++i) {
        const terrain::Guardrail::Post& p = run.posts[i - 1];
        const terrain::Guardrail::Post& q = run.posts[i];
        const plan_position_t& pa = p.at;
        const plan_position_t& qa = q.at;
        const float mx = 0.5f * (plan_value (pa).x () + plan_value (qa).x ());
        const float mz = 0.5f * (plan_value (pa).z () + plan_value (qa).z ());
        const float ox = terrain::whole_periods (middle[0] - mx, period_x);
        const float oz = terrain::whole_periods (middle[2] - mz, period_z);
        const Vec3 a (plan_value (pa).x () + ox,
                      terrain::surface_elevation_value (p.rail),
                      plan_value (pa).z () + oz);
        const Vec3 b (plan_value (qa).x () + ox,
                      terrain::surface_elevation_value (q.rail),
                      plan_value (qa).z () + oz);
        const float length = moppe::length (b - a);
        if (length < 1e-3f || std::max (std::abs (mx + ox - middle[0]),
                                        std::abs (mz + oz - middle[2])) >
                                meters_value (reach) + length)
          continue;
        const Vec3 x = (b - a) / length;
        Vec3 z = normalized (cross (x, Vec3 (0.0f, 1.0f, 0.0f)));
        const plan_direction_t& po = p.out;
        const plan_direction_t& qo = q.out;
        if (z[0] * (plan_value (po).x () + plan_value (qo).x ()) +
              z[2] * (plan_value (po).z () + plan_value (qo).z ()) <
            0.0f)
          z = -z;
        const Vec3 y = cross (z, x);
        const b3Matrix3 frame { to_b3 (x), to_b3 (y), to_b3 (z) };
        const Vec3 mid =
          (a + b) * 0.5f + y * (0.5f * (top + bottom)) + z * (thick - depth);
        const b3BoxHull wall = b3MakeTransformedBoxHull (
          0.5f * length + 0.05f,
          0.5f * (top - bottom),
          thick,
          { to_b3 (mid), b3MakeQuatFromMatrix (&frame) });
        b3CreateHullShape (obstacles (), &guard, &wall.base);
      }
  }

  void GroundPatch::recentre (b3WorldId world,
                              const TrunkField* trunks,
                              const Vec3& p) {
    const plan_displacement_t off = plan_position (plan (p)) - centre ();
    if (abs (x_of (off)) < m_params.recentre_distance &&
        abs (z_of (off)) < m_params.recentre_distance)
      return;
    int column = 0, row = 0;
    origin_for (p, column, row);
    destroy ();
    build (world, trunks, column, row);
  }
}
