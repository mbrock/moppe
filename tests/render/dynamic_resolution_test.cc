#include <moppe/render/dynamic_resolution.hh>

#include <tests/test.hh>

#include <set>

namespace {
  using namespace moppe;
  using render::DynamicResolution;

  // A frame whose GPU time is partly fixed and partly the scene's pixels.
  struct Machine {
    float fixed_ms, scene_ms;

    seconds_t frame (const DynamicResolution& dynamic) const {
      const float share = dynamic.share ();
      return seconds ((fixed_ms + scene_ms * share * share) * 0.001f);
    }
  };

  const seconds_t sixtieth = seconds (1.0f / 60.0f);

  // Runs `frames` frames and returns every share the last hundred drew at.
  std::set<float> play (DynamicResolution& dynamic, const Machine& machine,
                        int frames) {
    std::set<float> late;
    for (int frame = 0; frame < frames; ++frame) {
      dynamic.observe (machine.frame (dynamic), sixtieth);
      if (frame >= frames - 100)
        late.insert (dynamic.share ());
    }
    return late;
  }
}

MOPPE_TEST (dynamic_resolution_leaves_a_frame_within_its_budget_alone) {
  DynamicResolution dynamic;
  const std::set<float> late = play (dynamic, { 4.0f, 8.0f }, 600);
  MOPPE_CHECK (late == std::set<float> { 1.0f });
}

MOPPE_TEST (dynamic_resolution_sheds_pixels_until_the_frame_fits) {
  DynamicResolution dynamic;
  // 20 ms at full size: the M2 Pro filling a 7680x2160 display.
  const Machine machine { 7.0f, 13.0f };
  const std::set<float> late = play (dynamic, machine, 600);
  // It settles on one step, inside the budget, without hunting.
  MOPPE_CHECK (late.size () == 1);
  MOPPE_CHECK (*late.begin () < 1.0f);
  const float budget_ms =
    1000.0f / 60.0f * DynamicResolution::headroom;
  const float settled_ms = seconds_value (machine.frame (dynamic)) * 1000.0f;
  MOPPE_CHECK (settled_ms <= budget_ms);
  MOPPE_CHECK (settled_ms >= budget_ms * 0.80f);
}

MOPPE_TEST (dynamic_resolution_stops_at_half_size) {
  DynamicResolution dynamic;
  const std::set<float> late = play (dynamic, { 30.0f, 30.0f }, 600);
  MOPPE_CHECK (late == std::set<float> { 0.5f });
}

MOPPE_TEST (dynamic_resolution_returns_when_the_load_lifts) {
  DynamicResolution dynamic;
  play (dynamic, { 7.0f, 13.0f }, 600);
  MOPPE_CHECK (dynamic.share () < 1.0f);
  const std::set<float> late = play (dynamic, { 3.0f, 6.0f }, 600);
  MOPPE_CHECK (late == std::set<float> { 1.0f });
}

MOPPE_TEST (dynamic_resolution_ignores_frames_without_a_time) {
  DynamicResolution dynamic;
  for (int frame = 0; frame < 100; ++frame)
    dynamic.observe (seconds (0.0f), sixtieth);
  MOPPE_CHECK (dynamic.share () == 1.0f);
}
