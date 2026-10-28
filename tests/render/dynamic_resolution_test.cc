#include <moppe/nhal/presentation.hh>
#include <moppe/render/dynamic_resolution.hh>

#include <tests/test.hh>

#include <set>

namespace {
  using namespace moppe;
  using render::DynamicResolution;

  // A frame whose GPU time is partly fixed and partly the scene's pixels.
  struct GpuLoad {
    float fixed_ms, scene_ms;

    seconds_t frame (const DynamicResolution& dynamic) const {
      const float share = dynamic.share ();
      return seconds ((fixed_ms + scene_ms * share * share) * 0.001f);
    }
  };

  const seconds_t sixtieth = seconds (1.0f / 60.0f);

  // Runs `frames` frames and returns every share the last hundred drew at.
  std::set<float>
  play (DynamicResolution& dynamic, const GpuLoad& machine, int frames) {
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
  // The second view costs 17.75 ms even at half size: stopping there
  // cannot hold 60 fps, though a smaller scene still fits comfortably.
  for (const GpuLoad machine :
       { GpuLoad { 7.0f, 13.0f }, GpuLoad { 8.0f, 39.0f } }) {
    DynamicResolution dynamic;
    const std::set<float> late = play (dynamic, machine, 600);
    // It settles on one step, inside the budget, without hunting.
    MOPPE_CHECK (late.size () == 1);
    MOPPE_CHECK (*late.begin () < 1.0f);
    const float budget_ms = 1000.0f / 60.0f * DynamicResolution::headroom;
    const float settled_ms = seconds_value (machine.frame (dynamic)) * 1000.0f;
    MOPPE_CHECK (settled_ms <= budget_ms);
    MOPPE_CHECK (settled_ms >= budget_ms * 0.80f);
  }
}

MOPPE_TEST (dynamic_resolution_stops_at_three_eighths_size) {
  DynamicResolution dynamic;
  const std::set<float> late = play (dynamic, { 30.0f, 30.0f }, 600);
  MOPPE_CHECK (late == std::set<float> { 0.375f });
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

MOPPE_TEST (dynamic_resolution_does_not_take_slow_frames_for_a_slow_display) {
  // Metal can report presentations without knowing the display's refresh.
  // A missed budget must not become the next frame's more generous budget.
  nhal::PresentationClock clock;
  for (std::uint64_t frame = 1; frame <= 32; ++frame)
    clock.presented (frame, double (frame) / 30.0);
  const nhal::FrameTiming timing = clock.predict (33, 32.0 / 30.0);
  MOPPE_CHECK (timing.predicted);

  DynamicResolution dynamic;
  const GpuLoad machine { 7.0f, 13.0f };
  const seconds_t period =
    seconds (float (std::max (1.0 / 60.0, timing.refresh_seconds)));
  for (int frame = 0; frame < 600; ++frame)
    dynamic.observe (machine.frame (dynamic), period);
  MOPPE_CHECK (dynamic.share () < 1.0f);
  MOPPE_CHECK (machine.frame (dynamic) <=
               sixtieth * DynamicResolution::headroom);

  // A display that really is 30 Hz still gives the GPU its full period.
  clock.set_refresh (1.0 / 30.0);
  MOPPE_CHECK_NEAR (
    clock.predict (33, 32.0 / 30.0).refresh_seconds, 1.0 / 30.0, 1e-7);
}
