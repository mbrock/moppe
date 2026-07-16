#include <moppe/game/inspector_ui.hh>

#include <tests/test.hh>

using namespace moppe;

MOPPE_TEST (ui_flow_consumes_rows_with_gaps) {
  game::UiFlow flow (
    { 10.0f, 20.0f, 100.0f, 80.0f }, game::UiFlowDirection::Column, 5.0f);
  const game::UiRect first = flow.take (20.0f);
  const game::UiRect second = flow.take (15.0f);
  const game::UiRect rest = flow.rest ();

  MOPPE_CHECK_NEAR (first.y, 20.0f, 0.0f);
  MOPPE_CHECK_NEAR (first.height, 20.0f, 0.0f);
  MOPPE_CHECK_NEAR (second.y, 45.0f, 0.0f);
  MOPPE_CHECK_NEAR (rest.y, 65.0f, 0.0f);
  MOPPE_CHECK_NEAR (rest.height, 35.0f, 0.0f);
}

MOPPE_TEST (ui_grid_cells_share_available_width) {
  const game::UiRect bounds { 20.0f, 30.0f, 290.0f, 200.0f };
  const game::UiRect first =
    game::ui_grid_cell (bounds, 2, 0, 30.0f, 4.0f);
  const game::UiRect fourth =
    game::ui_grid_cell (bounds, 2, 3, 30.0f, 4.0f);

  MOPPE_CHECK_NEAR (first.width, 143.0f, 0.0f);
  MOPPE_CHECK_NEAR (fourth.x, 167.0f, 0.0f);
  MOPPE_CHECK_NEAR (fourth.y, 64.0f, 0.0f);
}

MOPPE_TEST (ui_inset_never_produces_negative_space) {
  const game::UiRect inset =
    game::ui_inset ({ 4.0f, 8.0f, 10.0f, 6.0f }, 20.0f);
  MOPPE_CHECK_NEAR (inset.x, 9.0f, 0.0f);
  MOPPE_CHECK_NEAR (inset.y, 11.0f, 0.0f);
  MOPPE_CHECK_NEAR (inset.width, 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (inset.height, 0.0f, 0.0f);
}
