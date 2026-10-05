#ifndef MOPPE_MOV_TRUNK_FIELD_HH
#define MOPPE_MOV_TRUNK_FIELD_HH

#include <moppe/gfx/math.hh>

#include <cstdint>
#include <memory>
#include <vector>

namespace moppe {
  namespace mov {
    // One tree trunk as a collider: a capsule from its root along its axis.
    struct Trunk {
      Vec3 root;           // metres, on the ground
      Vec3 axis;           // unit growth direction
      float height = 0.0f; // collidable length along the axis, metres
      float radius = 0.0f; // metres
    };

    // The result of pushing a vertical capsule out of nearby trunks.
    struct TrunkContact {
      bool hit = false;
      Vec3 push;   // translation that resolves the penetration
      Vec3 normal; // averaged contact normal, away from the trunks
    };

    // The trunks of a periodic forest as Box3D static capsules. The forest is
    // far too large to keep resident, so the world holds only the trunks in
    // cells near a focus, wrapped toward it, and streams cells as the focus
    // moves. Queries use Box3D's mover planes; callers keep their own
    // dynamics and decide how a contact feels.
    class TrunkField {
    public:
      TrunkField ();
      ~TrunkField ();
      TrunkField (const TrunkField&) = delete;
      TrunkField& operator= (const TrunkField&) = delete;

      // Replaces every trunk. `period` is the world's horizontal period in
      // x and z (zero for no wrap).
      void
      set_trunks (std::vector<Trunk> trunks, float period_x, float period_z);

      // Streams the cells around `centre` into the collision world.
      void focus (const Vec3& centre);

      // Pushes a vertical capsule (bottom and top hemisphere centres, shared
      // radius) out of the resident trunks.
      [[nodiscard]] TrunkContact
      collide (const Vec3& bottom, const Vec3& top, float radius) const;

      // The trunks rooted in the cells within `reach` of `centre`, each moved
      // to its periodic copy nearest the centre, in a stable order. A world
      // that simulates bodies among the trunks takes its own copies from
      // here rather than sharing this field's streamed residency.
      [[nodiscard]] std::vector<Trunk> gather (const Vec3& centre,
                                               float reach) const;

      [[nodiscard]] std::size_t trunk_count () const noexcept;
      [[nodiscard]] std::size_t resident_cells () const noexcept;

    private:
      struct Impl;
      std::unique_ptr<Impl> m_impl;
    };
  }
}

#endif
