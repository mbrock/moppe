// moppe's renderer over NHAL. A frame records, in the game's order: the
// scene (terrain, sky, draw lists and meshes) into a jittered RGBA16F
// colour, RG16F motion, and reversed-Z depth at the scene's scale; the
// temporal resolve into a history at the drawable's size; then the tonemap
// and the HUD into the drawable.
//
// This header is private to the renderer's own files: everyone else
// reaches it through create_renderer (nhal_renderer.hh). NhalRenderer's
// member functions are defined across nhal_renderer.cc and the
// nhal_renderer_*.cc files beside it, a file to each feature (terrain,
// forest, boulders, grass, water, shadow, post, capture) with its
// resources, pipelines, culling, and drawing; the frame around them and
// the scene's generic drawing are in _frame.cc and _scene.cc.
#ifndef MOPPE_NHAL_RENDERER_IMPL_HH
#define MOPPE_NHAL_RENDERER_IMPL_HH

#include <moppe/nhal/renderer/forest_grid.hh>
#include <moppe/nhal/renderer/nhal_renderer.hh>

#include <moppe/environment.hh>
#include <moppe/parallel.hh>

#include <moppe/render/draw.hh>
#include <moppe/render/dynamic_resolution.hh>
#include <moppe/render/ground_detail.hh>
#include <moppe/render/slug.hh>
#include <moppe/render/text.hh>
#include <moppe/render/texture_pixels.hh>
#include <moppe/render/types.hh>
#include <moppe/terrain/sea_waves.hh>

// Every world program's reflection header, as luv-shaderc writes them from
// shaders/world.lisp and CMake lists them, and the streamed vertex's layout.
#include <streamed_vertex.hh>
#include <tree_steps.hh>
#include <world_reflection.hh>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace moppe::nhal {
  namespace detail {
    using render::ChunkDraw;
    using render::DrawList;
    using render::DrawState;
    using render::FrameParams;

    using FrameBlock = shaders::terrain::Frame;
    static_assert (sizeof (FrameBlock) == sizeof (shaders::uber::Frame));
    static_assert (sizeof (FrameBlock) == sizeof (shaders::sky::Frame));
    static_assert (sizeof (FrameBlock) == sizeof (shaders::resolve::Frame));

    // The terrain's chunk levels, as the game's culling chooses them: a
    // step of source samples per rendered cell and vertices per chunk row.
    constexpr int chunk_cells = render::terrain_chunk_cells;
    constexpr int lod_count = int (render::TerrainLod::Count);
    constexpr float lod_step[lod_count] = { 0.25f, 1.0f, 2.0f, 4.0f, 8.0f };
    constexpr int lod_verts[lod_count] = { chunk_cells * 4 + 1,
                                           chunk_cells + 1,
                                           chunk_cells / 2 + 1,
                                           chunk_cells / 4 + 1,
                                           chunk_cells / 8 + 1 };

    // The trunk forest's (species, tier) classes, as forest_trunks.metal
    // numbers them: six detail tiers of birch (and pine, which shares its
    // topology), then six of spruce, then six of fell birch. A class's
    // topology -- facets, masses, and which vertices each triangle joins --
    // is a function of the two alone; world.lisp's forest-vertex places the
    // vertices in the same numbering.
    constexpr std::uint32_t forest_tiers = tree_steps::tiers;
    constexpr std::uint32_t shadow_size = 2048;
    // Indices of the far cascade's stand-in for a tree: a double cone.
    constexpr std::uint32_t far_tree_indices = 36;
    // The most grass tiles a window side may hold: 0.6-metre tiles over
    // 2 x 1.7 x the requested reach.
    constexpr std::uint32_t grass_window_side = 512;
    // A grass tile draws 1, 2, 4, 8, 16, or all 32 of its shoots.
    constexpr std::uint32_t grass_classes = 6;
    // The canopy's 16-metre patches over twice its farthest reach.
    constexpr std::uint32_t sward_window_side = 160;
    // Birch, spruce, and fell birch: each a topology of its own.
    constexpr std::uint32_t forest_species = 3;
    constexpr std::uint32_t forest_classes = forest_species * forest_tiers;

    // The scene pass's targets: RGBA16F colour, RG16F motion, and
    // reversed-Z depth, culling back faces; what every scene program
    // starts from.
    inline RenderPipelineDesc scene_target () {
      RenderPipelineDesc scene;
      scene.color_formats[0] = Format::rgba16_float;
      scene.color_formats[1] = Format::rg16_float;
      scene.color_count = 2;
      scene.depth_format = Format::d32_float;
      scene.cull = Cull::back;
      return scene;
    }

    // The sun's shadow atlas: depth alone, biased against acne.
    inline RenderPipelineDesc shadow_caster () {
      RenderPipelineDesc caster;
      caster.color_count = 0;
      caster.depth_format = Format::d32_float;
      caster.depth_compare = CompareOp::less_equal;
      caster.depth_bias = 2.0f;
      caster.slope_scaled_depth_bias = 2.0f;
      return caster;
    }

    inline int patch_verts (int lod) {
      return (lod_verts[lod] - 1) * render::terrain_patch_cells / chunk_cells +
             1;
    }

    inline std::array<float, 16> matrix (const Mat4& m) {
      std::array<float, 16> out;
      for (int i = 0; i < 16; ++i)
        out[i] = m.element (i);
      return out;
    }

    inline std::array<float, 4> lanes (const Vec3& v, float w = 0) {
      return { v[0], v[1], v[2], w };
    }

    inline std::array<float, 4> linear (DisplayColor c, float w = 0) {
      auto lin = [] (float x) { return std::pow (std::max (x, 0.0f), 2.2f); };
      return { lin (c.red), lin (c.green), lin (c.blue), w };
    }

    // Eight-position zero-mean stratified N-Rooks jitter in scene pixels
    // ({±1, ±3, ±5, ±7}/16 in both axes), ordered so consecutive frames
    // alternate vertical sign and every four frames cover all quadrants.
    // Halves the cycle relative to 16-phase Halton(2,3) to curb low-frequency
    // pulsing on subpixel grazing edges.
    inline constexpr std::array<std::array<float, 2>, 8> temporal_jitter { {
      { 0.0625f, -0.1875f },
      { -0.3125f, 0.3125f },
      { -0.1875f, -0.3125f },
      { 0.3125f, 0.0625f },
      { -0.4375f, -0.0625f },
      { 0.1875f, 0.4375f },
      { 0.4375f, -0.4375f },
      { -0.0625f, 0.1875f },
    } };

    // -- resources the game holds -----------------------------------------

    struct NhalTexture final : render::Texture,
                               std::enable_shared_from_this<NhalTexture> {
      Device* device = nullptr;
      nhal::Texture handle;
      ~NhalTexture () override {
        device->destroy (handle);
      }
    };

    struct MeshBounds {
      position_t centre {};
      displacement_t half_extent {};
      bool visible (const Mat4& clip_from_local) const;
    };

    struct NhalMesh final : render::Mesh,
                            std::enable_shared_from_this<NhalMesh> {
      Device* device = nullptr;
      Buffer vertices, indices;
      IndexType index_type = IndexType::uint32;
      MeshBounds bounds;
      bool wind = false;
      std::vector<DrawList::Run> runs;
      ~NhalMesh () override {
        if (indices)
          device->destroy (indices);
        if (vertices)
          device->destroy (vertices);
      }
    };

    struct NhalGlyphSet final : render::GlyphSet {
      Device* device = nullptr;
      Buffer curves, bands;
      ~NhalGlyphSet () override {
        device->destroy (curves);
        device->destroy (bands);
      }
    };

    // Glyphs placed in the world, a buffer of quads for each glyph set:
    // Direct3D's instance index does not count from a draw's first
    // instance, so runs cannot share one.
    struct NhalWorldText final : render::WorldText {
      struct Run {
        render::GlyphSetPtr glyphs;
        Buffer quads;
        std::uint32_t count = 0;
      };
      Device* device = nullptr;
      std::vector<Run> runs;
      ~NhalWorldText () override {
        for (const Run& run : runs)
          device->destroy (run.quads);
      }
    };

    // -- each feature's state, its file's to manage -----------------------

    // The terrain (nhal_renderer_terrain.cc): its height and normal
    // textures, its material sheets, the chunks' strip templates per level
    // and the chunks the game last placed, and the roads' segments, which
    // the ground reads and the road ribbon lays again near the camera.
    struct TerrainState {
      Pipeline draw, ribbon;
      render::TerrainParams params {};
      bool have = false;
      Texture heights, normals, detail;
      // Ring of snow-relief textures (frames_in_flight + 1) so CPU updates
      // never overwrite shared memory while the GPU still samples an earlier
      // frame's shifted snow window.
      std::vector<Texture> snow;
      std::uint32_t snow_slot = 0;
      std::uint32_t snow_side = 0;
      std::array<float, 4> snow_window {};
      Texture landscape, ground, plants, more_plants, scars, bedrock;
      Buffer chunk_indices[lod_count], patch_indices[lod_count];
      std::uint32_t chunk_index_count[lod_count] {};
      std::uint32_t patch_index_count[lod_count] {};
      std::vector<ChunkDraw> chunks;
      render::Renderer::RoadSetup road_setup {};
      Buffer road_rows, road_index, no_roads;
      // The segments' rows on the CPU too, to choose those near the
      // camera that the road ribbon lays again; and the ribbon's strips.
      std::vector<std::array<float, 4>> road_segments;
      Buffer ribbon_indices;
      std::uint32_t ribbon_index_count = 0;
    };

    // The forest (nhal_renderer_forest.cc): the trees' records, the cull's
    // candidates and indirect draws for the camera and for each of the
    // sun's cascades, the classes' topology, and the stand's rasters.
    struct ForestState {
      ForestGrid grid;
      Buffer classified;
      std::size_t scratch_capacity = 0;
      std::array<std::size_t, 3> capacity {}, lanes {};

      Pipeline draw, cull, shadow, far_shadow, leaves;
      Buffer far_indices;
      Buffer records, candidates, arguments, indices;
      std::uint32_t count = 0;
      std::uint32_t class_first[forest_classes] {};
      std::uint32_t class_count[forest_classes] {};
      Transient block;
      // Per cascade of the sun's map, culled before its pass begins.
      std::array<Buffer, 2> shadow_candidates, shadow_arguments;
      std::array<Transient, 2> shadow_block;
      Texture closure, litter;
      std::array<float, 2> stand_inverse_period {};
    };

    // The boulders (nhal_renderer_boulders.cc): their records, the cull's
    // candidates and draws for the camera and the sun, and the icosahedron.
    struct BoulderState {
      Pipeline draw, shadow, cull;
      Buffer records, candidates, arguments, shadow_candidates,
        shadow_arguments, corners, faces, indices;
      std::uint32_t count = 0;
      float period[2] {};
      Transient block, shadow_block;
    };

    // The undergrowth (nhal_renderer_grass.cc): the sward canopy's patches
    // and the grass tiles the cull chose, and what the game last asked for.
    struct GrassState {
      Pipeline blades, tiles_pipeline, sward, sward_cells_pipeline,
        sward_patches_pipeline;
      render::TexturePtr grain;
      // The cells in view and each 64's count, which put the patches in
      // the window's order (world.lisp's SWARD-CELLS).
      Buffer sward_marks, sward_patches, sward_arguments, sward_indices;
      shaders::grass::Grass sward_block {};
      Buffer tiles, arguments, indices;
      render::UndergrowthParams params {};
      shaders::grass::Grass block {};
      bool wanted = false, culled = false;
    };

    // The water (nhal_renderer_water.cc): its fields on the terrain grid,
    // which patches hold any, and the opaque scene copied out beneath it.
    struct WaterState {
      Pipeline draw, waterfall, scene_copy;
      Texture levels, flow, still, shore_surf;
      std::array<float, 2> shore_size {};
      std::vector<std::uint8_t> wet_patches;
      struct Batch {
        int lod;
        std::size_t first;
      };
      std::vector<Batch> batches;
      std::vector<std::array<float, 4>> patches;
      Texture beneath, beneath_depth;
    };

    // The sun's shadow (nhal_renderer_shadow.cc): its two-cascade atlas,
    // and the meshes lent to the next one (begin_shadow_casters).
    struct SunShadowState {
      struct Caster {
        const NhalMesh* mesh;
        Mat4 model;
      };
      Pipeline terrain, mesh, clear;
      std::optional<render::ShadowCascade> cached_far;
      direction_t cached_sun {};
      std::uint32_t far_frame = 0;
      Texture map;
      std::vector<Caster> casters;
      bool casting = false;
    };

    // The post effects (nhal_renderer_post.cc): their pipelines, the
    // occlusion's and the bloom's targets, the smeared image, and what lies
    // under the water's surface.
    struct PostState {
      Pipeline gtao, gtao_apply, shafts, shafts_apply;
      Pipeline bloom_bright, bloom_blur, motion_blur, underwater;
      shaders::gtao::Post block {};
      Texture ao;
      Texture shafts_half;
      std::uint32_t half_width = 1, half_height = 1;
      Texture bloom[2];
      std::uint32_t bloom_width = 1, bloom_height = 1;
      // The resolved image, smeared (apply_motion_blur).
      Texture blurred;
      Texture submerged;
    };

    class NhalRenderer final : public render::Renderer {
    public:
      NhalRenderer (std::unique_ptr<Device> device,
                    const WorldShaders& shaders,
                    float scale);

      ~NhalRenderer () override;

      // -- resources ------------------------------------------------------

      render::TexturePtr create_texture (const render::TextureDesc& desc,
                                         const void* pixels) override;

      render::MeshPtr create_mesh (const DrawList& recorded) override;

      render::GlyphSetPtr
      create_glyph_set (const render::SlugGlyphData& data) override;

      render::WorldTextPtr
      create_world_text (const render::TextList& text) override;

      // -- world setup ----------------------------------------------------

      void
      set_terrain (const render::TerrainParams& params,
                   std::span<const terrain::SurfaceElevation> heights,
                   std::span<const terrain::TerrainNormal> normals) override;

      void set_sward_texture (render::TexturePtr grain) override;

      // The water sheet on the terrain grid: the surface in the heights'
      // units, and the swell's amplitude.
      void set_ocean (const render::OceanSetup& setup,
                      const render::TexturePixels& levels) override;

      // The current on the terrain grid, metres a second along x, z, and
      // up; the water's ripples and foam stream along it, and it whitens
      // as it descends.
      void set_water_flow (const render::TexturePixels& flow) override;

      void set_shore_surf (const render::TexturePixels& surf) override;

      void
      set_forest (const render::ForestSetup& setup,
                  std::span<const render::ForestInstance> instances) override;

      void set_roads (const RoadSetup& setup) override;

      // The roads' buffers, or a stand-in where there are none: every
      // binding the terrain declares must hold something.
      void bind_roads (int first = 3);
      void set_snow_relief (plan_position_t,
                            meters_t,
                            meters_t,
                            int,
                            std::span<const std::uint8_t>) override;

      void set_boulders (
        const render::BoulderSetup& setup,
        std::span<const render::BoulderInstance> instances) override;

      // The stand's rasters over the forest's period: the canopy's optical
      // closure, splatted from each crown's projected area as Metal splats
      // its stand moments, and the fallen leaves the game spread. Returns
      // each tree's stand closure, the 24-metre mean the crown shapes read.
      std::vector<float>
      spread_stand (const render::ForestSetup& setup,
                    std::span<const render::ForestInstance> instances,
                    const Vec3& period);

      // Binds the stand's rasters where the ground and grass read them.
      void bind_stand ();

      std::array<float, 4> stand_field () const;

      void
      set_terrain_materials (const render::TexturePixels& landscape,
                             const render::TexturePixels& ground,
                             const render::TexturePixels& plants,
                             const render::TexturePixels& more_plants,
                             const render::TexturePixels& scars,
                             const render::TexturePixels& bedrock) override;

      // -- a frame --------------------------------------------------------

      void offer_platform_drawable (void* drawable) override;

      bool begin_frame (const FrameParams& params) override;

      // The sun's shadow in two cascades, side by side in one atlas: terrain
      // chunks and the trees, each species at its coarsest tier, in both;
      // the boulders, too small to matter far off, in the near one alone.
      // Both cascades are culled first, since compute may not run inside
      // the shadow's render pass.
      void begin_shadow_casters () override;

      void end_shadow_casters () override;

      void
      render_local_shadow (const render::LocalShadowParams& params) override;

      // One cascade's casters into the viewport already set on the pass.
      // The far cascade draws the ground two samples a cell: its texels are
      // metres wide.
      void draw_shadow_cascade (const render::ShadowCascade& cascade,
                                std::size_t index,
                                bool trees,
                                bool rocks);

      // The lent meshes' opaque runs: what is blended or glows lets the
      // sun through.
      void draw_mesh_casters (const Transient& caster_block, const Mat4& light);

      void draw_terrain (const ChunkDraw* chunks, int count) override;

      // What the terrain's programs, and the road ribbon's, read.
      void bind_ground ();

      // The ground's textures as every program over it reads them: the
      // heights and normals, the material sheets, the sun's shadow, and
      // the stand.
      void bind_ground_textures ();

      // The built roads' segments near the camera laid again as fine
      // strips over the terrain (road-ribbon in world.lisp), inside the
      // terrain's finest level, which stands at the roads' own heights.
      void draw_road_ribbons ();

      // Whether a chunk holds any water; every chunk may until the game has
      // said which patches are dry.
      void collect_water_patches ();

      // The terrain's chunks as draw_terrain last placed them, under the
      // bound pipeline; the water's pass takes only the wet ones.
      void draw_chunks ();

      void draw_forest () override;

      void draw_undergrowth (const render::UndergrowthParams& params) override;

      void draw_sward ();

      // Each class of tile with the shoots it keeps, from the records the
      // cull sorted into the class's stretch of the window.
      void draw_blades (const render::UndergrowthParams& params);

      void draw_boulders () override;

      void draw_sky (const render::SkyParams&) override;

      // Sea, lakes, and rivers: the terrain's chunks again, lifted to the
      // water sheet, over the opaque scene; the rivers' ripples and foam
      // stream along the current.
      void draw_ocean (const render::OceanParams& params) override;

      // Leaves drifting down where the crowns have turned: a window of
      // world-anchored cells around the camera, one leaf each, those the
      // fallen-leaf raster says stand under gold crowns.
      void draw_falling_leaves () override;
      // The nickpoints' curtains, falling over the water they feed.
      void draw_waterfalls (const render::Mesh& mesh,
                            const Mat4& model) override;

      // A list given a motion id moves as its vertices did: those of the
      // list drawn under the id last frame, where it had as many. So the
      // figure, skinned on the CPU into the world each frame, carries its
      // own motion rather than the camera's alone.
      void draw_list (const DrawList& list, std::uint64_t motion_id) override;

      void draw_world_text (const render::WorldText& text) override;

      void draw_world_labels (const render::TextList& text) override;

      bool in_scene (position_t centre, meters_t radius) const override;
      float scene_focal_pixels () const override;

      void draw_model_glass () override;
      void draw_mesh (const render::Mesh& mesh,
                      const Mat4& model,
                      std::uint64_t motion_id) override;

      // Ends the scene: the rain, last, and the pass closed, leaving its
      // colour for the occlusion and the shafts to shade before the resolve.
      void close_scene ();

      void reconstruct_scene () override;

      // Occlusion over the scene's depth, multiplied into its colour before
      // the resolve, so that the history accumulates it with the rest of
      // the jittered scene it was computed from.
      void apply_gtao (const render::GtaoParams& params) override;

      // Sun shafts marched through the shadow map, added to the scene
      // before the resolve, as the occlusion is.
      void apply_light_shafts (const render::LightShaftParams& params) override;

      void apply_underwater (const render::UnderwaterParams& params) override;

      // The resolved image smeared along the scene's motion into a
      // target of its own, which the present pass then shows: the history
      // stays sharp. `strength` is the share of a frame's motion the
      // streaks span.
      void apply_motion_blur (float strength) override;

      void draw_hud_text (const render::TextList& text) override;

      // The HUD's draw list, then its Slug text over it.
      void draw_hud (const DrawList& list) override;

      void request_screenshot (const std::string& path) override;

      void request_frame (
        std::function<void (const render::FramePixels&)> sink) override;

      // What every half-float value shows as on an 8-bit display: the
      // gamma curve, worked out once for all 65536 of them, so a frame's
      // conversion is a lookup for each channel.
      static const std::array<std::uint8_t, 65536>& display_bytes ();

      // A captured drawable as rows of 8-bit RGB. A recording asks for
      // this sixty times for every second of video, so the rows are shared
      // out among the threads.
      static std::vector<std::uint8_t> capture_rgb (const Capture& capture);

      void end_frame () override;

      // MOPPE_NHAL_TIMINGS=1 prints each pass's GPU time every two
      // seconds, averaged over the frames since the last report.
      void report_timings ();

      void reset_temporal_state () override;

      void set_aspect_limit (float maximum) override;

      render::Viewport viewport_pixels () const override;

      int width_pts () const override;
      int height_pts () const override;
      float scale_factor () const override;

    private:
      // The pipelines, each feature's made in its own file.
      void make_pipelines ();
      bool use_temporal_scaler () const;
      void make_terrain_pipelines ();
      void make_scene_pipelines ();
      void make_forest_pipelines ();
      void make_boulders_pipelines ();
      void make_shadow_pipelines ();
      void make_water_pipelines ();
      void make_grass_pipelines ();
      void make_frame_pipelines ();
      void make_post_pipelines ();

      // Ends the device's frame, first handing any screenshot or frame
      // asked for to its file or sink.
      void end_device_frame ();

      // The ground tiles around the camera that carry blades, counted into
      // the sward's indirect draw: a window of 0.6-metre cells anchored to
      // the world lattice, so a blade keeps its identity as the rider moves.
      void cull_grass (const Mat4& view_proj, const FrameParams& params);

      shaders::gtao::Post post_block (const position_t& eye,
                                      const direction_t& forward,
                                      const Vec3& right,
                                      const Vec3& up) const;

      void upload_frame ();

      // The sea's wave trains at `time` (terrain/sea_waves.hh), which
      // the game's floating things read too.
      static shaders::water::Sea sea_block (double time);

      shaders::terrain::Terrain terrain_block () const;

      // Every boulder tested against a view, coarse or fine for the camera
      // and coarse for the sun, counted into its class's indirect draw.
      void
      cull_rocks (const Mat4& view, const Vec3& eye, float height, bool shadow);

      // Every tree tested against a view, appended to its class's list and
      // counted into the class's indirect draw: eighteen classes for the
      // camera, three (a species each, coarsest tier) for the sun's shadow.
      // Runs outside render passes, which compute passes may not overlap.
      void cull_trees (const Mat4& view,
                       const Vec3& eye,
                       float height,
                       bool shadow,
                       std::size_t cascade = 0);

      // The draw-list pipeline for a run's fixed-function state.
      Pipeline uber (const DrawState& state);

      // Draws runs through their states' uber pipelines, or all of them
      // through `program` when one is given.
      // `streamed_before`, where given, holds each streamed vertex as it
      // was last frame, for the motion.
      void draw_runs (const Transient& streamed,
                      const NhalMesh* mesh,
                      std::span<const DrawList::Run> runs,
                      const Mat4& model,
                      const Mat4& previous,
                      Pipeline program = {},
                      const Transient* streamed_before = nullptr);

      struct GlassRun {
        Transient vertices, previous_vertices;
        std::shared_ptr<const NhalMesh> mesh;
        std::shared_ptr<const NhalTexture> texture;
        DrawList::Run run;
        Mat4 model, previous;
      };
      std::vector<GlassRun> m_glass;

      // The part of an upload from `bytes` on.
      static Transient advanced (Transient slice, std::uint64_t bytes);

      Texture texture_or_white (const render::Texture* texture) const;

      Texture texture_or_white (const render::TexturePtr& texture) const;

      // The 3D scene's share of the play area, by the Metal renderer's rule:
      // point resolution, capped by the megapixel budget, unless overridden.
      float scene_scale (const FrameParams& params) const;

      // Tells the dynamic resolution what the last finished frame cost the
      // GPU, where the game asks for a frame rate and the device times its
      // passes; otherwise the scene keeps its whole size. Frames are due at
      // the rate asked or the display's, whichever is slower.
      void hold_frame_rate (const FrameParams& params);

      // The targets at the play area's size: the temporal history, its
      // motion-blurred copy, and the bloom's quarter of it. A new history
      // starts from the scene alone.
      void make_drawable_targets ();

      // The targets at the scene's size. Nothing in them outlives a frame,
      // and the history is the drawable's, so the scene may change size
      // from one frame to the next with nothing to start over.
      void make_scene_targets (std::uint32_t scene_width,
                               std::uint32_t scene_height);

      // Begins the frame's scene pass, or resumes it where an interlude
      // (the copy beneath the water) closed it.
      void open_scene ();

      // Copies the opaque scene's colour and depth out for the water to
      // read, closing the scene pass around the copy.
      void copy_beneath_water ();

      // Where the sun sits on screen and how much of it the camera sees,
      // fading as it nears the frame's edge: the present pass's glare.
      std::array<float, 4> sun_glare () const;

      // The exposed scene's bright parts at a quarter of the drawable's
      // size, blurred across and then down, ending in m_post.bloom[0].
      void bloom (const shaders::present::Present& present);

      void open_present ();

      std::unique_ptr<Device> m_device;
      const WorldShaders& m_shaders;
      float m_scale;
      float m_aspect_limit = 0;
      // DeviceInfo::indices_count_from_first.
      bool m_first_vertex_counts;

      TerrainState m_terrain;
      ForestState m_forest;
      BoulderState m_boulders;
      GrassState m_grass;
      WaterState m_water;
      SunShadowState m_sun_shadow;
      PostState m_post;

      Pipeline m_sky, m_resolve, m_neighbourhood, m_present, m_hud, m_slug_text,
        m_exposure, m_rain, m_world_text, m_world_labels_behind;
      std::unordered_map<int, Pipeline> m_uber;
      FrameBlock m_frame_values {};
      Buffer m_exposure_value;
      render::TextList m_hud_text;
      // m_clear is zero in every lane: no plant of the field layer anywhere.
      Texture m_white, m_black, m_clear;

      std::uint32_t m_width = 0, m_height = 0;
      std::uint32_t m_scene_width = 0, m_scene_height = 0;
      render::DynamicResolution m_dynamic;
      Texture m_scene_color, m_scene_motion, m_scene_depth, m_history[2];
      // The scene's neighbourhoods for the resolve: low and high bounds,
      // with the motion to follow in their fourth lanes.
      Texture m_bounds[2];
      Texture m_image;

      FrameParams m_params {};
      Transient m_frame_block;
      Mat4 m_previous_view_proj;
      float m_previous_time = 0;
      std::uint32_t m_frame = 0;
      std::array<float, 2> m_frame_jitter { 0.0f, 0.0f };
      bool m_frame_reset = true;
      bool m_last_frame_used_fx = false;
      bool m_restart = true;
      bool m_scene_open = false, m_scene_begun = false, m_scene_closed = false,
           m_resolved = false, m_presented = false;
      std::map<std::uint64_t, Mat4> m_previous_models, m_current_models;
      // The streamed lists' vertices by motion id, last frame's and this.
      std::map<std::uint64_t, std::vector<render::Vertex>> m_previous_vertices,
        m_current_vertices;
      std::string m_screenshot;
      std::function<void (const render::FramePixels&)> m_frame_sink;
      std::map<std::string, double> m_timing_sums;
      int m_timing_frames = 0;
    };
  }
}

#endif
