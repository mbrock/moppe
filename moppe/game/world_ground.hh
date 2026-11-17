// The active world as what stands on it reads it: the height of its
// ground, its still water, its trail and roads, and the obstacles in it.
// A view, made fresh each time from the world MoppeGame holds, so that
// what parks machines, plans the places, and drives the autopilots reads
// the world without reaching through the game.
#ifndef MOPPE_GAME_WORLD_GROUND_HH
#define MOPPE_GAME_WORLD_GROUND_HH

#include <moppe/game/camp.hh>
#include <moppe/game/generated_world.hh>
#include <moppe/game/places.hh>
#include <moppe/game/world.hh>
#include <moppe/mov/trunk_field.hh>

namespace moppe::game {
  struct WorldGround {
    const WorldParams& world;
    const GeneratedWorld& generated;
    const mov::TrunkField& trunks;

    const map::SurfaceGeometry& surface () const noexcept {
      return generated.surface ();
    }

    // The world's period, the torus's extent in plan
    // (moppe/terrain/torus.hh).
    plan_extent_t period () const;

    // The ground's elevation under `at`.
    terrain::SurfaceElevation elevation (const plan_position_t& at) const;

    // The ground's elevation everywhere, for what plans on it.
    terrain::ElevationField elevation_field () const {
      return
        [g = *this] (const plan_position_t& at) { return g.elevation (at); };
    }

    // The surface of the still water over `at` (the sea's where none
    // stands there), and how deep it stands over the ground: below the
    // ground, negative.
    terrain::SurfaceElevation still_water (const plan_position_t& at) const;
    terrain::ElevationChange water_depth (const plan_position_t& at) const {
      return still_water (at) - elevation (at);
    }

    // Whether a machine `room` round could stand at `at`: on dry ground
    // well above any water, no steeper across that than `relief`, and with
    // no trunk or boulder within reach.
    bool open (const plan_position_t& at, meters_t room, meters_t relief) const;

    // The open ground nearest `want`, looked for out to `search` round
    // it; `want` itself when there is none.
    plan_position_t open_near (const plan_position_t& want,
                               meters_t room,
                               meters_t relief,
                               meters_t search = 30.0f * u::m) const;

    // Whether something `reach` round stands on a cart road's tread (or,
    // with `decks`, under a bridge's deck). The forest and the rocks know
    // the roads only through the coarse trail field, which a burn's snags
    // and a floodplain's trees under a bridge slip through: they stood in
    // the road, and stopped whatever rode it.
    bool on_road (const plan_position_t& at, meters_t reach, bool decks) const;

    // ---- Untyped adapters ----
    //
    // The queries above in floats and Vec3s (heights in metres, `at`'s y
    // ignored), for callers not yet typed; each forwards to its typed
    // query. Remove one when its last caller has moved
    // (docs/typed-quantities.md).

    float period_x () const {
      return plan_value (period ()).x ();
    }
    float period_z () const {
      return plan_value (period ()).z ();
    }
    float height (const Vec3& at) const {
      return terrain::surface_elevation_value (elevation (plan_of (at)));
    }
    GroundHeight reader () const {
      return
        [g = *this] (float x, float z) { return g.height (Vec3 (x, 0.0f, z)); };
    }
    float still_level (const Vec3& at) const {
      return terrain::surface_elevation_value (still_water (plan_of (at)));
    }
    float depth (const Vec3& at) const {
      return still_level (at) - height (at);
    }
    bool open (const Vec3& at, float room, float relief) const {
      return open (plan_of (at), room * u::m, relief * u::m);
    }
    // Keeps `want`'s height.
    Vec3 open_near (const Vec3& want,
                    float room,
                    float relief,
                    float search = 30.0f) const {
      return lift (
        plan_value (open_near (
          plan_of (want), room * u::m, relief * u::m, search * u::m)),
        want[1]);
    }
    bool on_road (const Vec3& at, float reach, bool decks) const {
      return on_road (plan_of (at), reach * u::m, decks);
    }

    // Where a trail cell and a point of the trail's alignment stand.
    Vec3 trail_cell (terrain::CellIndex cell) const;
    Vec3 trail_point (const plan_position_t& point) const;

    // The way the trail leaves its home base, on the level.
    Vec3 trail_heading () const;

    // What the places are planned and built on.
    PlaceGround place_ground () const;

  private:
    static plan_position_t plan_of (const Vec3& at) {
      return plan_position (at[0], at[2]);
    }
  };
}

#endif
