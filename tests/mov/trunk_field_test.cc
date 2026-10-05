#include <moppe/mov/trunk_field.hh>

#include <tests/test.hh>

using namespace moppe;

namespace {
  mov::Trunk upright (float x, float z, float radius) {
    return { Vec3 (x, 0.0f, z), Vec3 (0.0f, 1.0f, 0.0f), 12.0f, radius };
  }
}

MOPPE_TEST (trunk_field_pushes_a_capsule_out_of_a_trunk) {
  mov::TrunkField field;
  std::vector<mov::Trunk> trunks { upright (0.0f, 0.0f, 0.3f) };
  field.set_trunks (std::move (trunks), 0.0f, 0.0f);
  field.focus (Vec3 (0.0f, 0.0f, 0.0f));
  MOPPE_CHECK (field.resident_cells () > 0);

  // Centres 0.5 m apart with radii 0.3 + 0.45: 0.25 m of overlap along +x.
  const mov::TrunkContact contact =
    field.collide (Vec3 (0.5f, 1.0f, 0.0f), Vec3 (0.5f, 2.0f, 0.0f), 0.45f);
  MOPPE_CHECK (contact.hit);
  MOPPE_CHECK_NEAR (contact.normal[0], 1.0f, 1e-3f);
  MOPPE_CHECK_NEAR (contact.push[0], 0.25f, 0.02f);
  MOPPE_CHECK_NEAR (contact.push[2], 0.0f, 1e-3f);

  const mov::TrunkContact clear =
    field.collide (Vec3 (2.0f, 1.0f, 0.0f), Vec3 (2.0f, 2.0f, 0.0f), 0.45f);
  MOPPE_CHECK (!clear.hit);
}

MOPPE_TEST (trunk_field_wraps_trunks_toward_the_focus) {
  mov::TrunkField field;
  std::vector<mov::Trunk> trunks { upright (10.0f, 10.0f, 0.3f) };
  field.set_trunks (std::move (trunks), 1000.0f, 1000.0f);
  // The same trunk, one period over: present only where the focus is.
  field.focus (Vec3 (1008.0f, 0.0f, 1010.0f));
  const mov::TrunkContact wrapped = field.collide (
    Vec3 (1010.5f, 1.0f, 1010.0f), Vec3 (1010.5f, 2.0f, 1010.0f), 0.45f);
  MOPPE_CHECK (wrapped.hit);
  MOPPE_CHECK_NEAR (wrapped.normal[0], 1.0f, 1e-3f);
  const mov::TrunkContact canonical =
    field.collide (Vec3 (10.5f, 1.0f, 10.0f), Vec3 (10.5f, 2.0f, 10.0f), 0.45f);
  MOPPE_CHECK (!canonical.hit);
}
