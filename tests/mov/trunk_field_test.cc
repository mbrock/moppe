#include <moppe/mov/trunk_field.hh>

#include <tests/test.hh>

using namespace moppe;

namespace {
  mov::Trunk upright (float x, float z, meters_t radius) {
    return { position (Vec3 (x, 0.0f, z)),
             unit_direction (Vec3 (0.0f, 1.0f, 0.0f)),
             12.0f * u::m,
             radius };
  }

  // A capsule of radius 0.45 m standing from 1 m to 2 m at (x, z).
  mov::TrunkContact probe (const mov::TrunkField& field, float x, float z) {
    return field.collide (
      position (Vec3 (x, 1.0f, z)), position (Vec3 (x, 2.0f, z)), 0.45f * u::m);
  }
}

MOPPE_TEST (trunk_field_pushes_a_capsule_out_of_a_trunk) {
  mov::TrunkField field;
  std::vector<mov::Trunk> trunks { upright (0.0f, 0.0f, 0.3f * u::m) };
  field.set_trunks (std::move (trunks), plan_extent (0.0f * u::m, 0.0f * u::m));
  field.focus (position (Vec3 (0.0f, 0.0f, 0.0f)));
  MOPPE_CHECK (field.resident_cells () > 0);

  // Centres 0.5 m apart with radii 0.3 + 0.45: 0.25 m of overlap along +x.
  const mov::TrunkContact contact = probe (field, 0.5f, 0.0f);
  MOPPE_CHECK (contact.hit);
  MOPPE_CHECK_NEAR (direction_value (contact.normal)[0], 1.0f, 1e-3f);
  MOPPE_CHECK_NEAR (displacement_value (contact.push)[0], 0.25f, 0.02f);
  MOPPE_CHECK_NEAR (displacement_value (contact.push)[2], 0.0f, 1e-3f);

  const mov::TrunkContact clear = probe (field, 2.0f, 0.0f);
  MOPPE_CHECK (!clear.hit);
}

MOPPE_TEST (trunk_field_wraps_trunks_toward_the_focus) {
  mov::TrunkField field;
  std::vector<mov::Trunk> trunks { upright (10.0f, 10.0f, 0.3f * u::m) };
  field.set_trunks (std::move (trunks),
                    plan_extent (1000.0f * u::m, 1000.0f * u::m));
  // The same trunk, one period over: present only where the focus is.
  field.focus (position (Vec3 (1008.0f, 0.0f, 1010.0f)));
  const mov::TrunkContact wrapped = probe (field, 1010.5f, 1010.0f);
  MOPPE_CHECK (wrapped.hit);
  MOPPE_CHECK_NEAR (direction_value (wrapped.normal)[0], 1.0f, 1e-3f);
  const mov::TrunkContact canonical = probe (field, 10.5f, 10.0f);
  MOPPE_CHECK (!canonical.hit);
}
