#include <moppe/game/input_frame_adapter.hh>
#include <moppe/platform/input.hh>

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
  MOPPE_CHECK (frame.deploy_glider_held);
  frame = input.take_frame ();
  MOPPE_CHECK (!frame.deploy_glider);
  MOPPE_CHECK (frame.deploy_glider_held);
  input.key (platform::Key::E, true);
  frame = input.take_frame ();
  MOPPE_CHECK (!frame.deploy_glider);
  MOPPE_CHECK (frame.deploy_glider_held);
  input.key (platform::Key::E, false);
  MOPPE_CHECK (!input.take_frame ().deploy_glider_held);

  input.key (platform::Key::Tab, true);
  frame = input.take_frame ();
  MOPPE_CHECK (frame.cycle_camera);

  input.key (platform::Key::Seven, true);
  input.key (platform::Key::Five, true);
  input.key (platform::Key::R, true);
  frame = input.take_frame ();
  MOPPE_CHECK (frame.toggle_mount);

  input.key (platform::Key::Mount, true);
  frame = input.take_frame ();
  MOPPE_CHECK (frame.toggle_mount);
  MOPPE_CHECK (!input.take_frame ().toggle_mount);
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

MOPPE_TEST (input_frame_adapter_combines_keyboard_and_controller_controls) {
  game::InputFrameAdapter input;
  input.controls ({ .steer = 0.5f, .drive = 0.75f, .boost = 0.4f });
  input.key (platform::Key::A, true);
  game::InputFrame frame = input.take_frame ();
  MOPPE_CHECK_NEAR (game::input_value (frame.turn), -1.0f, 0.0f);
  MOPPE_CHECK_NEAR (game::input_value (frame.drive), 0.75f, 0.0f);
  MOPPE_CHECK_NEAR (game::input_value (frame.boost), 0.4f, 0.0f);

  input.key (platform::Key::A, false);
  frame = input.take_frame ();
  MOPPE_CHECK_NEAR (game::input_value (frame.turn), 0.5f, 0.0f);
}

MOPPE_TEST (input_frame_adapter_turns_the_head_at_the_stick_rate) {
  game::InputFrameAdapter input;
  input.controls ({ .look_x = 1.0f, .look_y = -0.5f });
  game::InputFrame frame = input.take_frame (0.5f);
  MOPPE_CHECK_NEAR (frame.look_yaw,
                    0.5f * game::InputFrameAdapter::look_yaw_rate, 1e-6f);
  MOPPE_CHECK_NEAR (frame.look_pitch,
                    -0.25f * game::InputFrameAdapter::look_pitch_rate,
                    1e-6f);
  // The mouse adds to it within the same frame.
  input.look (10.0f, 0.0f);
  frame = input.take_frame (0.0f);
  MOPPE_CHECK_NEAR (frame.look_yaw, 0.035f, 1e-6f);
  input.controls ({});
  frame = input.take_frame (1.0f);
  MOPPE_CHECK_NEAR (frame.look_yaw, 0.0f, 0.0f);
}

MOPPE_TEST (right_stick_looks_through_a_round_dead_zone) {
  float x = 1, y = 1;
  platform::look_axes (0.1f, 0.1f, x, y);
  MOPPE_CHECK_NEAR (x, 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (y, 0.0f, 0.0f);
  // Straight up stays straight up, and full travel is full rate.
  platform::look_axes (0.0f, 1.0f, x, y);
  MOPPE_CHECK_NEAR (x, 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (y, 1.0f, 1e-6f);
  // Half travel turns well under half as fast, for fine aiming.
  platform::look_axes (0.575f, 0.0f, x, y);
  MOPPE_CHECK (x > 0.0f && x < 0.3f);
}
