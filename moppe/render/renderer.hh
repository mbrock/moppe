#ifndef MOPPE_RENDER_RENDERER_HH
#define MOPPE_RENDER_RENDERER_HH

#include <moppe/color.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/texture_pixels.hh>
#include <moppe/render/types.hh>
#include <moppe/terrain/domain.hh>

#include <cstdint>
#include <span>
#include <string>

namespace moppe {
  namespace render {
    inline constexpr float terrain_shore_band_metres = 8.0f;

    // Per-frame environment.  The view matrix already includes the
    // camera-shake rotation; the right/up/forward basis is derived
    // from it and replaces the old GL_MODELVIEW_MATRIX readback for
    // billboards.
    struct FrameParams {
      Mat4 view;
      Mat4 proj; // reversed-Z perspective
      Vec3 camera_pos;
      Vec3 cam_right, cam_up, cam_forward;
      DisplayColor clear_color; // also the fog/haze color
      float fog_scale = 0.0f;
      Vec3 sun_dir; // world space, toward the sun
      // Art-directed sun products.  Ambient is the strength/color fed
      // into the shaders' cool-sky / warm-ground hemisphere fill.
      DisplayColor sun_diffuse;
      DisplayColor sun_specular;
      DisplayColor ambient;
      // Art-direction multiplier applied after automatic exposure.  Tools can
      // favor legibility without changing adaptation for normal gameplay.
      float exposure_bias = 1.0f;
      float time = 0.0f;
      float cloud_cover = 0.0f;
      // How much of the sun the camera can actually see (0..1); the
      // game raymarches the terrain and folds in cloud cover.
      // Drives the present pass's lens flare.
      float sun_visibility = 0.0f;
      UpscalingMode upscaling = UpscalingMode::Temporal;
      float scene_scale = 1.0f;
      float render_scale_override = 0.0f;
      float scene_megapixel_budget = 0.0f;
      bool bloom = true;
      bool auto_exposure = true;
      bool lens_flare = true;
      // Development profiling and GPU capture should ignore loading/UI-only
      // frames and measure the complete world render.
      bool profile = false;
      uint32_t benchmark_mask = 0;
      uint32_t benchmark_partition_mask = 0;
      uint32_t benchmark_epoch = 0;
      uint32_t benchmark_frame = 0;
      bool benchmark_measured = false;
    };

    // A bounded sun-shadow reading for the current camera neighborhood.
    // Geometry becomes unit-blind only at the backend boundary: callers keep
    // the focus as a position point and the reach as a length quantity.
    struct LocalShadowParams {
      Mat4 light_view_proj;
      position_t focus;
      meters_t radius = 160.0f * u::m;
      bool include_forest = true;
    };

    // Sun-shaft raymarch through the camera-local shadow map. The caller
    // supplies the shaken camera basis with the frustum half-extents folded
    // into the right/up vectors, so the backend reconstructs a view ray per
    // pixel without inverting any matrix. Directions are unit-length basis
    // vectors; the measurable quantities keep their units to the boundary.
    struct LightShaftParams {
      position_t camera_pos;
      Vec3 forward;    // unit view direction
      Vec3 right_span; // right * tan(fov/2) * aspect
      Vec3 up_span;    // up * tan(fov/2)
      Vec3 sun_dir;    // toward the sun
      DisplayColor sun_color;
      magnitude_t strength = 1.0f * one;
      meters_t max_distance = 140.0f * u::m;
    };

    // Screen-space ambient occlusion over the stored scene depth. The same
    // shaken camera basis as the sun shafts reconstructs positions; the
    // near and far planes linearize the reversed-Z depth.
    struct GtaoParams {
      position_t camera_pos;
      Vec3 forward;    // unit view direction
      Vec3 right_span; // right * tan(fov/2) * aspect
      Vec3 up_span;    // up * tan(fov/2)
      magnitude_t strength = 1.0f * one;
      meters_t radius = 1.6f * u::m;
      meters_t near_plane = 0.5f * u::m;
      meters_t far_plane = 9000.0f * u::m;
    };

    // World-change-time terrain setup.  Heights/normals are the same
    // arrays the CPU-side physics samples, so sim and render cannot
    // diverge.
    struct TerrainParams {
      int width; // grid samples
      int height;
      Vec3 scale;      // grid step x/z; y is one metre per height unit
      float sea_level; // world metres
      // This world's own land relief: the metres between sea level and its
      // highest ground. Altitude material bands are fractions of it, so a
      // gentle world and an alpine one both grade from meadow to snowfield
      // rather than reading the same absolute metre thresholds.
      float land_relief = 400.0f;
      float tex_scale; // texture repeats per world metre
      float shadow_strength;
      float fog_scale;
      // Development overlay showing the actual terrain triangles. Useful for
      // inspecting the dense reconstructed near field and ordinary LODs.
      bool topology_overlay = false;
      // Light Native and coarser LODs from the full-resolution normal
      // texture at fragment rate, decoupling shading detail from
      // geometric LOD.
      bool fragment_normals = true;
      // Classify snow retention from a broad material-scale surface reading,
      // leaving the detailed normal available for lighting.
      bool snow_support_filter = true;
      // Band and rill the ground along the concentrated-drainage flux so
      // headwater channels read as worked ground below the visible rivers.
      bool channel_flux_detail = true;
      // Diagnostic cover saturation for the grass canopy material; 1 is
      // ordinary habitat-driven cover (see GraphicsSettings).
      float grass_cover_boost = 1.0f;
    };

    enum class TerrainOverlayRamp : uint8_t {
      Heat,
      Flow,
      Streams,
      Categorical,
      Diverging,
      Marker,
      Water,
      Droplet
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
      uint16_t x0, z0; // grid sample origin (multiple of 128)
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
      Vec3 sun_dir;
      DisplayColor fog_color;
    };

    struct OceanSetup {
      float level;
      Vec3 center;
      float half_extent;
      int cells; // 300
    };

    struct OceanParams {
      float time;
      DisplayColor fog_color;
      float fog_scale;
      Vec3 world_offset;
    };

    // Grass and occasional ferns on the forest floor. There is no plant list
    // and no mesh: the backend walks ground tiles around the camera and grows
    // whatever the terrain's own canopy, moisture, and trail fields say
    // stands there. The optional interaction footprint lets the current mover
    // part those generated blades without turning them into retained objects.
    struct UndergrowthParams {
      float time = 0.0f;
      float cloud_cover = 0.0f;
      float reach = 52.0f;  // world metres from the camera
      float density = 1.0f; // art-directed scale over the field's own answer
      Vec3 interaction_position {};
      float interaction_radius = 0.0f;
    };

    enum class ForestSpecies : uint8_t { Broadleaf, Conifer };
    enum class ForestAge : uint8_t { Sapling, Young, Mature, Ancient };

    // One stable individual, still expressed in physical and bounded domain
    // types at the game/renderer boundary. Backends may narrow this to a packed
    // GPU record only after the semantic checks have happened here.
    struct ForestInstance {
      position_t root {};
      terrain::TerrainNormal ground_normal {};
      meters_t height {};
      meters_t crown_radius {};
      proportion_t canopy_cover {};
      proportion_t moisture {};
      std::uint32_t seed = 0;
      ForestSpecies species = ForestSpecies::Broadleaf;
      ForestAge age = ForestAge::Mature;
    };

    struct ForestSetup {
      spatial_extent_t period {};
    };

    struct DustEmission {
      uint64_t id = 0;
      float birth_time = 0.0f;
      Vec3 position;
      Vec3 velocity;
      DisplayColor color;
      float size = 1.0f;
      float life = 1.0f;
      float gravity = 0.0f;
      float spread = 1.0f;
      uint32_t particle_count = 0;
      bool additive = false;
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
      virtual void
      set_terrain (const TerrainParams& params,
                   std::span<const terrain::SurfaceElevation> heights,
                   std::span<const terrain::TerrainNormal> normals) = 0;
      // Hot development control: changes only terrain shading state, leaving
      // height/normal textures and chunk geometry intact.
      virtual void set_terrain_topology_overlay (bool enabled) = 0;
      virtual void set_terrain_textures (TexturePtr grass,
                                         TexturePtr dirt,
                                         TexturePtr rock,
                                         TexturePtr snow) = 0;
      virtual void set_terrain_overlay (const TerrainOverlayParams& params,
                                        std::span<const float> values) = 0;
      virtual void clear_terrain_overlay () = 0;
      // Renders the one-time terrain shadow map from the fixed sun.
      // light_view_proj maps world to light NDC (conventional Z).
      virtual void render_terrain_shadow (const Mat4& light_view_proj,
                                          bool include_forest) = 0;
      // Optional standing-water raster turns the ocean grid into the complete
      // surface: sea plus inland lakes. Samples are interleaved pairs of
      // (physical surface elevation, wave amplitude factor) following the
      // terrain grid; an empty description retains the single full-swell sea
      // plane.
      virtual void set_ocean (const OceanSetup& setup,
                              const TexturePixels& water_levels) = 0;
      // Water flow following the terrain grid: (x, z) world velocity in
      // metres per second per sample. Rivers carry strong downstream arrows,
      // lakes almost none; the water shader advects its surface detail along
      // them. Optional; an empty description clears.
      virtual void set_water_flow (const TexturePixels& flow) {
        (void)flow;
      }
      // Renderer-facing material sheets following the terrain grid. The
      // simulation retains separate typed columns; SurfacePresentation packs
      // them only at this final boundary:
      //
      //   landscape RGBA = moisture, erosion, deposition, forest cover
      //   ground RGBA = shore proximity / 8 m, snow support, trail, home base
      //   flow RG = concentrated drainage x/z
      //
      // The first two are RGBA8Unorm and flow is RG8Snorm. All are filterable
      // and consumed synchronously because TexturePixels borrows its source.
      virtual void set_terrain_materials (const TexturePixels& landscape,
                                          const TexturePixels& ground,
                                          const TexturePixels& flow,
                                          bool include_forest) {
        (void)landscape;
        (void)ground;
        (void)flow;
        (void)include_forest;
      }

      // Stable tree individuals cross once when a finished world is activated.
      // The Metal backend retains compact records and expands reusable organs
      // through object/mesh stages; other backends may choose their own
      // presentation without changing the forest plan.
      virtual void set_forest (const ForestSetup& setup,
                               std::span<const ForestInstance> instances) {
        (void)setup;
        (void)instances;
      }

      // -- frame -------------------------------------------------------
      virtual bool begin_frame (const FrameParams& params) = 0;
      // Encode before the first scene draw. Backends without a dynamic local
      // shadow level may retain their setup-time world map.
      virtual void render_local_shadow (const LocalShadowParams&) {}
      virtual void draw_terrain (const ChunkDraw* chunks, int count) = 0;
      virtual void draw_sky (const SkyParams& params) = 0;
      virtual void draw_ocean (const OceanParams& params) = 0;
      virtual void draw_dust (std::span<const DustEmission> emissions,
                              float logical_time) {
        (void)emissions;
        (void)logical_time;
      }
      // Optional: backends without a mesh pipeline simply grow nothing.
      virtual void draw_undergrowth (const UndergrowthParams& params) {
        (void)params;
      }
      virtual void draw_forest () {}
      // Vertical nickpoint curtains; horizontal water belongs to draw_ocean.
      virtual void draw_waterfalls (const Mesh& mesh, const Mat4& model) = 0;
      // A nonzero motion id names geometry whose prior transform/vertices
      // should be retained for temporal reconstruction. IDs need only remain
      // stable within one renderer and are deliberately absent from Mesh.
      virtual void draw_mesh (const Mesh& mesh,
                              const Mat4& model,
                              uint64_t motion_id = 0) = 0;
      virtual void draw_list (const DrawList& list, uint64_t motion_id = 0) = 0;
      // Resolve the low-resolution 3D scene before screen-space effects. It
      // is idempotent so backend-specific callers can safely enforce the
      // boundary again before HUD/present.
      virtual void reconstruct_scene () {}
      // Post effects; call after reconstruction and before draw_hud.
      // Backends without a stored scene depth may ignore light shafts
      // and ambient occlusion.
      virtual void apply_gtao (const GtaoParams&) {}
      virtual void apply_light_shafts (const LightShaftParams&) {}
      virtual void apply_underwater (float time) = 0;
      virtual void apply_motion_blur (float strength) = 0;
      // Soft-focus the completed 3D scene; HUD drawn afterwards stays crisp.
      virtual void apply_scene_blur () = 0;
      // 2D overlay in point coordinates, y-down, origin top-left.
      virtual void draw_hud (const DrawList& list) = 0;
      // Development capture: the backend writes the next completed frame.
      // Unsupported platforms may leave this as a no-op.
      virtual void request_screenshot (const std::string& path) {
        (void)path;
      }
      virtual void end_frame () = 0;
      virtual bool benchmark_complete () const {
        return false;
      }
      virtual void reset_temporal_state () {}
      virtual void write_benchmark_results () {}

      // -- geometry of the drawable -------------------------------------
      virtual int width_pts () const = 0;
      virtual int height_pts () const = 0;
      virtual float scale_factor () const = 0;
    };
  }
}

#endif
