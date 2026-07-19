#ifndef MOPPE_WEBGPU_RENDERER_HH
#define MOPPE_WEBGPU_RENDERER_HH

#include <moppe/render/renderer.hh>

#include <functional>
#include <memory>

namespace moppe::render {
  class WebGpuRenderer final : public Renderer {
  public:
    using Ready = std::function<void (bool, const std::string&)>;

    WebGpuRenderer (const char* canvas_selector,
                    int width_points,
                    int height_points,
                    float scale_factor);
    ~WebGpuRenderer () override;

    void initialize (Ready ready);
    void resize (int width_points, int height_points, float scale_factor);

    TexturePtr create_texture (const TextureDesc& desc,
                               const void* pixels) override;
    MeshPtr create_mesh (const DrawList& recorded) override;

    void set_terrain (const TerrainParams& params,
                      const float* heights,
                      const Vec3* normals) override;
    void set_terrain_topology_overlay (bool enabled) override;
    void set_terrain_textures (TexturePtr grass,
                               TexturePtr dirt,
                               TexturePtr rock,
                               TexturePtr snow) override;
    void set_terrain_overlay (const TerrainOverlayParams& params,
                              std::span<const float> values) override;
    void clear_terrain_overlay () override;
    void render_terrain_shadow (const Mat4& light_view_proj) override;
    void set_ocean (const OceanSetup& setup,
                    std::span<const float> water_levels) override;

    bool begin_frame (const FrameParams& params) override;
    void draw_terrain (const ChunkDraw* chunks, int count) override;
    void draw_sky (const SkyParams& params) override;
    void draw_ocean (const OceanParams& params) override;
    void draw_rivers (const Mesh& mesh, const Mat4& model) override;
    void draw_mesh (const Mesh& mesh, const Mat4& model) override;
    void draw_list (const DrawList& list) override;
    void apply_underwater (float time) override;
    void apply_motion_blur (float strength) override;
    void apply_scene_blur () override;
    void draw_hud (const DrawList& list) override;
    void end_frame () override;

    int width_pts () const override;
    int height_pts () const override;
    float scale_factor () const override;

  private:
    struct State;
    std::unique_ptr<State> m_state;
  };
}

#endif
