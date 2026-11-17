#include <moppe/render/viewport.hh>

#include <tests/test.hh>

namespace {
  using moppe::render::fit_viewport;
  using moppe::render::Viewport;

  // Sixteen wide for every nine high.
  constexpr moppe::aspect_ratio_t wide =
    16.0f / 9.0f * moppe::aspect_ratio[mp_units::one];
}

MOPPE_TEST (viewport_centres_the_play_area_without_rendering_the_sides) {
  const Viewport view = fit_viewport (6016, 1692, wide);
  MOPPE_CHECK (view.x == 1504);
  MOPPE_CHECK (view.y == 0);
  MOPPE_CHECK (view.width == 3008);
  MOPPE_CHECK (view.height == 1692);

  // An odd leftover pixel goes to the right, not into the image or its
  // pointer coordinates; resizing does not change the content's scale.
  const Viewport odd = fit_viewport (6017, 1692, wide);
  MOPPE_CHECK (odd.x == 1504);
  MOPPE_CHECK (odd.x + odd.width == 4512);
  MOPPE_CHECK (odd.height == 1692);
}

MOPPE_TEST (viewport_keeps_narrower_surfaces_whole) {
  for (const Viewport surface : { Viewport { 0, 0, 1920, 1080 },
                                  Viewport { 0, 0, 1920, 1200 },
                                  Viewport { 0, 0, 1024, 768 },
                                  Viewport { 0, 0, 1080, 1920 },
                                  Viewport { 0, 0, 0, 1080 },
                                  Viewport { 0, 0, 1920, 0 } }) {
    const Viewport view = fit_viewport (surface.width, surface.height, wide);
    MOPPE_CHECK (view.x == 0 && view.y == 0);
    MOPPE_CHECK (view.width == surface.width);
    MOPPE_CHECK (view.height == surface.height);
  }
}

MOPPE_TEST (viewport_can_return_to_full_width) {
  const Viewport view =
    fit_viewport (6016, 1692, 0.0f * moppe::aspect_ratio[mp_units::one]);
  MOPPE_CHECK (view.x == 0 && view.y == 0);
  MOPPE_CHECK (view.width == 6016);
  MOPPE_CHECK (view.height == 1692);
}
