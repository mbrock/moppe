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
      // How far up from the root it stands bare, before its first
      // branches, metres; nothing collides with this.
      float clear = 0.0f;
    };

    // The result of pushing a vertical capsule out of nearby trunks.
    struct TrunkContact {
      bool hit = false;
      Vec3 push;   // translation that resolves the penetration
      Vec3 normal; // averaged contact normal, away from the trunks
    };

    // One collision plane between a capsule mover and an obstacle, in the
    // mover's frame: moving by `delta` keeps clear while
    // dot (normal, delta) >= depth, so a positive depth is penetration.
    struct MoverPlane {
      Vec3 normal;
      float depth = 0.0f;
    };

    // The first obstacle met by a sphere swept straight down.
    struct GroundHit {
      bool hit = false;
      Vec3 point;  // metres, the contact on the obstacle
      Vec3 normal; // away from the obstacle
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
      // Appends the planes between a capsule (hemisphere centres relative to
      // `origin`) and the resident obstacles it touches, in a canonical
      // order that does not depend on how cells were streamed in.
      void collide_mover (const Vec3& origin,
                          const Vec3& centre1,
                          const Vec3& centre2,
                          float radius,
                          std::vector<MoverPlane>& planes) const;

      // The fraction of `translation` the capsule can sweep before it meets
      // an obstacle; one when the way is clear.
      [[nodiscard]] float cast_mover (const Vec3& origin,
                                      const Vec3& centre1,
                                      const Vec3& centre2,
                                      float radius,
                                      const Vec3& translation) const;

      // Sweeps a sphere from `centre` down by `distance`. Obstacles the
      // sphere already overlaps are ignored; the mover planes resolve those.
      [[nodiscard]] GroundHit
      cast_down (const Vec3& centre, float radius, float distance) const;

      [[nodiscard]] std::size_t trunk_count () const noexcept;
      [[nodiscard]] std::size_t resident_cells () const noexcept;

    private:
      struct Impl;
      std::unique_ptr<Impl> m_impl;
    };
  }
}

#endif
