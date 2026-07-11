#ifndef MOPPE_GAME_MODEL_HH
#define MOPPE_GAME_MODEL_HH

#include <moppe/render/draw.hh>

namespace moppe {
  namespace game {
    namespace model {
      // Small, renderer-independent vocabulary shared by procedural models.
      // Dimensions are full extents for boxes and radii for ellipsoids.
      void box (render::DrawList& dl, float width, float height, float depth);
      void ellipsoid (render::DrawList& dl,
                      float rx,
                      float ry,
                      float rz,
                      int slices = 16,
                      int stacks = 16);
      void link (render::DrawList& dl,
                 const Vector3D& start,
                 const Vector3D& end,
                 float thickness);

      enum class RiderMaterial {
        Pants,
        Jersey,
        Armor,
        Boots,
        Gloves,
        HelmetShell,
        Visor
      };

      void rider_material (render::DrawList& dl, RiderMaterial material);
      void rider_helmet (render::DrawList& dl);
    }
  }
}

#endif
