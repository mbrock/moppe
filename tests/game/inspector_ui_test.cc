#include <moppe/game/inspector_ui.hh>

#include <tests/test.hh>

using namespace moppe;

MOPPE_TEST (ui_window_translates_between_local_and_screen_space) {
  const game::UiWindow window ({ 40.0f, 70.0f, 320.0f, 200.0f });
  const game::UiRect screen = window.to_screen ({ 12.0f, 18.0f, 80.0f, 30.0f });

  MOPPE_CHECK_NEAR (screen.x, 52.0f, 0.0f);
  MOPPE_CHECK_NEAR (screen.y, 88.0f, 0.0f);
  MOPPE_CHECK_NEAR (window.local_x (52.0f), 12.0f, 0.0f);
  MOPPE_CHECK_NEAR (window.local_y (88.0f), 18.0f, 0.0f);
}

MOPPE_TEST (ui_window_only_starts_dragging_from_its_title_bar) {
  game::UiWindow window ({ 40.0f, 70.0f, 320.0f, 200.0f });

  MOPPE_CHECK (!window.begin_drag (60.0f, 120.0f));
  MOPPE_CHECK (window.begin_drag (60.0f, 80.0f));
  MOPPE_CHECK (window.dragging ());
  window.end_drag ();
  MOPPE_CHECK (!window.dragging ());
}

MOPPE_TEST (ui_window_dragging_preserves_grab_offset_and_stays_visible) {
  game::UiWindow window ({ 40.0f, 70.0f, 120.0f, 90.0f });
  MOPPE_CHECK (window.begin_drag (60.0f, 80.0f));

  window.drag_to (390.0f, 290.0f, 400.0f, 300.0f);
  MOPPE_CHECK_NEAR (window.bounds ().x, 272.0f, 0.0f);
  MOPPE_CHECK_NEAR (window.bounds ().y, 202.0f, 0.0f);

  window.drag_to (-20.0f, -20.0f, 400.0f, 300.0f);
  MOPPE_CHECK_NEAR (window.bounds ().x, 8.0f, 0.0f);
  MOPPE_CHECK_NEAR (window.bounds ().y, 8.0f, 0.0f);
}
