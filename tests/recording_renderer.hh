#ifndef MOPPE_TESTS_RECORDING_RENDERER_HH
#define MOPPE_TESTS_RECORDING_RENDERER_HH

#include <moppe/render/renderer.hh>

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

    render::TexturePtr create_texture (const render::TextureDesc&,
                                       const void*) override {
      return {};
    }
    render::MeshPtr create_mesh (const render::DrawList&) override {
      return {};
    }
    void set_terrain (const render::TerrainParams&,
                      const float*,
                      const Vec3*) override {}
    void set_terrain_topology_overlay (bool) override {}
    void set_terrain_textures (render::TexturePtr,
                               render::TexturePtr,
                               render::TexturePtr,
                               render::TexturePtr) override {}
    void set_terrain_overlay (const render::TerrainOverlayParams&,
                              std::span<const float>) override {}
    void clear_terrain_overlay () override {}
    void render_terrain_shadow (const Mat4&) override {}
    void set_ocean (const render::OceanSetup& setup,
                    std::span<const float> levels) override {
      ocean = setup;
      water_levels.assign (levels.begin (), levels.end ());
    }
    void set_water_flow (std::span<const float> flow) override {
      water_flow.assign (flow.begin (), flow.end ());
    }
    void set_terrain_paths (std::span<const float> trails,
                            std::span<const float> home_base) override {
      trail_influence.assign (trails.begin (), trails.end ());
      home_base_influence.assign (home_base.begin (), home_base.end ());
    }
    bool begin_frame (const render::FrameParams&) override {
      return true;
    }
    void draw_terrain (const render::ChunkDraw*, int) override {}
    void draw_sky (const render::SkyParams&) override {}
    void draw_ocean (const render::OceanParams&) override {}
    void draw_rivers (const render::Mesh&, const Mat4&) override {}
    void draw_mesh (const render::Mesh&, const Mat4&) override {}
    void draw_list (const render::DrawList&) override {}
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
