#include <moppe/game/input_frame_adapter.hh>

#include <tests/test.hh>

#include <type_traits>

using namespace moppe;

static_assert (std::is_trivially_copyable_v<game::InputFrame>);

MOPPE_TEST (recorded_input_frame_needs_no_platform_event) {
  game::InputFrame recorded;
  recorded.turn = -0.25f;
  recorded.drive = 0.75f;
  recorded.boost = 0.5f;
  recorded.deploy_glider = true;

  MOPPE_CHECK_NEAR (game::input_value (recorded.turn), -0.25f, 0.0f);
  MOPPE_CHECK_NEAR (game::input_value (recorded.drive), 0.75f, 0.0f);
  MOPPE_CHECK_NEAR (game::input_value (recorded.boost), 0.5f, 0.0f);
  MOPPE_CHECK (recorded.deploy_glider);
}

MOPPE_TEST (input_frame_adapter_maps_keyboard_controls_and_actions) {
  game::InputFrameAdapter input;

  input.key (platform::Key::A, true);
  input.key (platform::Key::W, true);
  input.key (platform::Key::Space, true);
  game::InputFrame frame = input.take_frame ();
  MOPPE_CHECK_NEAR (game::input_value (frame.turn), -1.0f, 0.0f);
  MOPPE_CHECK_NEAR (game::input_value (frame.drive), 1.0f, 0.0f);
  MOPPE_CHECK_NEAR (game::input_value (frame.boost), 1.0f, 0.0f);

  input.key (platform::Key::A, false);
  input.key (platform::Key::W, false);
  input.key (platform::Key::Space, false);
  frame = input.take_frame ();
  MOPPE_CHECK_NEAR (game::input_value (frame.turn), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (game::input_value (frame.drive), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (game::input_value (frame.boost), 0.0f, 0.0f);

  input.key (platform::Key::E, true);
  frame = input.take_frame ();
  MOPPE_CHECK (frame.deploy_glider);
  MOPPE_CHECK (!input.take_frame ().deploy_glider);

  input.key (platform::Key::Tab, true);
  frame = input.take_frame ();
  MOPPE_CHECK (frame.cycle_camera);

  input.key (platform::Key::Seven, true);
  input.key (platform::Key::Five, true);
  input.key (platform::Key::R, true);
  frame = input.take_frame ();
  MOPPE_CHECK (frame.toggle_mount);
}

MOPPE_TEST (input_frame_adapter_maps_touch_and_cinematic_controls) {
  game::InputFrameAdapter input;
  input.controls ({ .steer = 1.4f, .drive = -1.4f, .boost = 1.4f });
  game::InputFrame frame = input.take_frame ();
  MOPPE_CHECK_NEAR (game::input_value (frame.turn), 1.0f, 0.0f);
  MOPPE_CHECK_NEAR (game::input_value (frame.drive), -1.0f, 0.0f);
  MOPPE_CHECK_NEAR (game::input_value (frame.boost), 1.0f, 0.0f);

  input.cinematic_key (platform::Key::E, true);
  input.cinematic_key (platform::Key::Space, true);
  frame = input.take_frame ();
  MOPPE_CHECK_NEAR (game::input_value (frame.boost), 1.0f, 0.0f);
  MOPPE_CHECK (frame.leave_cinematic);
}
