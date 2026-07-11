#ifndef MOPPE_RENDER_RENDERER_HH
#define MOPPE_RENDER_RENDERER_HH

#include <moppe/gfx/math.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/render/types.hh>
#include <moppe/render/draw.hh>

#include <cstdint>
#include <span>
#include <string>

namespace moppe {
namespace render {
  enum class TerrainProjection : uint8_t {
    Plane,
    Torus
  };

  // Per-frame environment.  The view matrix already includes the
  // camera-shake rotation; the right/up/forward basis is derived
  // from it and replaces the old GL_MODELVIEW_MATRIX readback for
  // billboards.
  struct FrameParams {
    Mat4 view;
    Mat4 proj;                    // reversed-Z perspective
    Vector3D camera_pos;
    Vector3D cam_right, cam_up, cam_forward;
    Vector3D clear_color;         // also the fog/haze color
    float fog_scale = 0.0f;
    Vector3D sun_dir;             // world space, toward the sun
    // Art-directed sun products.  Ambient is the strength/color fed
    // into the shaders' cool-sky / warm-ground hemisphere fill.
    Vector3D sun_diffuse;
    Vector3D sun_specular;
    Vector3D ambient;
    float time = 0.0f;
    // How much of the sun the camera can actually see (0..1); the
    // game raymarches the heightmap and folds in cloud cover.
    // Drives the present pass's lens flare.
    float sun_visibility = 0.0f;
  };

  // World-change-time terrain setup.  Heights/normals are the same
  // arrays the CPU-side physics samples, so sim and render cannot
  // diverge.
  struct TerrainParams {
    int width;                    // grid samples (2049)
    int height;
    Vector3D scale;               // grid step x/z, height scale y
    float height_scale;           // world meters at height 1.0
    float sea_level_norm;         // sea level / height_scale
    float tex_scale;              // texture repeats per world meter
    float shadow_strength;
    int shadow_resolution = 4096;
    int shadow_sample_step = 1;
    float fog_scale;
    bool periodic = false;
    TerrainProjection projection = TerrainProjection::Plane;
    float torus_major_radius = 0.0f;
    float torus_minor_radius = 0.0f;
    float torus_height_scale = 0.0f;
    bool derive_normals = false;
  };

  enum class TerrainOverlayRamp : uint8_t {
    Heat,
    Flow,
    Streams,
    Categorical,
    Diverging,
    Marker,
    Water
  };

  struct TerrainOverlayParams {
    int width;
    int height;
    float minimum;
    float maximum;
    float opacity = 0.65f;
    TerrainOverlayRamp ramp = TerrainOverlayRamp::Heat;
  };

  enum class TerrainLod : uint8_t {
    Subdivided,
    Native,
    Stride2,
    Stride4,
    Stride8,
    Count
  };

  // One culled terrain chunk instance.  The two distances describe
  // where this level morphs onto the exact triangle surface of its
  // parent level, preventing pops and cracks at chunk boundaries.
  struct ChunkDraw {
    uint16_t x0, z0;              // grid sample origin (multiple of 128)
    TerrainLod lod;
    float morph_start;
    float morph_end;
    float offset_x = 0.0f;
    float offset_z = 0.0f;
  };

  struct SkyParams {
    float time;
    float sun_height;
    float cloudiness;
    Vector3D sun_dir;
    Vector3D fog_color;
  };

  struct OceanSetup {
    float level;
    Vector3D center;
    float half_extent;
    int cells;                    // 300
  };

  struct OceanParams {
    float time;
    Vector3D fog_color;
    float fog_scale;
    Vector3D world_offset;
  };

  // Camera-centered procedural full-geometry grass. The backend samples the
  // authoritative terrain textures and expands compact grid instances into
  // blades on the GPU; no per-blade CPU geometry crosses the API.
  struct GrassParams {
    float radius = 90.0f;
    float spacing = 0.70f;
    int blades_per_cell = 3;
  };

  // The renderer: a game-shaped interface, not a general RHI.  Sky,
  // ocean, terrain and the post effects are backend features with
  // dedicated shaders; a WebGPU backend reimplements this interface
  // rather than translating shaders at runtime.
  //
  // Threading: create_texture/create_mesh/set_terrain are safe to
  // call from the world-generation thread; everything between
  // begin_frame and end_frame must stay on the render thread.
  class Renderer {
  public:
    virtual ~Renderer () {}

    // -- resources ---------------------------------------------------
    virtual TexturePtr create_texture (const TextureDesc& desc,
				       const void* pixels) = 0;
    virtual MeshPtr create_mesh (const DrawList& recorded) = 0;

    // -- world setup -------------------------------------------------
    virtual void set_terrain (const TerrainParams& params,
			      const float* heights,
			      const Vector3D* normals) = 0;
    virtual void set_terrain_textures (TexturePtr grass,
				       TexturePtr dirt,
				       TexturePtr rock,
				       TexturePtr snow) = 0;
    virtual void set_terrain_overlay
      (const TerrainOverlayParams& params, std::span<const float> values) = 0;
    virtual void clear_terrain_overlay () = 0;
    // Renders the one-time terrain shadow map from the fixed sun.
    // light_view_proj maps world to light NDC (conventional Z).
    virtual void render_terrain_shadow (const Mat4& light_view_proj) = 0;
    // Optional standing-water raster turns the ocean grid into the complete
    // surface: sea plus inland lakes. Samples are interleaved pairs of
    // (normalized surface level, wave amplitude factor) following the
    // terrain grid; an empty span retains the single full-swell sea plane.
    virtual void set_ocean (const OceanSetup& setup,
			    std::span<const float> water_levels) = 0;
    // Ground moisture in [0,1] following the terrain grid; vegetation
    // reads it for blade height, color, and density. Optional.
    virtual void set_terrain_moisture (std::span<const float> moisture)
    { (void) moisture; }

    // -- frame -------------------------------------------------------
    virtual bool begin_frame (const FrameParams& params) = 0;
    virtual void draw_terrain (const ChunkDraw* chunks, int count) = 0;
    virtual void draw_sky (const SkyParams& params) = 0;
    virtual void draw_ocean (const OceanParams& params) = 0;
    virtual void draw_grass (const GrassParams& params) { (void) params; }
    virtual void draw_rivers (const Mesh& mesh, const Mat4& model) = 0;
    virtual void draw_mesh (const Mesh& mesh, const Mat4& model) = 0;
    virtual void draw_list (const DrawList& list) = 0;
    // Post effects; call between world drawing and draw_hud.
    virtual void apply_underwater (float time) = 0;
    virtual void apply_motion_blur (float strength) = 0;
    // 2D overlay in point coordinates, y-down, origin top-left.
    virtual void draw_hud (const DrawList& list) = 0;
    // Development capture: the backend writes the next completed frame.
    // Unsupported platforms may leave this as a no-op.
    virtual void request_screenshot (const std::string& path)
    { (void) path; }
    virtual void end_frame () = 0;

    // -- geometry of the drawable -------------------------------------
    virtual int width_pts () const = 0;
    virtual int height_pts () const = 0;
    virtual float scale_factor () const = 0;
  };
}
}

#endif
