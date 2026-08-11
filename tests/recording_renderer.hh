#ifndef MOPPE_TESTS_RECORDING_RENDERER_HH
#define MOPPE_TESTS_RECORDING_RENDERER_HH

#include <moppe/render/renderer.hh>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace moppe::test {
  // Deliberately small renderer recorder for presentation-boundary tests.
  // It retains only texture payloads whose ownership the tests assert.
  class RecordingRenderer final : public render::Renderer {
  public:
    render::OceanSetup ocean {};
    std::vector<float> water_levels;
    std::vector<float> water_flow;
    std::vector<float> trail_influence;
    std::vector<float> home_base_influence;
    std::vector<float> moisture;
    std::vector<float> erosion;
    std::vector<float> deposition;
    std::vector<float> forest_cover;
    std::vector<float> shore_distance;
    std::vector<float> snow_support;
    render::ForestSetup forest_setup {};
    std::vector<render::ForestInstance> forest_instances;
    std::size_t forest_draws = 0;
    std::optional<render::LocalShadowParams> local_shadow;

    render::TexturePtr create_texture (const render::TextureDesc&,
                                       const void*) override {
      return {};
    }
    // Retained geometry comes back as a real handle. A subject that decides
    // when to hold a mesh and when to let it go cannot be tested against a
    // renderer that always answers null.
    std::vector<std::size_t> baked_vertex_counts;
    std::size_t meshes_drawn = 0;

    render::MeshPtr create_mesh (const render::DrawList& recorded) override {
      baked_vertex_counts.push_back (recorded.vertices ().size ());
      return std::make_shared<render::Mesh> ();
    }
    void set_terrain (const render::TerrainParams&,
                      std::span<const terrain::SurfaceElevation>,
                      std::span<const terrain::TerrainNormal>) override {}
    void set_terrain_topology_overlay (bool) override {}
    void set_terrain_textures (render::TexturePtr,
                               render::TexturePtr,
                               render::TexturePtr,
                               render::TexturePtr) override {}
    void set_terrain_overlay (const render::TerrainOverlayParams&,
                              std::span<const float>) override {}
    void clear_terrain_overlay () override {}
    void render_terrain_shadow (const Mat4&, bool) override {}
    void set_ocean (const render::OceanSetup& setup,
                    const render::TexturePixels& levels) override {
      ocean = setup;
      const auto lanes = render::decode_channels (levels);
      water_levels.clear ();
      if (lanes.size () != 2)
        return;
      water_levels.reserve (2 * lanes[0].size ());
      for (std::size_t index = 0; index < lanes[0].size (); ++index) {
        water_levels.push_back (lanes[0][index]);
        water_levels.push_back (lanes[1][index]);
      }
    }
    void set_water_flow (const render::TexturePixels& flow) override {
      const auto lanes = render::decode_channels (flow);
      water_flow.clear ();
      if (lanes.size () != 2)
        return;
      water_flow.reserve (2 * lanes[0].size ());
      for (std::size_t index = 0; index < lanes[0].size (); ++index) {
        water_flow.push_back (lanes[0][index]);
        water_flow.push_back (lanes[1][index]);
      }
    }
    void set_terrain_materials (const render::TexturePixels& landscape,
                                const render::TexturePixels& ground,
                                bool) override {
      const auto landscape_lanes = render::decode_channels (landscape);
      const auto ground_lanes = render::decode_channels (ground);
      if (landscape_lanes.size () == 4) {
        moisture = landscape_lanes[0];
        erosion = landscape_lanes[1];
        deposition = landscape_lanes[2];
        forest_cover = landscape_lanes[3];
      }
      if (ground_lanes.size () == 4) {
        shore_distance = ground_lanes[0];
        for (float& distance : shore_distance)
          distance *= render::terrain_shore_band_metres;
        snow_support = ground_lanes[1];
        trail_influence = ground_lanes[2];
        home_base_influence = ground_lanes[3];
      }
    }
    void
    set_forest (const render::ForestSetup& setup,
                std::span<const render::ForestInstance> instances) override {
      forest_setup = setup;
      forest_instances.assign (instances.begin (), instances.end ());
    }
    bool begin_frame (const render::FrameParams&) override {
      return true;
    }
    void
    render_local_shadow (const render::LocalShadowParams& params) override {
      local_shadow = params;
    }
    void draw_terrain (const render::ChunkDraw*, int) override {}
    void draw_sky (const render::SkyParams&) override {}
    void draw_ocean (const render::OceanParams&) override {}
    void draw_forest () override {
      ++forest_draws;
    }
    void draw_waterfalls (const render::Mesh&, const Mat4&) override {}
    void draw_mesh (const render::Mesh&, const Mat4&, uint64_t) override {
      ++meshes_drawn;
    }
    void draw_list (const render::DrawList&, uint64_t) override {}
    void apply_underwater (float) override {}
    void apply_motion_blur (float) override {}
    void apply_scene_blur () override {}
    void draw_hud (const render::DrawList&) override {}
    void end_frame () override {}
    int width_pts () const override {
      return 0;
    }
    int height_pts () const override {
      return 0;
    }
    float scale_factor () const override {
      return 1.0f;
    }
  };
}

#endif
