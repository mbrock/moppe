// The NHAL demo scene: a valley of procedural terrain under a spruce stand,
// drawn by vertex pulling into a 4x multisampled HDR target with reversed-Z,
// then sky-filled and tonemapped into the drawable. A compute pass sways
// the trees in the wind and writes their indirect draw. It is the first moppe-
// shaped workload for NHAL, identical on Metal 4 and Direct3D 12.
#ifndef MOPPE_NHAL_DEMO_SCENE_HH
#define MOPPE_NHAL_DEMO_SCENE_HH

#include <moppe/nhal/nhal.hh>

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
  };

  class Scene {
  public:
    Scene (Device& device, const Shaders& shaders);
    ~Scene ();

    // Begins a frame and records the scene at `seconds` of scene time; the
    // host ends the frame (and may capture it first). False when the
    // device had no drawable this time.
    bool render (double seconds);

    std::uint32_t tree_count () const { return m_tree_count; }

  private:
    void make_targets ();

    Device& m_device;
    Pipeline m_terrain, m_trees, m_sky, m_tonemap, m_wind;
    Pipeline m_terrain_shadow, m_trees_shadow;
    // The sun's depth over the whole map, sampled by both receivers.
    Texture m_shadow_map;
    Buffer m_terrain_samples, m_terrain_indices;
    Buffer m_tree_instances, m_tree_indices;
    // Written each frame by the wind: swaying instances and their draw.
    Buffer m_tree_animated, m_tree_draw;
    std::uint32_t m_terrain_index_count = 0;
    std::uint32_t m_tree_index_count = 0;
    std::uint32_t m_tree_count = 0;
    std::uint32_t m_grid = 0;
    float m_cell = 0;
    Texture m_color, m_depth, m_scene;
    std::uint32_t m_width = 0, m_height = 0;
  };
}

#endif
