// Reflection for the demo's hand-written shaders, in the form luv-shaderc
// generates (docs/nhal.md). It goes away when the shaders are written in
// Luv's language.
#ifndef MOPPE_NHAL_DEMO_PROGRAMS_HH
#define MOPPE_NHAL_DEMO_PROGRAMS_HH

#include <moppe/nhal/reflection.hh>

#include <array>

namespace moppe::nhal::shaders {
  // Every program reads the same frame block at buffer binding 0.
  struct FrameState {
    std::array<float, 4> camera_position; // w: seconds
    std::array<float, 4> camera_right;    // right / tan(fov x / 2); w: tan
    std::array<float, 4> camera_up;       // up / tan(fov y / 2); w: tan
    std::array<float, 4> camera_forward;  // w: near plane
    std::array<float, 4> sun_direction;   // toward the sun
    std::array<float, 4> sun_color;
    std::array<float, 4> sky_zenith;
    std::array<float, 4> sky_horizon;     // w: fog density per metre
    std::array<float, 4> terrain;         // cell, samples per side, x0, z0
  };
  static_assert (sizeof (FrameState) == 144);

  namespace terrain {
    inline constexpr Resource resources[] = {
      { "frame_state", ResourceKind::uniform_block, 0,
        stage_vertex | stage_fragment, sizeof (FrameState) },
      { "samples", ResourceKind::storage_buffer, 1, stage_vertex, 0 },
    };
    inline constexpr Program program {
      "terrain", "terrain_vertex", "terrain_fragment", nullptr, resources, 1
    };
  }

  namespace trees {
    inline constexpr Resource resources[] = {
      { "frame_state", ResourceKind::uniform_block, 0,
        stage_vertex | stage_fragment, sizeof (FrameState) },
      { "instances", ResourceKind::storage_buffer, 1, stage_vertex, 0 },
    };
    inline constexpr Program program {
      "trees", "trees_vertex", "trees_fragment", nullptr, resources, 1
    };
  }

  namespace sky {
    inline constexpr Resource resources[] = {
      { "frame_state", ResourceKind::uniform_block, 0,
        stage_vertex | stage_fragment, sizeof (FrameState) },
    };
    inline constexpr Program program {
      "sky", "sky_vertex", "sky_fragment", nullptr, resources, 1
    };
  }

  namespace tonemap {
    inline constexpr Resource resources[] = {
      { "frame_state", ResourceKind::uniform_block, 0,
        stage_vertex | stage_fragment, sizeof (FrameState) },
      { "scene", ResourceKind::texture_2d, 0, stage_fragment, 0 },
      { "linear_clamp", ResourceKind::sampler, 0, stage_fragment, 0 },
    };
    inline constexpr Program program {
      "tonemap", "tonemap_vertex", "tonemap_fragment", nullptr, resources, 1
    };
  }
}

#endif
