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
                               Vec3 (0.0f, 0.866f, -0.5f),
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

MOPPE_TEST (the_shadow_lattice_turns_about_the_camera_not_the_origin) {
  // The map's texels are a lattice in the world. When the sun moves the
  // lattice must turn, and about the eye: a point beside the camera keeps
  // very nearly its texel, however far from the world's origin they stand.
  game::Terrain terrain;
  test::RecordingRenderer renderer;
  const position_t camera = position (Vec3 (4200.0f, 70.0f, 3100.0f));
  const Vec3 probe (4203.0f, 71.0f, 3149.0f);
  const Vec3 pole = normalized (Vec3 (0.0f, 0.866f, -0.5f));
  // The equinox sun, square to the pole, a frame apart on a 48-minute day.
  const auto sun_at = [&] (float turn) {
    const Vec3 east (1, 0, 0);
    const Vec3 noon = cross (pole, east);
    return normalized (east * std::cos (turn) + noon * std::sin (turn));
  };
  const auto probe_texel = [&] (float turn, int axis) {
    terrain.render_local_shadow (
      renderer, camera, Vec3 (0, 0, 1), sun_at (turn), pole, true);
    const Mat4& map =
      renderer.local_shadow->cascades[render::LocalShadowParams::near]
        .light_view_proj;
    return 1024.0f * map.transform_point (probe)[axis];
  };
  const float turn = 0.9f, frame = 3.6e-5f;
  for (int axis = 0; axis < 2; ++axis) {
    const float before = probe_texel (turn, axis);
    const float after = probe_texel (turn + frame, axis);
    // Pivoting on the origin slid it most of a texel every frame.
    MOPPE_CHECK_NEAR (after, before, 0.05f);
  }
  // Travel moves the lattice only by whole texels.
  const float before = probe_texel (turn, 0);
  terrain.render_local_shadow (renderer,
                               position (Vec3 (4207.3f, 70.4f, 3111.9f)),
                               Vec3 (0, 0, 1),
                               sun_at (turn),
                               pole,
                               true);
  const float after =
    1024.0f * renderer.local_shadow->cascades[render::LocalShadowParams::near]
                .light_view_proj.transform_point (probe)[0];
  const float moved = after - before;
  MOPPE_CHECK_NEAR (moved, std::round (moved), 0.02f);
}
