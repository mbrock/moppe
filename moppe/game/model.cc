#include <moppe/game/model.hh>

#include <cmath>

namespace moppe {
namespace game {
namespace model {
  void
  box (render::DrawList& dl, float width, float height, float depth)
  {
    dl.push ();
    dl.scale (width, height, depth);
    dl.cube (1.0f);
    dl.pop ();
  }

  void
  ellipsoid (render::DrawList& dl, float rx, float ry, float rz,
	     int slices, int stacks)
  {
    dl.push ();
    dl.scale (rx, ry, rz);
    dl.sphere (1.0f, slices, stacks);
    dl.pop ();
  }

  void
  link (render::DrawList& dl, const Vector3D& start,
	const Vector3D& end, float thickness)
  {
    const Vector3D midpoint = (start + end) * 0.5f;
    const Vector3D direction = end - start;
    const float length = direction.length ();
    const float flat = std::sqrt (direction.x * direction.x
				 + direction.z * direction.z);

    dl.push ();
    dl.translate (midpoint);
    dl.rotate_deg (std::atan2 (direction.x, direction.z) * 57.2958f,
		   0, 1, 0);
    dl.rotate_deg (-std::atan2 (direction.y, flat) * 57.2958f,
		   1, 0, 0);
    box (dl, thickness, thickness, length);
    dl.pop ();
  }

  void
  rider_material (render::DrawList& dl, RiderMaterial material)
  {
    switch (material) {
    case RiderMaterial::Pants:       dl.color (0.13f, 0.14f, 0.19f); break;
    case RiderMaterial::Jersey:      dl.color (0.15f, 0.28f, 0.55f); break;
    case RiderMaterial::Armor:       dl.color (0.90f, 0.91f, 0.94f); break;
    case RiderMaterial::Boots:       dl.color (0.05f, 0.05f, 0.07f); break;
    case RiderMaterial::Gloves:      dl.color (0.08f, 0.08f, 0.10f); break;
    case RiderMaterial::HelmetShell: dl.color (0.15f, 0.45f, 1.0f); break;
    case RiderMaterial::Visor:       dl.color (0.05f, 0.05f, 0.08f); break;
    }
  }

  void
  rider_helmet (render::DrawList& dl)
  {
    rider_material (dl, RiderMaterial::HelmetShell);
    dl.sphere (0.15f, 12, 12);

    dl.push ();
    dl.translate (0, -0.055f, 0.09f);
    box (dl, 0.15f, 0.09f, 0.15f);
    dl.pop ();

    rider_material (dl, RiderMaterial::Visor);
    dl.push ();
    dl.translate (0, 0.03f, 0.115f);
    box (dl, 0.155f, 0.07f, 0.09f);
    dl.pop ();

    rider_material (dl, RiderMaterial::Armor);
    dl.push ();
    dl.translate (0, 0.125f, 0.09f);
    dl.rotate_deg (18, 1, 0, 0);
    box (dl, 0.20f, 0.02f, 0.17f);
    dl.pop ();
  }
}
}
}
