// The NHAL demo scene: a valley of procedural terrain under a spruce stand,
// drawn by vertex pulling into a 4x multisampled HDR target with reversed-Z,
// then sky-filled and tonemapped into the drawable. A compute pass sways
// the trees in the wind, culls them to the view, and writes their
// indirect draws. It is the first moppe-
// shaped workload for NHAL, identical on Metal 4 and Direct3D 12.
#ifndef MOPPE_NHAL_DEMO_SCENE_HH
#define MOPPE_NHAL_DEMO_SCENE_HH

#include <moppe/nhal/nhal.hh>

#include <array>
#include <cstdint>

namespace moppe::nhal::demo {
  // Each program's stages in the device's form.
  struct Shaders {
    StageCode terrain_vertex, terrain_fragment;
    StageCode trees_vertex, trees_fragment;
    StageCode sky_vertex, sky_fragment;
    StageCode tonemap_vertex, tonemap_fragment;
    StageCode forest_wind_compute;
    StageCode terrain_shadow_vertex, trees_shadow_vertex;
    StageCode resolve_vertex, resolve_fragment;
  };

  // One frame's flight controls, each in -1..1.
  struct Flight {
    float forward = 0, strafe = 0, rise = 0;
    float turn = 0, pitch = 0;
    bool boost = false;
  };

  class Scene {
  public:
    // A temporal scene renders at render_scale of the drawable per axis
    // and accumulates into a history at the drawable's size.
    Scene (Device& device, const Shaders& shaders, float render_scale = 0.5f);
    ~Scene ();

    // Begins a frame and records the scene at `seconds` of scene time; the
    // host ends the frame (and may capture it first). False when the
    // device had no drawable this time.
    bool render (double seconds);

    // Steers a free camera for `seconds` of travel. The scene orbits the
    // valley until the first input, and then flies from where it was.
    void fly (const Flight& input, double seconds);

    std::uint32_t tree_count () const { return m_tree_count; }

    // Temporal upscaling (jittered, smaller scene; the default) or the
    // native scene with 4x MSAA at the drawable's size.
    void set_temporal (bool temporal);
    bool temporal () const { return m_temporal; }

  private:
    void make_targets ();

    Device& m_device;
    // Scene pipelines by mode: [0] native 4x MSAA, [1] temporal.
    Pipeline m_terrain[2], m_trees[2], m_sky[2];
    Pipeline m_tonemap, m_wind, m_resolve;
    Pipeline m_terrain_shadow, m_trees_shadow;
    // The sun's depth over the whole map, sampled by both receivers.
    Texture m_shadow_map;
    Buffer m_terrain_samples, m_terrain_indices;
    Buffer m_tree_instances, m_tree_indices;
    // Written each frame by the wind: swaying instances, the visible ones'
    // indices and indirect draw, and the sun's draw of all of them.
    Buffer m_tree_animated, m_tree_draw, m_tree_visible, m_tree_shadow_draw;
    std::uint32_t m_terrain_index_count = 0;
    std::uint32_t m_tree_index_count = 0;
    std::uint32_t m_tree_count = 0;
    std::uint32_t m_grid = 0;
    float m_cell = 0;
    Texture m_color, m_motion, m_depth, m_scene;
    Texture m_low_color, m_low_motion, m_low_depth, m_history[2];
    std::uint32_t m_width = 0, m_height = 0;
    std::uint32_t m_low_width = 0, m_low_height = 0;
    float m_render_scale;
    bool m_temporal = true;
    bool m_restart = true;
    std::uint32_t m_frame = 0;
    std::array<std::array<float, 4>, 4> m_previous {};
    bool m_flying = false;
    float m_eye[3] {};
    float m_yaw = 0, m_pitch = 0;
  };
}

#endif
