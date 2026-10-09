#include <moppe/game/terrain.hh>

#include <tests/recording_renderer.hh>
#include <tests/test.hh>

#include <cmath>

using namespace moppe;

MOPPE_TEST (terrain_local_shadow_keeps_a_typed_bounded_focus) {
  game::Terrain terrain;
  test::RecordingRenderer renderer;
  const position_t camera = position (Vec3 (100.0f, 20.0f, 200.0f));

  terrain.render_local_shadow (renderer,
                               camera,
                               Vec3 (0.0f, 0.0f, 1.0f),
                               normalized (Vec3 (0.7f, 0.7f, 0.2f)),
                               true);

  MOPPE_CHECK (renderer.local_shadow.has_value ());
  const render::LocalShadowParams& shadow = *renderer.local_shadow;
  MOPPE_CHECK (shadow.include_forest);
  const render::ShadowCascade& near =
    shadow.cascades[render::LocalShadowParams::near];
  MOPPE_CHECK_NEAR (near.radius.numerical_value_in (u::m), 160.0f, 1e-6f);
  const Vec3 focus = position_value (near.focus);
  const float texel = 320.0f / 2048.0f;
  MOPPE_CHECK (std::fabs (focus[0] - 100.0f) <= texel);
  MOPPE_CHECK (std::fabs (focus[1] - 20.0f) <= texel);
  MOPPE_CHECK (std::fabs (focus[2] - 248.0f) <= texel);
  // The far cascade reaches well past the near one, mostly ahead.
  const render::ShadowCascade& far =
    shadow.cascades[render::LocalShadowParams::far];
  MOPPE_CHECK (far.radius > 5.0f * near.radius);
  MOPPE_CHECK (position_value (far.focus)[2] > focus[2]);
  for (const render::ShadowCascade& cascade : shadow.cascades)
    for (int element = 0; element < 16; ++element)
      MOPPE_CHECK (std::isfinite (cascade.light_view_proj.element (element)));
}
