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
      position_t root {};  // on the ground
      direction_t axis {}; // the way it grows
      meters_t height {};  // collidable length along the axis
      meters_t radius {};
      // How far up from the root it stands bare, before its first
      // branches; nothing collides with this.
      meters_t clear {};

      // The point `up` along the axis from the root.
      position_t point (meters_t up) const {
        return root + quantity_cast<isq::position_vector> (along (axis, up));
      }
    };

    // The result of pushing a vertical capsule out of nearby trunks.
    struct TrunkContact {
      bool hit = false;
      displacement_t push {}; // resolves the penetration
      direction_t normal {};  // averaged contact normal, away from the trunks
    };

    // One collision plane between a capsule mover and an obstacle, in the
    // mover's frame: moving by `delta` keeps clear while
    // dot (normal, delta) >= depth, so a positive depth is penetration.
    struct MoverPlane {
      direction_t normal {};
      meters_t depth {};
    };

    // The first obstacle met by a sphere swept straight down.
    struct GroundHit {
      bool hit = false;
      position_t point {};   // the contact on the obstacle
      direction_t normal {}; // away from the obstacle
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
      void set_trunks (std::vector<Trunk> trunks, const plan_extent_t& period);

      // Streams the cells around `centre` into the collision world.
      void focus (const position_t& centre);

      // Pushes a vertical capsule (bottom and top hemisphere centres, shared
      // radius) out of the resident trunks.
      [[nodiscard]] TrunkContact collide (const position_t& bottom,
                                          const position_t& top,
                                          meters_t radius) const;

      // The trunks rooted in the cells within `reach` of `centre`, each moved
      // to its periodic copy nearest the centre, in a stable order. A world
      // that simulates bodies among the trunks takes its own copies from
      // here rather than sharing this field's streamed residency.
      [[nodiscard]] std::vector<Trunk> gather (const position_t& centre,
                                               meters_t reach) const;
      // Appends the planes between a capsule (hemisphere centres relative to
      // `origin`) and the resident obstacles it touches, in a canonical
      // order that does not depend on how cells were streamed in.
      void collide_mover (const position_t& origin,
                          const displacement_t& centre1,
                          const displacement_t& centre2,
                          meters_t radius,
                          std::vector<MoverPlane>& planes) const;

      // The fraction of `translation` the capsule can sweep before it meets
      // an obstacle; one when the way is clear.
      [[nodiscard]] proportion_t
      cast_mover (const position_t& origin,
                  const displacement_t& centre1,
                  const displacement_t& centre2,
                  meters_t radius,
                  const displacement_t& translation) const;

      // Sweeps a sphere from `centre` down by `distance`. Obstacles the
      // sphere already overlaps are ignored; the mover planes resolve those.
      [[nodiscard]] GroundHit cast_down (const position_t& centre,
                                         meters_t radius,
                                         meters_t distance) const;

      [[nodiscard]] std::size_t trunk_count () const noexcept;
      [[nodiscard]] std::size_t resident_cells () const noexcept;

    private:
      struct Impl;
      std::unique_ptr<Impl> m_impl;
    };
  }
}

#endif
