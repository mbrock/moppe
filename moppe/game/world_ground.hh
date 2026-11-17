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

    // The world's period along x and along z, metres.
    float period_x () const;
    float period_z () const;

    // The height of the ground under `at`, metres.
    float height (const Vec3& at) const;

    GroundHeight reader () const {
      return
        [g = *this] (float x, float z) { return g.height (Vec3 (x, 0.0f, z)); };
    }

    // The level of the still water at `at`, metres, and how deep it
    // stands over the ground there.
    float still_level (const Vec3& at) const;
    float depth (const Vec3& at) const {
      return still_level (at) - height (at);
    }

    // Whether a machine `room` metres round could stand at `at`: on dry
    // ground well above any water, no steeper across that than `relief`
    // metres, and with no trunk or boulder within reach.
    bool open (const Vec3& at, float room, float relief) const;

    // The open ground nearest `want`, looked for out to `search` metres
    // round it; `want` itself when there is none.
    Vec3 open_near (const Vec3& want,
                    float room,
                    float relief,
                    float search = 30.0f) const;

    // Whether something `reach` metres round stands on a cart road's
    // tread (or, with `decks`, under a bridge's deck). The forest and
    // the rocks know the roads only through the coarse trail field,
    // which a burn's snags and a floodplain's trees under a bridge slip
    // through: they stood in the road, and stopped whatever rode it.
    bool on_road (const Vec3& at, float reach, bool decks) const;

    // Where a trail cell and a point of the trail's alignment stand.
    Vec3 trail_cell (terrain::CellIndex cell) const;
    Vec3 trail_point (const terrain::TrailAlignmentPoint& point) const;

    // The way the trail leaves its home base, on the level.
    Vec3 trail_heading () const;

    // What the places are planned and built on.
    PlaceGround place_ground () const;
  };
}

#endif
