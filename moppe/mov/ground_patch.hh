#ifndef MOPPE_MOV_GROUND_PATCH_HH
#define MOPPE_MOV_GROUND_PATCH_HH

#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>

#include <cstdint>

#include <box3d/box3d.h>

namespace moppe::mov {
  // The ground a Box3D vehicle stands on: a square of the height field
  // around it as a static height-field shape, sampled at half the terrain
  // lattice spacing so that every other sample is a lattice node and the
  // facets follow the bilinear surface closely, and the trunks near it as
  // static capsules. The world is far too large to keep resident, so the
  // patch is rebuilt about the vehicle whenever it has strayed from the
  // middle, long before a wheel nears the edge.
  //
  // The patch lives in its vehicle's private world and is built first in
  // it, in one order -- the ground body, its shape, the obstacle body, its
  // capsules -- which Box3D's own ordering follows.
  class GroundPatch {
  public:
    struct Params {
      float ground_friction = 1.0f;
      float obstacle_friction = 0.3f;
      float obstacle_restitution = 0.3f;
      // What the ground and the obstacles are, and what each meets.
      std::uint64_t ground_bit = 0, ground_mask = 0;
      std::uint64_t obstacle_bit = 0, obstacle_mask = 0;
      // Samples along a side, how far from the middle the vehicle may
      // stray, and how far from the middle trunks are gathered, metres.
      int samples = 97;
      float recentre_distance = 24.0f;
      float trunk_reach = 72.0f;
    };

    GroundPatch (const map::SurfaceGeometry& surface, Params params);
    ~GroundPatch ();
    GroundPatch (const GroundPatch&) = delete;
    GroundPatch& operator= (const GroundPatch&) = delete;

    // The patch origin, in samples, that would centre the patch on `p`.
    void origin_for (const Vec3& p, int& column, int& row) const;

    // Builds the patch at an origin in `world`, with `trunks`' obstacles
    // if there are any.
    void build (b3WorldId world, const TrunkField* trunks, int column, int row);
    // Rebuilds it about `p` if that has strayed from the middle.
    void recentre (b3WorldId world, const TrunkField* trunks, const Vec3& p);
    // The world is gone and its bodies with it: lets go of what is left.
    void forget ();

    int column () const noexcept {
      return m_column;
    }
    int row () const noexcept {
      return m_row;
    }
    b3BodyId ground () const noexcept {
      return m_ground;
    }
    // Null where no trunk stands near.
    b3BodyId obstacles () const noexcept {
      return m_obstacles;
    }

  private:
    Vec3 centre () const;
    void destroy ();
    void add_bridges (const b3ShapeDef& ground);

    const map::SurfaceGeometry& m_surface;
    Params m_params;
    float m_dx, m_dz;
    b3BodyId m_ground = b3_nullBodyId;
    b3BodyId m_obstacles = b3_nullBodyId;
    b3HeightFieldData* m_height_field = nullptr;
    int m_column = 0, m_row = 0;
  };
}

#endif
