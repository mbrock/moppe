#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/trunk_field.hh>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>

#include <box3d/box3d.h>

namespace moppe {
  namespace mov {
    namespace {
      constexpr float target_cell_metres = 32.0f;
      // Cells within this reach of the focus are resident; a cell leaves only
      // once it is a further cell away, so a rider on a border does not churn.
      constexpr float resident_reach_metres = 96.0f;

      std::int64_t cell_key (int x, int z) {
        return (static_cast<std::int64_t> (x) << 32) ^
               static_cast<std::uint32_t> (z);
      }

      int wrap (int value, int count) {
        const int r = value % count;
        return r < 0 ? r + count : r;
      }

      bool gather_plane (b3ShapeId,
                         const b3PlaneResult* planes,
                         int count,
                         void* context) {
        auto& out = *static_cast<std::vector<b3CollisionPlane>*> (context);
        for (int i = 0; i < count; ++i)
          out.push_back ({ planes[i].plane, FLT_MAX, 0.0f, true });
        return true;
      }
    }

    struct TrunkField::Impl {
      b3WorldId world = b3_nullWorldId;
      std::vector<Trunk> trunks;
      // Wrapped cell -> indices of the trunks rooted in it.
      std::unordered_map<std::int64_t, std::vector<std::uint32_t>> cells;
      // Unwrapped cell around the focus -> its static body.
      std::unordered_map<std::int64_t, b3BodyId> resident;
      int cells_x = 1, cells_z = 1;
      float cell_x = target_cell_metres, cell_z = target_cell_metres;
      bool wraps = false;

      Impl () {
        b3WorldDef def = b3DefaultWorldDef ();
        def.gravity = { 0.0f, 0.0f, 0.0f };
        world = b3CreateWorld (&def);
      }

      ~Impl () {
        b3DestroyWorld (world);
      }

      void clear_resident () {
        for (const auto& [key, body] : resident)
          b3DestroyBody (body);
        resident.clear ();
      }

      int cell_index (float coordinate, float size) const {
        return static_cast<int> (std::floor (coordinate / size));
      }

      void make_resident (int x, int z) {
        const int wx = wraps ? wrap (x, cells_x) : x;
        const int wz = wraps ? wrap (z, cells_z) : z;
        const auto found = cells.find (cell_key (wx, wz));
        if (found == cells.end ())
          return;
        // The body sits at the unwrapped copy's offset, so its shapes keep the
        // trunks' canonical coordinates.
        b3BodyDef body_def = b3DefaultBodyDef ();
        body_def.type = b3_staticBody;
        body_def.position = { (x - wx) * cell_x, 0.0f, (z - wz) * cell_z };
        const b3BodyId body = b3CreateBody (world, &body_def);
        b3ShapeDef shape_def = b3DefaultShapeDef ();
        for (const std::uint32_t index : found->second) {
          const Trunk& trunk = trunks[index];
          b3Capsule capsule;
          capsule.center1 = to_b3 (trunk.root + trunk.axis * trunk.radius);
          capsule.center2 = to_b3 (trunk.root + trunk.axis * trunk.height);
          capsule.radius = trunk.radius;
          b3CreateCapsuleShape (body, &shape_def, &capsule);
        }
        resident.emplace (cell_key (x, z), body);
      }
    };

    TrunkField::TrunkField () : m_impl (std::make_unique<Impl> ()) {}
    TrunkField::~TrunkField () = default;

    void TrunkField::set_trunks (std::vector<Trunk> trunks,
                                 float period_x,
                                 float period_z) {
      Impl& m = *m_impl;
      m.clear_resident ();
      m.cells.clear ();
      m.trunks = std::move (trunks);
      m.wraps = period_x > 0.0f && period_z > 0.0f;
      if (m.wraps) {
        m.cells_x =
          std::max (1, static_cast<int> (period_x / target_cell_metres));
        m.cells_z =
          std::max (1, static_cast<int> (period_z / target_cell_metres));
        m.cell_x = period_x / m.cells_x;
        m.cell_z = period_z / m.cells_z;
      } else {
        m.cell_x = m.cell_z = target_cell_metres;
      }
      for (std::uint32_t i = 0; i < m.trunks.size (); ++i) {
        const Vec3& root = m.trunks[i].root;
        int x = m.cell_index (root[0], m.cell_x);
        int z = m.cell_index (root[2], m.cell_z);
        if (m.wraps) {
          x = wrap (x, m.cells_x);
          z = wrap (z, m.cells_z);
        }
        m.cells[cell_key (x, z)].push_back (i);
      }
    }

    void TrunkField::focus (const Vec3& centre) {
      Impl& m = *m_impl;
      if (m.trunks.empty ())
        return;
      const int cx = m.cell_index (centre[0], m.cell_x);
      const int cz = m.cell_index (centre[2], m.cell_z);
      const int reach_x =
        static_cast<int> (std::ceil (resident_reach_metres / m.cell_x));
      const int reach_z =
        static_cast<int> (std::ceil (resident_reach_metres / m.cell_z));
      for (auto it = m.resident.begin (); it != m.resident.end ();) {
        const int x = static_cast<int> (it->first >> 32);
        const int z = static_cast<std::int32_t> (it->first & 0xffffffff);
        if (std::abs (x - cx) > reach_x + 1 ||
            std::abs (z - cz) > reach_z + 1) {
          b3DestroyBody (it->second);
          it = m.resident.erase (it);
        } else {
          ++it;
        }
      }
      for (int z = cz - reach_z; z <= cz + reach_z; ++z)
        for (int x = cx - reach_x; x <= cx + reach_x; ++x)
          if (!m.resident.contains (cell_key (x, z)))
            m.make_resident (x, z);
    }

    TrunkContact TrunkField::collide (const Vec3& bottom,
                                      const Vec3& top,
                                      float radius) const {
      TrunkContact contact;
      if (m_impl->resident.empty ())
        return contact;
      b3Capsule mover;
      mover.center1 = { 0.0f, 0.0f, 0.0f };
      mover.center2 = to_b3 (top - bottom);
      mover.radius = radius;
      std::vector<b3CollisionPlane> planes;
      b3World_CollideMover (m_impl->world,
                            to_b3 (bottom),
                            &mover,
                            b3DefaultQueryFilter (),
                            gather_plane,
                            &planes);
      if (planes.empty ())
        return contact;
      const b3PlaneSolverResult solved = b3SolvePlanes (
        b3Vec3 { 0.0f, 0.0f, 0.0f }, planes.data (), (int)planes.size ());
      Vec3 normal (0, 0, 0);
      for (const b3CollisionPlane& plane : planes)
        if (plane.push > 0.0f)
          normal += from_b3 (plane.plane.normal) * plane.push;
      if (length2 (normal) <= 0.0f)
        return contact;
      contact.hit = true;
      contact.push = from_b3 (solved.delta);
      contact.normal = normalized (normal);
      return contact;
    }

    std::vector<Trunk> TrunkField::gather (const Vec3& centre,
                                           float reach) const {
      const Impl& m = *m_impl;
      std::vector<Trunk> gathered;
      if (m.trunks.empty ())
        return gathered;
      const int x0 = m.cell_index (centre[0] - reach, m.cell_x);
      const int x1 = m.cell_index (centre[0] + reach, m.cell_x);
      const int z0 = m.cell_index (centre[2] - reach, m.cell_z);
      const int z1 = m.cell_index (centre[2] + reach, m.cell_z);
      for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
          const int wx = m.wraps ? wrap (x, m.cells_x) : x;
          const int wz = m.wraps ? wrap (z, m.cells_z) : z;
          const auto found = m.cells.find (cell_key (wx, wz));
          if (found == m.cells.end ())
            continue;
          const Vec3 offset ((x - wx) * m.cell_x, 0.0f, (z - wz) * m.cell_z);
          for (const std::uint32_t index : found->second) {
            Trunk trunk = m.trunks[index];
            trunk.root += offset;
            gathered.push_back (trunk);
          }
        }
      return gathered;
    }

    void TrunkField::collide_mover (const Vec3& origin,
                                    const Vec3& centre1,
                                    const Vec3& centre2,
                                    float radius,
                                    std::vector<MoverPlane>& planes) const {
      if (m_impl->resident.empty ())
        return;
      b3Capsule mover;
      mover.center1 = to_b3 (centre1);
      mover.center2 = to_b3 (centre2);
      mover.radius = radius;
      std::vector<b3CollisionPlane> found;
      b3World_CollideMover (m_impl->world,
                            to_b3 (origin),
                            &mover,
                            b3DefaultQueryFilter (),
                            gather_plane,
                            &found);
      const std::size_t first = planes.size ();
      for (const b3CollisionPlane& plane : found)
        planes.push_back ({ from_b3 (plane.plane.normal), plane.plane.offset });
      // The broadphase reports shapes in tree order, which depends on the
      // streaming history; the plane solver is order-sensitive, so a replay
      // must see the same planes in the same order.
      std::sort (planes.begin () + first,
                 planes.end (),
                 [] (const MoverPlane& a, const MoverPlane& b) {
                   if (a.normal[0] != b.normal[0])
                     return a.normal[0] < b.normal[0];
                   if (a.normal[2] != b.normal[2])
                     return a.normal[2] < b.normal[2];
                   if (a.normal[1] != b.normal[1])
                     return a.normal[1] < b.normal[1];
                   return a.depth < b.depth;
                 });
    }

    float TrunkField::cast_mover (const Vec3& origin,
                                  const Vec3& centre1,
                                  const Vec3& centre2,
                                  float radius,
                                  const Vec3& translation) const {
      if (m_impl->resident.empty ())
        return 1.0f;
      b3Capsule mover;
      mover.center1 = to_b3 (centre1);
      mover.center2 = to_b3 (centre2);
      mover.radius = radius;
      return b3World_CastMover (m_impl->world,
                                to_b3 (origin),
                                &mover,
                                to_b3 (translation),
                                b3DefaultQueryFilter (),
                                nullptr,
                                nullptr);
    }

    GroundHit TrunkField::cast_down (const Vec3& centre,
                                     float radius,
                                     float distance) const {
      struct Closest {
        GroundHit hit;
        float fraction = 2.0f;
      } closest;
      if (m_impl->resident.empty ())
        return closest.hit;
      const b3Vec3 point { 0.0f, 0.0f, 0.0f };
      b3ShapeProxy proxy {};
      proxy.points = &point;
      proxy.count = 1;
      proxy.radius = radius;
      const auto keep = [] (b3ShapeId,
                            b3Pos at,
                            b3Vec3 normal,
                            float fraction,
                            uint64_t,
                            int,
                            int,
                            void* context) -> float {
        // A zero fraction is an initial overlap: no floor to stand on.
        if (fraction <= 0.0f)
          return -1.0f;
        auto& best = *static_cast<Closest*> (context);
        if (fraction < best.fraction) {
          best.fraction = fraction;
          best.hit = { true, from_b3 (at), from_b3 (normal) };
        }
        return fraction;
      };
      b3World_CastShape (m_impl->world,
                         to_b3 (centre),
                         &proxy,
                         b3Vec3 { 0.0f, -distance, 0.0f },
                         b3DefaultQueryFilter (),
                         keep,
                         &closest);
      return closest.hit;
    }

    std::size_t TrunkField::trunk_count () const noexcept {
      return m_impl->trunks.size ();
    }

    std::size_t TrunkField::resident_cells () const noexcept {
      return m_impl->resident.size ();
    }
  }
}
