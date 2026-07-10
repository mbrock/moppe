// Metal backend for moppe/render.  One command buffer per frame:
//
//   scene pass   MSAA 4x -> resolve into sceneA (reversed-Z depth)
//   post passes  underwater grade / motion-blur ghosts on sceneA/B
//   present pass fullscreen quad of the final scene + HUD overlay
//
// Terrain is vertex-pulled from height/normal textures; the shadow
// map is a one-time Depth16Unorm ortho render.  All texture uploads
// go through staging buffers + blit (simulator requires private
// storage; it's the right call on TBDR devices anyway).

#import <TargetConditionals.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

#include <moppe/render/metal/metal_renderer.hh>
#include <moppe/render/metal/shader_types.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace moppe {
namespace render {
  namespace {
    const int FRAMES_IN_FLIGHT = 3;
    const int MSAA_SAMPLES = 4;
    const int SHADOW_SIZE = 4096;
    const int CHUNK_CELLS = 128;
    const int TERRAIN_LOD_COUNT = (int) TerrainLod::Count;
    const int TERRAIN_NATIVE_LOD = (int) TerrainLod::Native;
    const float TERRAIN_LOD_STEP[TERRAIN_LOD_COUNT] = {
      0.5f, 1.0f, 2.0f, 4.0f, 8.0f
    };
    const int TERRAIN_LOD_VERTS[TERRAIN_LOD_COUNT] = {
      CHUNK_CELLS * 2 + 1,
      CHUNK_CELLS + 1,
      CHUNK_CELLS / 2 + 1,
      CHUNK_CELLS / 4 + 1,
      CHUNK_CELLS / 8 + 1
    };
    const int PROBE_W = 32;       // auto-exposure luminance probe
    const int PROBE_H = 16;

    MoppeFloat4 f4 (const Vector3D& v, float w = 0.0f) {
      MoppeFloat4 r; r.x = v.x; r.y = v.y; r.z = v.z; r.w = w;
      return r;
    }

    // Game-side colors are authored in display space; the scene
    // lights in linear.  Decode at the uniform boundary.
    MoppeFloat4 f4lin (const Vector3D& v, float w = 0.0f) {
      MoppeFloat4 r;
      r.x = std::pow (std::max (v.x, 0.0f), 2.2f);
      r.y = std::pow (std::max (v.y, 0.0f), 2.2f);
      r.z = std::pow (std::max (v.z, 0.0f), 2.2f);
      r.w = w;
      return r;
    }

    MoppeMat4 m4 (const Mat4& m) {
      MoppeMat4 r;
      std::memcpy (&r, m.m, sizeof (r));
      return r;
    }

    struct MetalTexture: public Texture {
      id<MTLTexture> texture = nil;
      id<MTLSamplerState> sampler = nil;
    };

    struct MetalMesh: public Mesh {
      id<MTLBuffer> vertices = nil;
      std::vector<DrawList::Run> runs;
    };
  }

  class MetalRenderer: public Renderer {
  public:
    MetalRenderer (MTKView* view, const std::string& lib_path);

    // resources
    TexturePtr create_texture (const TextureDesc& desc,
			       const void* pixels) override;
    MeshPtr create_mesh (const DrawList& recorded) override;

    // world setup
    void set_terrain (const TerrainParams& params,
		      const float* heights,
		      const Vector3D* normals) override;
    void set_terrain_textures (TexturePtr grass, TexturePtr dirt,
			       TexturePtr rock,
			       TexturePtr snow) override;
    void render_terrain_shadow (const Mat4& light_view_proj) override;
    void set_ocean (const OceanSetup& setup) override;

    // frame
    bool begin_frame (const FrameParams& params) override;
    void draw_terrain (const ChunkDraw* chunks, int count) override;
    void draw_sky (const SkyParams& params) override;
    void draw_ocean (const OceanParams& params) override;
    void draw_mesh (const Mesh& mesh, const Mat4& model) override;
    void draw_list (const DrawList& list) override;
    void apply_underwater (float time) override;
    void apply_motion_blur (float strength) override;
    void draw_hud (const DrawList& list) override;
    void end_frame () override;

    int width_pts () const override { return m_width_pts; }
    int height_pts () const override { return m_height_pts; }
    float scale_factor () const override { return m_scale; }

  private:
    void build_pipelines ();
    void ensure_targets ();
    id<MTLTexture> make_target (MTLPixelFormat fmt, int w, int h,
				int samples, bool memoryless);
    void upload_texture (id<MTLTexture> tex, const void* pixels,
			 int w, int h, int bytes_per_pixel,
			 bool gen_mips);
    id<MTLRenderPipelineState> make_pipeline
      (NSString* vs, NSString* fs, MTLPixelFormat color,
       MTLPixelFormat depth, int samples, bool blend,
       bool additive = false);

    // scene-pass encoding helpers
    id<MTLRenderCommandEncoder> scene_encoder ();
    void end_scene_encoder ();
    void update_exposure ();
    void play_list (id<MTLRenderCommandEncoder> enc,
		    const std::vector<Vertex>& verts,
		    const std::vector<DrawList::Run>& runs,
		    bool hud);
    size_t stream_vertices (const std::vector<Vertex>& verts);
    void set_run_state (id<MTLRenderCommandEncoder> enc,
			const DrawState& s, const Texture* tex,
			bool hud);

    MTKView* m_view;
    id<MTLDevice> m_device;
    id<MTLCommandQueue> m_queue;
    id<MTLLibrary> m_library;

    // pipelines
    id<MTLRenderPipelineState> m_uber_opaque = nil, m_uber_blend = nil;
    id<MTLRenderPipelineState> m_uber_add = nil;
    id<MTLRenderPipelineState> m_hud = nil;
    id<MTLRenderPipelineState> m_present = nil, m_ghost = nil;
    id<MTLRenderPipelineState> m_underwater = nil;
    id<MTLRenderPipelineState> m_bloom_bright = nil;
    id<MTLRenderPipelineState> m_bloom_blur = nil;
    id<MTLRenderPipelineState> m_probe = nil;
    id<MTLRenderPipelineState> m_terrain = nil, m_terrain_shadow = nil;
    id<MTLRenderPipelineState> m_sky = nil, m_ocean = nil;

    // depth-stencil: index [test][write], reversed-Z (>=)
    id<MTLDepthStencilState> m_ds[2][2];
    id<MTLDepthStencilState> m_ds_shadow = nil;

    id<MTLSamplerState> m_sampler_repeat = nil;   // mip, aniso
    id<MTLSamplerState> m_sampler_clamp = nil;
    TexturePtr m_white;

    // render targets (drawable-sized; bloom at quarter res)
    id<MTLTexture> m_msaa_color = nil, m_msaa_depth = nil;
    id<MTLTexture> m_scene_a = nil, m_scene_b = nil;
    id<MTLTexture> m_prev_frame = nil;
    id<MTLTexture> m_bloom_a = nil, m_bloom_b = nil;
    bool m_prev_valid = false;

    // Auto exposure: a tiny luminance probe of each frame is read
    // back FRAMES_IN_FLIGHT frames later (safe by the semaphore)
    // and drives a smoothed exposure factor.
    id<MTLTexture> m_probe_tex = nil;
    id<MTLBuffer> m_probe_buf[FRAMES_IN_FLIGHT];
    float m_exposure = 1.0f;
    float m_edr_headroom = 1.0f;
    int m_target_w = 0, m_target_h = 0;
    bool m_memoryless_ok = false;

    // shadow map
    id<MTLTexture> m_shadow_map = nil;
    Mat4 m_light_biased;
    bool m_have_shadow = false;

    // terrain
    id<MTLTexture> m_heights = nil, m_normals = nil;
    id<MTLBuffer> m_terrain_indices[TERRAIN_LOD_COUNT];
    uint32_t m_terrain_index_count[TERRAIN_LOD_COUNT];
    TerrainParams m_terrain_params;
    bool m_have_terrain = false;
    TexturePtr m_grass, m_dirt, m_rock, m_snow;

    // sky dome / ocean grid
    id<MTLBuffer> m_sky_verts = nil;
    uint32_t m_sky_vcount = 0;
    id<MTLBuffer> m_ocean_verts = nil;
    uint32_t m_ocean_vcount = 0;
    float m_ocean_level = 0;

    // streaming
    dispatch_semaphore_t m_inflight;
    int m_slot = 0;
    id<MTLBuffer> m_stream[FRAMES_IN_FLIGHT];
    size_t m_stream_used = 0;

    // per-frame state
    id<MTLCommandBuffer> m_cmd = nil;
    id<MTLRenderCommandEncoder> m_enc = nil;
    id<CAMetalDrawable> m_drawable = nil;
    id<MTLTexture> m_current = nil;   // scene_a or scene_b
    bool m_scene_pass_done = false;
    FrameParams m_fp;
    MoppeFrameUniforms m_fu;

    int m_width_pts = 0, m_height_pts = 0;
    float m_scale = 1.0f;
  };

  // ------------------------------------------------------------------

  MetalRenderer::MetalRenderer (MTKView* view,
				const std::string& lib_path) {
    m_view = view;
    m_device = view.device ? view.device
      : MTLCreateSystemDefaultDevice ();
    m_queue = [m_device newCommandQueue];

    view.device = m_device;
#if TARGET_OS_IPHONE
    view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
#else
    view.colorPixelFormat = MTLPixelFormatRGBA16Float;
#endif
    view.depthStencilPixelFormat = MTLPixelFormatInvalid;
    view.sampleCount = 1;
    view.framebufferOnly = YES;
#if !TARGET_OS_IPHONE
    {
      CGColorSpaceRef linear =
	CGColorSpaceCreateWithName (kCGColorSpaceExtendedLinearSRGB);
      view.colorspace = linear;
      CGColorSpaceRelease (linear);
      CAMetalLayer* layer = (CAMetalLayer*) view.layer;
      layer.wantsExtendedDynamicRangeContent = YES;
      layer.displaySyncEnabled = YES;
    }
    if (view.window)
      m_scale = (float) view.window.backingScaleFactor;
#else
    m_scale = (float) view.contentScaleFactor;
#endif

#if TARGET_OS_SIMULATOR
    m_memoryless_ok = false;
#else
    m_memoryless_ok = [m_device supportsFamily: MTLGPUFamilyApple2];
#endif

    NSError* error = nil;
    NSString* path = [NSString stringWithUTF8String: lib_path.c_str ()];
    NSURL* url = [NSURL fileURLWithPath: path];
    if ([path.pathExtension isEqualToString: @"metal"]) {
      NSString* source = [NSString stringWithContentsOfFile: path
					   encoding: NSUTF8StringEncoding
					      error: &error];
      if (source)
	m_library = [m_device newLibraryWithSource: source
					 options: nil error: &error];
    } else {
      m_library = [m_device newLibraryWithURL: url error: &error];
    }
    if (!m_library) {
      std::cerr << "moppe: failed to load shader library at " << lib_path
		<< ": "
		<< (error ? error.localizedDescription.UTF8String : "?")
		<< std::endl;
      abort ();
    }

    build_pipelines ();

    m_inflight = dispatch_semaphore_create (FRAMES_IN_FLIGHT);
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i)
      m_stream[i] = nil;

    // 1x1 white fallback texture.
    TextureDesc wd;
    wd.width = wd.height = 1;
    wd.format = TextureFormat::RGBA8;
    wd.filter = TextureFilter::Nearest;
    const uint8_t white[4] = { 255, 255, 255, 255 };
    m_white = create_texture (wd, white);
  }

  id<MTLRenderPipelineState>
  MetalRenderer::make_pipeline (NSString* vs, NSString* fs,
				MTLPixelFormat color,
				MTLPixelFormat depth,
				int samples, bool blend,
				bool additive) {
    id<MTLFunction> vf = [m_library newFunctionWithName: vs];
    id<MTLFunction> ff = fs ? [m_library newFunctionWithName: fs] : nil;
    if (!vf || (fs && !ff)) {
      // Shader not present (yet) -- callers treat nil as "skip".
      return nil;
    }

    MTLRenderPipelineDescriptor* d =
      [[MTLRenderPipelineDescriptor alloc] init];
    d.vertexFunction = vf;
    d.fragmentFunction = ff;
    d.rasterSampleCount = samples;
    if (color != MTLPixelFormatInvalid) {
      d.colorAttachments[0].pixelFormat = color;
      if (blend) {
	// Additive keeps src-alpha as the throttle but sums into
	// the framebuffer: overlapping glow builds toward white.
	const MTLBlendFactor dst = additive
	  ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
	d.colorAttachments[0].blendingEnabled = YES;
	d.colorAttachments[0].sourceRGBBlendFactor =
	  MTLBlendFactorSourceAlpha;
	d.colorAttachments[0].destinationRGBBlendFactor = dst;
	d.colorAttachments[0].sourceAlphaBlendFactor =
	  MTLBlendFactorSourceAlpha;
	d.colorAttachments[0].destinationAlphaBlendFactor = dst;
      }
    }
    d.depthAttachmentPixelFormat = depth;

    NSError* error = nil;
    id<MTLRenderPipelineState> pso =
      [m_device newRenderPipelineStateWithDescriptor: d error: &error];
    if (!pso) {
      std::cerr << "moppe: pipeline " << vs.UTF8String << "/"
		<< (fs ? fs.UTF8String : "-") << " failed: "
		<< (error ? error.localizedDescription.UTF8String : "?")
		<< std::endl;
      abort ();
    }
    return pso;
  }

  void
  MetalRenderer::build_pipelines () {
    // The scene renders HDR (half-float, scene-referred with headroom
    // above 1.0). macOS keeps that headroom through an EDR drawable;
    // iOS uses the existing 8-bit SDR presentation path.
    const MTLPixelFormat scene = MTLPixelFormatRGBA16Float;
#if TARGET_OS_IPHONE
    const MTLPixelFormat drawable = MTLPixelFormatBGRA8Unorm;
#else
    const MTLPixelFormat drawable = MTLPixelFormatRGBA16Float;
#endif
    const MTLPixelFormat depth = MTLPixelFormatDepth32Float;

    m_uber_opaque = make_pipeline (@"uber_vertex", @"uber_fragment",
				   scene, depth, MSAA_SAMPLES, false);
    m_uber_blend = make_pipeline (@"uber_vertex", @"uber_fragment",
				  scene, depth, MSAA_SAMPLES, true);
    m_uber_add = make_pipeline (@"uber_vertex", @"uber_fragment",
				scene, depth, MSAA_SAMPLES, true,
				true);
    m_hud = make_pipeline (@"hud_vertex", @"hud_fragment",
			   drawable, MTLPixelFormatInvalid, 1, true);
    m_present = make_pipeline (@"quad_vertex", @"present_fragment",
			       drawable, MTLPixelFormatInvalid, 1,
			       false);
    m_ghost = make_pipeline (@"quad_vertex", @"quad_fragment",
			     scene, MTLPixelFormatInvalid, 1, true);
    m_underwater = make_pipeline (@"quad_vertex",
				  @"underwater_fragment",
				  scene, MTLPixelFormatInvalid, 1,
				  false);
    m_bloom_bright = make_pipeline (@"quad_vertex",
				    @"bloom_bright_fragment",
				    scene, MTLPixelFormatInvalid, 1,
				    false);
    m_bloom_blur = make_pipeline (@"quad_vertex",
				  @"bloom_blur_fragment",
				  scene, MTLPixelFormatInvalid, 1,
				  false);
    m_probe = make_pipeline (@"quad_vertex", @"probe_fragment",
			     MTLPixelFormatRGBA32Float,
			     MTLPixelFormatInvalid, 1, false);
    m_terrain = make_pipeline (@"terrain_vertex", @"terrain_fragment",
			       scene, depth, MSAA_SAMPLES, false);
    m_terrain_shadow = make_pipeline (@"terrain_shadow_vertex", nil,
				      MTLPixelFormatInvalid,
				      MTLPixelFormatDepth16Unorm, 1,
				      false);
    m_sky = make_pipeline (@"sky_vertex", @"sky_fragment",
			   scene, depth, MSAA_SAMPLES, false);
    m_ocean = make_pipeline (@"ocean_vertex", @"ocean_fragment",
			     scene, depth, MSAA_SAMPLES, true);

    // Depth-stencil states, reversed-Z.
    for (int test = 0; test < 2; ++test)
      for (int write = 0; write < 2; ++write) {
	MTLDepthStencilDescriptor* d =
	  [[MTLDepthStencilDescriptor alloc] init];
	d.depthCompareFunction = test
	  ? MTLCompareFunctionGreaterEqual : MTLCompareFunctionAlways;
	d.depthWriteEnabled = write ? YES : NO;
	m_ds[test][write] =
	  [m_device newDepthStencilStateWithDescriptor: d];
      }
    {
      MTLDepthStencilDescriptor* d =
	[[MTLDepthStencilDescriptor alloc] init];
      d.depthCompareFunction = MTLCompareFunctionLessEqual;
      d.depthWriteEnabled = YES;
      m_ds_shadow = [m_device newDepthStencilStateWithDescriptor: d];
    }

    // Samplers.
    {
      MTLSamplerDescriptor* d = [[MTLSamplerDescriptor alloc] init];
      d.minFilter = MTLSamplerMinMagFilterLinear;
      d.magFilter = MTLSamplerMinMagFilterLinear;
      d.mipFilter = MTLSamplerMipFilterLinear;
      d.sAddressMode = MTLSamplerAddressModeRepeat;
      d.tAddressMode = MTLSamplerAddressModeRepeat;
      d.maxAnisotropy = 8;
      m_sampler_repeat = [m_device newSamplerStateWithDescriptor: d];
      d.sAddressMode = MTLSamplerAddressModeClampToEdge;
      d.tAddressMode = MTLSamplerAddressModeClampToEdge;
      d.maxAnisotropy = 1;
      m_sampler_clamp = [m_device newSamplerStateWithDescriptor: d];
    }
  }

  // -- resources -----------------------------------------------------

  void
  MetalRenderer::upload_texture (id<MTLTexture> tex, const void* pixels,
				 int w, int h, int bpp, bool gen_mips) {
    const size_t bytes = (size_t) w * h * bpp;
    id<MTLBuffer> staging =
      [m_device newBufferWithBytes: pixels
			    length: bytes
			   options: MTLResourceStorageModeShared];

    id<MTLCommandBuffer> cmd = [m_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromBuffer: staging
	    sourceOffset: 0
       sourceBytesPerRow: (NSUInteger) w * bpp
     sourceBytesPerImage: bytes
	      sourceSize: MTLSizeMake (w, h, 1)
	       toTexture: tex
	destinationSlice: 0
	destinationLevel: 0
       destinationOrigin: MTLOriginMake (0, 0, 0)];
    if (gen_mips)
      [blit generateMipmapsForTexture: tex];
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
  }

  TexturePtr
  MetalRenderer::create_texture (const TextureDesc& desc,
				 const void* pixels) {
    // RGB8 is expanded to RGBA8 on upload.
    std::vector<uint8_t> expanded;
    const void* data = pixels;
    if (desc.format == TextureFormat::RGB8) {
      const uint8_t* src = (const uint8_t*) pixels;
      expanded.resize ((size_t) desc.width * desc.height * 4);
      for (size_t i = 0; i < (size_t) desc.width * desc.height; ++i) {
	expanded[i * 4 + 0] = src[i * 3 + 0];
	expanded[i * 4 + 1] = src[i * 3 + 1];
	expanded[i * 4 + 2] = src[i * 3 + 2];
	expanded[i * 4 + 3] = 255;
      }
      data = expanded.data ();
    }

    const bool mips = desc.filter == TextureFilter::Mipmap;
    // sRGB view: the hardware decodes texels to linear at sample
    // time (and encodes on mip generation), so albedo textures light
    // correctly in the linear scene.  White + coverage textures
    // (glyphs, particle discs) are unaffected.
    MTLTextureDescriptor* td = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat: MTLPixelFormatRGBA8Unorm_sRGB
				   width: desc.width
				  height: desc.height
			       mipmapped: mips];
    td.storageMode = MTLStorageModePrivate;
    td.usage = MTLTextureUsageShaderRead;

    MetalTexture* t = new MetalTexture ();
    t->width = desc.width;
    t->height = desc.height;
    t->texture = [m_device newTextureWithDescriptor: td];

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = desc.filter == TextureFilter::Nearest
      ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
    sd.magFilter = sd.minFilter;
    sd.mipFilter = mips ? MTLSamplerMipFilterLinear
      : MTLSamplerMipFilterNotMipmapped;
    sd.sAddressMode = desc.wrap == TextureWrap::Repeat
      ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = sd.sAddressMode;
    sd.maxAnisotropy = (NSUInteger) (desc.max_anisotropy < 1
				     ? 1 : desc.max_anisotropy);
    t->sampler = [m_device newSamplerStateWithDescriptor: sd];

    upload_texture (t->texture, data, desc.width, desc.height, 4, mips);
    return TexturePtr (t);
  }

  MeshPtr
  MetalRenderer::create_mesh (const DrawList& recorded) {
    MetalMesh* m = new MetalMesh ();
    m->runs.assign (recorded.runs ().begin (), recorded.runs ().end ());
    if (!recorded.vertices ().empty ())
      m->vertices = [m_device
	newBufferWithBytes: recorded.vertices ().data ()
		    length: recorded.vertices ().size ()
			    * sizeof (Vertex)
		   options: MTLResourceStorageModeShared];
    return MeshPtr (m);
  }

  // -- world setup ---------------------------------------------------

  void
  MetalRenderer::set_terrain (const TerrainParams& params,
			      const float* heights,
			      const Vector3D* normals) {
    m_terrain_params = params;
    const int w = params.width, h = params.height;

    // Heights: R32Float, read() access only.
    {
      MTLTextureDescriptor* td = [MTLTextureDescriptor
	texture2DDescriptorWithPixelFormat: MTLPixelFormatR32Float
				     width: w height: h
				 mipmapped: NO];
      td.storageMode = MTLStorageModePrivate;
      td.usage = MTLTextureUsageShaderRead;
      m_heights = [m_device newTextureWithDescriptor: td];
      upload_texture (m_heights, heights, w, h, 4, false);
    }

    // Normals: RG16Snorm xz, y reconstructed in the shader.
    {
      std::vector<int16_t> packed ((size_t) w * h * 2);
      for (size_t i = 0; i < (size_t) w * h; ++i) {
	const Vector3D& n = normals[i];
	float x = n.x, z = n.z;
	if (x < -1) x = -1; if (x > 1) x = 1;
	if (z < -1) z = -1; if (z > 1) z = 1;
	packed[i * 2 + 0] = (int16_t) (x * 32767.0f);
	packed[i * 2 + 1] = (int16_t) (z * 32767.0f);
      }
      MTLTextureDescriptor* td = [MTLTextureDescriptor
	texture2DDescriptorWithPixelFormat: MTLPixelFormatRG16Snorm
				     width: w height: h
				 mipmapped: NO];
      td.storageMode = MTLStorageModePrivate;
      td.usage = MTLTextureUsageShaderRead;
      m_normals = [m_device newTextureWithDescriptor: td];
      upload_texture (m_normals, packed.data (), w, h, 4, false);
    }

    // Shared chunk-local index templates.  The finest inserts one
    // virtual vertex between source samples; progressively coarser
    // levels use source strides 1, 2, 4, and 8.
    struct Gen {
      static id<MTLBuffer> make (id<MTLDevice> dev, int vpr,
				 uint32_t& count_out) {
	std::vector<uint32_t> idx;
	const int rows = vpr - 1;
	idx.reserve ((size_t) rows * (vpr * 2 + 1));
	for (int r = 0; r < rows; ++r) {
	  for (int x = 0; x < vpr; ++x) {
	    idx.push_back ((uint32_t) (r * vpr + x));
	    idx.push_back ((uint32_t) ((r + 1) * vpr + x));
	  }
	  idx.push_back (0xFFFFFFFFu);   // strip restart
	}
	count_out = (uint32_t) idx.size ();
	return [dev newBufferWithBytes: idx.data ()
				length: idx.size () * 4
			       options: MTLResourceStorageModeShared];
      }
    };
    for (int lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
      m_terrain_indices[lod] = Gen::make
	(m_device, TERRAIN_LOD_VERTS[lod], m_terrain_index_count[lod]);

    m_have_terrain = true;
  }

  void
  MetalRenderer::set_terrain_textures (TexturePtr grass, TexturePtr dirt,
				       TexturePtr rock,
				       TexturePtr snow) {
    m_grass = grass; m_dirt = dirt; m_rock = rock; m_snow = snow;
  }

  void
  MetalRenderer::render_terrain_shadow (const Mat4& light_view_proj) {
    if (!m_terrain_shadow || !m_have_terrain)
      return;

    if (!m_shadow_map) {
      MTLTextureDescriptor* td = [MTLTextureDescriptor
	texture2DDescriptorWithPixelFormat: MTLPixelFormatDepth16Unorm
				     width: SHADOW_SIZE
				    height: SHADOW_SIZE
				 mipmapped: NO];
      td.storageMode = MTLStorageModePrivate;
      td.usage = MTLTextureUsageRenderTarget
	| MTLTextureUsageShaderRead;
      m_shadow_map = [m_device newTextureWithDescriptor: td];
    }

    // Bias: xy from NDC [-1,1] to uv [0,1], with the Metal
    // texture-space Y flip; z is already [0,1].
    Mat4 bias;
    bias.m[0] = 0.5f; bias.m[5] = -0.5f;
    bias.m[12] = 0.5f; bias.m[13] = 0.5f;
    m_light_biased = bias * light_view_proj;

    MTLRenderPassDescriptor* rp =
      [MTLRenderPassDescriptor renderPassDescriptor];
    rp.depthAttachment.texture = m_shadow_map;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionStore;
    rp.depthAttachment.clearDepth = 1.0;

    id<MTLCommandBuffer> cmd = [m_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc =
      [cmd renderCommandEncoderWithDescriptor: rp];
    [enc setRenderPipelineState: m_terrain_shadow];
    [enc setDepthStencilState: m_ds_shadow];
    [enc setCullMode: MTLCullModeNone];
    [enc setFrontFacingWinding: MTLWindingCounterClockwise];
    // Port of glPolygonOffset(2,2) in the GL shadow pass.
    [enc setDepthBias: 2.0f slopeScale: 2.0f clamp: 0.0f];

    MoppeTerrainUniforms u;
    std::memset (&u, 0, sizeof (u));
    u.view_proj = m4 (light_view_proj);
    u.params0.x = m_terrain_params.scale.x;
    u.params0.y = m_terrain_params.scale.y;
    u.params0.z = m_terrain_params.scale.z;
    [enc setVertexBytes: &u length: sizeof (u)
		atIndex: MOPPE_BUF_FRAME];
    [enc setVertexTexture: m_heights atIndex: MOPPE_TEX_HEIGHTS];

    const int chunks = (m_terrain_params.width - 1) / CHUNK_CELLS;
    for (int cz = 0; cz < chunks; ++cz)
      for (int cx = 0; cx < chunks; ++cx) {
	MoppeChunkUniforms c;
	std::memset (&c, 0, sizeof (c));
	c.origin_x = cx * CHUNK_CELLS;
	c.origin_z = cz * CHUNK_CELLS;
	c.step = 1.0f;
	c.verts_per_row = CHUNK_CELLS + 1;
	c.parent_step = 1.0f;
	[enc setVertexBytes: &c length: sizeof (c)
		    atIndex: MOPPE_BUF_CHUNK];
	[enc drawIndexedPrimitives: MTLPrimitiveTypeTriangleStrip
			indexCount: m_terrain_index_count[TERRAIN_NATIVE_LOD]
			 indexType: MTLIndexTypeUInt32
		       indexBuffer: m_terrain_indices[TERRAIN_NATIVE_LOD]
		 indexBufferOffset: 0];
      }
    [enc endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    m_have_shadow = true;
  }

  void
  MetalRenderer::set_ocean (const OceanSetup& setup) {
    // Flat indexed-as-triangles grid at sea level; the vertex shader
    // does the waving.  Built as plain triangles to keep the mesh a
    // single draw.
    const int cells = setup.cells;
    const float step = 2 * setup.half_extent / cells;
    const float x0 = setup.center.x - setup.half_extent;
    const float z0 = setup.center.z - setup.half_extent;

    std::vector<float> verts;
    verts.reserve ((size_t) cells * cells * 6 * 3);
    for (int j = 0; j < cells; ++j)
      for (int i = 0; i < cells; ++i) {
	const float xa = x0 + i * step, xb = xa + step;
	const float za = z0 + j * step, zb = za + step;
	const float y = setup.level;
	const float quad[6][3] = {
	  { xa, y, za }, { xa, y, zb }, { xb, y, za },
	  { xb, y, za }, { xa, y, zb }, { xb, y, zb },
	};
	for (int k = 0; k < 6; ++k) {
	  verts.push_back (quad[k][0]);
	  verts.push_back (quad[k][1]);
	  verts.push_back (quad[k][2]);
	}
      }

    m_ocean_level = setup.level;
    m_ocean_vcount = (uint32_t) (verts.size () / 3);
    m_ocean_verts = [m_device
      newBufferWithBytes: verts.data ()
		  length: verts.size () * sizeof (float)
		 options: MTLResourceStorageModeShared];
  }

  // -- targets -------------------------------------------------------

  id<MTLTexture>
  MetalRenderer::make_target (MTLPixelFormat fmt, int w, int h,
			      int samples, bool memoryless) {
    MTLTextureDescriptor* td = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat: fmt
				   width: w height: h
			       mipmapped: NO];
    td.textureType = samples > 1
      ? MTLTextureType2DMultisample : MTLTextureType2D;
    td.sampleCount = samples;
    td.usage = MTLTextureUsageRenderTarget
      | (samples > 1 ? 0 : MTLTextureUsageShaderRead);
    td.storageMode = (memoryless && m_memoryless_ok)
      ? MTLStorageModeMemoryless : MTLStorageModePrivate;
    return [m_device newTextureWithDescriptor: td];
  }

  void
  MetalRenderer::ensure_targets () {
    const int w = (int) m_view.drawableSize.width;
    const int h = (int) m_view.drawableSize.height;
    if (w == 0 || h == 0)
      return;
    if (w == m_target_w && h == m_target_h && m_msaa_color)
      return;

    m_target_w = w; m_target_h = h;
    m_msaa_color = make_target (MTLPixelFormatRGBA16Float, w, h,
				MSAA_SAMPLES, true);
    m_msaa_depth = make_target (MTLPixelFormatDepth32Float, w, h,
				MSAA_SAMPLES, true);
    m_scene_a = make_target (MTLPixelFormatRGBA16Float, w, h, 1,
			     false);
    m_scene_b = make_target (MTLPixelFormatRGBA16Float, w, h, 1,
			     false);
    m_prev_frame = make_target (MTLPixelFormatRGBA16Float, w, h, 1,
				false);
    const int bw = w / 4 > 0 ? w / 4 : 1;
    const int bh = h / 4 > 0 ? h / 4 : 1;
    m_bloom_a = make_target (MTLPixelFormatRGBA16Float, bw, bh, 1,
			     false);
    m_bloom_b = make_target (MTLPixelFormatRGBA16Float, bw, bh, 1,
			     false);
    if (!m_probe_tex) {
      m_probe_tex = make_target (MTLPixelFormatRGBA32Float,
				 PROBE_W, PROBE_H, 1, false);
      for (int i = 0; i < FRAMES_IN_FLIGHT; ++i)
	m_probe_buf[i] = [m_device
	  newBufferWithLength: PROBE_W * PROBE_H * 16
		      options: MTLResourceStorageModeShared];
    }
    // Freshly created: undefined contents until the first blur blit.
    m_prev_valid = false;
  }

  // Log-average the last completed probe and ease the exposure
  // toward mid-gray; clamped to about a stop either way so night
  // stays night.  Adapting down (a blinding scene) is faster than
  // adapting up, like eyes.
  void
  MetalRenderer::update_exposure () {
    id<MTLBuffer> buf = m_probe_buf[m_slot];
    if (!buf)
      return;
    const float* px = (const float*) buf.contents;
    double sum = 0;
    for (int i = 0; i < PROBE_W * PROBE_H; ++i) {
      const float l = 0.2126f * px[i * 4]
	+ 0.7152f * px[i * 4 + 1]
	+ 0.0722f * px[i * 4 + 2];
      sum += std::log2 (std::max (l, 1e-4f));
    }
    const float avg =
      (float) std::exp2 (sum / (PROBE_W * PROBE_H));
    if (avg <= 1.5e-4f)
      return;   // probe not written yet (startup frames)

    float target = 0.16f / avg;
    target = std::min (1.9f, std::max (0.55f, target));
    const float rate = target < m_exposure ? 0.10f : 0.04f;
    m_exposure += (target - m_exposure) * rate;
  }

  // -- frame ---------------------------------------------------------

  bool
  MetalRenderer::begin_frame (const FrameParams& params) {
    ensure_targets ();
    if (!m_msaa_color)
      return false;

    dispatch_semaphore_wait (m_inflight, DISPATCH_TIME_FOREVER);

    m_drawable = m_view.currentDrawable;
    if (!m_drawable) {
      dispatch_semaphore_signal (m_inflight);
      return false;
    }

    m_slot = (m_slot + 1) % FRAMES_IN_FLIGHT;
    m_stream_used = 0;

    const CGSize points = m_view.bounds.size;
    m_scale = points.width > 0
      ? m_target_w / (float) points.width : 1.0f;
#if !TARGET_OS_IPHONE
    if (m_view.window) {
      NSScreen* screen = m_view.window.screen;
      if (screen)
	m_edr_headroom = std::max
	  (1.0f, (float)
	   screen.maximumExtendedDynamicRangeColorComponentValue);
    }
#endif
    m_width_pts = (int) points.width;
    m_height_pts = (int) points.height;

    m_fp = params;
    std::memset (&m_fu, 0, sizeof (m_fu));
    m_fu.view_proj = m4 (params.proj * params.view);
    m_fu.camera_pos = f4 (params.camera_pos);
    m_fu.sun_dir = f4 (params.sun_dir);
    m_fu.sun_diffuse = f4lin (params.sun_diffuse);
    m_fu.sun_specular = f4lin (params.sun_specular);
    m_fu.ambient = f4lin (params.ambient);
    m_fu.fog_color = f4lin (params.clear_color, params.fog_scale);
    m_fu.misc.x = params.time;

    update_exposure ();

    m_cmd = [m_queue commandBuffer];
    m_current = m_scene_a;
    m_enc = nil;
    m_scene_pass_done = false;
    return true;
  }

  id<MTLRenderCommandEncoder>
  MetalRenderer::scene_encoder () {
    if (m_enc)
      return m_enc;

    MTLRenderPassDescriptor* rp =
      [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = m_msaa_color;
    rp.colorAttachments[0].resolveTexture = m_scene_a;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction =
      MTLStoreActionMultisampleResolve;
    rp.colorAttachments[0].clearColor =
      MTLClearColorMake (std::pow (m_fp.clear_color.x, 2.2f),
			 std::pow (m_fp.clear_color.y, 2.2f),
			 std::pow (m_fp.clear_color.z, 2.2f), 1.0);
    rp.depthAttachment.texture = m_msaa_depth;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    rp.depthAttachment.clearDepth = 0.0;   // reversed-Z far

    m_enc = [m_cmd renderCommandEncoderWithDescriptor: rp];
    [m_enc setFrontFacingWinding: MTLWindingCounterClockwise];
    return m_enc;
  }

  void
  MetalRenderer::end_scene_encoder () {
    if (m_enc) {
      [m_enc endEncoding];
      m_enc = nil;
      m_scene_pass_done = true;
    } else if (!m_scene_pass_done) {
      // Nothing was drawn: run an empty pass so sceneA is cleared.
      scene_encoder ();
      [m_enc endEncoding];
      m_enc = nil;
      m_scene_pass_done = true;
    }
  }

  void
  MetalRenderer::draw_terrain (const ChunkDraw* chunks, int count) {
    if (!m_terrain || !m_have_terrain || count == 0)
      return;
    id<MTLRenderCommandEncoder> enc = scene_encoder ();

    [enc setRenderPipelineState: m_terrain];
    [enc setDepthStencilState: m_ds[1][1]];
    [enc setCullMode: MTLCullModeBack];

    MoppeTerrainUniforms u;
    std::memset (&u, 0, sizeof (u));
    u.view_proj = m_fu.view_proj;
    u.light_matrix = m4 (m_light_biased);
    u.camera_pos = m_fu.camera_pos;
    u.sun_dir = m_fu.sun_dir;
    u.sun_diffuse = m_fu.sun_diffuse;
    u.ambient = m_fu.ambient;
    u.fog_color = m_fu.fog_color;
    u.fog_color.w = m_terrain_params.fog_scale;
    u.params0.x = m_terrain_params.scale.x;
    u.params0.y = m_terrain_params.scale.y;
    u.params0.z = m_terrain_params.scale.z;
    u.params0.w = m_terrain_params.tex_scale;
    u.params1.x = m_terrain_params.height_scale;
    u.params1.y = m_terrain_params.sea_level_norm;
    u.params1.z = m_have_shadow ? m_terrain_params.shadow_strength : 0;
    u.params1.w = 1.0f / SHADOW_SIZE;

    [enc setVertexBytes: &u length: sizeof (u)
		atIndex: MOPPE_BUF_FRAME];
    [enc setFragmentBytes: &u length: sizeof (u)
		  atIndex: MOPPE_BUF_FRAME];
    [enc setVertexTexture: m_heights atIndex: MOPPE_TEX_HEIGHTS];
    [enc setVertexTexture: m_normals atIndex: MOPPE_TEX_NORMALS];

    MetalTexture* grass = (MetalTexture*) m_grass.get ();
    MetalTexture* dirt = (MetalTexture*) m_dirt.get ();
    MetalTexture* rock = (MetalTexture*) m_rock.get ();
    MetalTexture* snow = (MetalTexture*) m_snow.get ();
    if (grass) {
      [enc setFragmentTexture: grass->texture
		      atIndex: MOPPE_TEX_GRASS];
      [enc setFragmentSamplerState: grass->sampler atIndex: 0];
    }
    if (dirt)
      [enc setFragmentTexture: dirt->texture atIndex: MOPPE_TEX_DIRT];
    if (rock)
      [enc setFragmentTexture: rock->texture atIndex: MOPPE_TEX_ROCK];
    if (snow)
      [enc setFragmentTexture: snow->texture atIndex: MOPPE_TEX_SNOW];
    if (m_shadow_map)
      [enc setFragmentTexture: m_shadow_map
		      atIndex: MOPPE_TEX_SHADOW];

    for (int i = 0; i < count; ++i) {
      const int lod = std::max
	(0, std::min (TERRAIN_LOD_COUNT - 1, (int) chunks[i].lod));
      MoppeChunkUniforms c;
      std::memset (&c, 0, sizeof (c));
      c.origin_x = chunks[i].x0;
      c.origin_z = chunks[i].z0;
      c.step = TERRAIN_LOD_STEP[lod];
      c.verts_per_row = TERRAIN_LOD_VERTS[lod];
      c.morph_start = chunks[i].morph_start;
      c.morph_end = chunks[i].morph_end;
      c.parent_step = lod + 1 < TERRAIN_LOD_COUNT
	? TERRAIN_LOD_STEP[lod + 1] : TERRAIN_LOD_STEP[lod];
      [enc setVertexBytes: &c length: sizeof (c)
		  atIndex: MOPPE_BUF_CHUNK];
      [enc drawIndexedPrimitives: MTLPrimitiveTypeTriangleStrip
		      indexCount: m_terrain_index_count[lod]
		       indexType: MTLIndexTypeUInt32
		     indexBuffer: m_terrain_indices[lod]
	       indexBufferOffset: 0];
    }
  }

  void
  MetalRenderer::draw_sky (const SkyParams& params) {
    if (!m_sky)
      return;

    // Build the dome lazily: 24x24 sphere, radius 4000, positions
    // only (matches the old display list).
    if (!m_sky_verts) {
      const float radius = 4000.0f;
      const int slices = 24, stacks = 24;
      std::vector<float> v;
      for (int i = 0; i < stacks; ++i) {
	const float a0 = PI * (float) i / stacks - PI / 2;
	const float a1 = PI * (float) (i + 1) / stacks - PI / 2;
	const float y0 = radius * std::sin (a0);
	const float r0 = radius * std::cos (a0);
	const float y1 = radius * std::sin (a1);
	const float r1 = radius * std::cos (a1);
	for (int j = 0; j < slices; ++j) {
	  const float b0 = PI2 * (float) j / slices;
	  const float b1 = PI2 * (float) (j + 1) / slices;
	  const float p[4][3] = {
	    { r0 * std::cos (b0), y0, r0 * std::sin (b0) },
	    { r0 * std::cos (b1), y0, r0 * std::sin (b1) },
	    { r1 * std::cos (b0), y1, r1 * std::sin (b0) },
	    { r1 * std::cos (b1), y1, r1 * std::sin (b1) },
	  };
	  const int tris[6] = { 0, 2, 1, 1, 2, 3 };
	  for (int k = 0; k < 6; ++k) {
	    v.push_back (p[tris[k]][0]);
	    v.push_back (p[tris[k]][1]);
	    v.push_back (p[tris[k]][2]);
	  }
	}
      }
      m_sky_vcount = (uint32_t) (v.size () / 3);
      m_sky_verts = [m_device
	newBufferWithBytes: v.data ()
		    length: v.size () * sizeof (float)
		   options: MTLResourceStorageModeShared];
    }

    id<MTLRenderCommandEncoder> enc = scene_encoder ();
    [enc setRenderPipelineState: m_sky];
    // Depth test on, write off: terrain occludes the cloud shader.
    [enc setDepthStencilState: m_ds[1][0]];
    [enc setCullMode: MTLCullModeNone];

    // Rotation-only view: the dome follows the camera.
    Mat4 view_rot = m_fp.view;
    view_rot.m[12] = view_rot.m[13] = view_rot.m[14] = 0;

    MoppeSkyUniforms u;
    std::memset (&u, 0, sizeof (u));
    u.view_proj = m4 (m_fp.proj * view_rot);
    u.sun_dir = f4 (params.sun_dir);
    u.fog_color = f4lin (params.fog_color);
    u.params.x = params.time;
    u.params.y = params.sun_height;
    u.params.z = params.cloudiness;

    [enc setVertexBuffer: m_sky_verts offset: 0
		 atIndex: MOPPE_BUF_VERTICES];
    [enc setVertexBytes: &u length: sizeof (u)
		atIndex: MOPPE_BUF_FRAME];
    [enc setFragmentBytes: &u length: sizeof (u)
		  atIndex: MOPPE_BUF_FRAME];
    [enc drawPrimitives: MTLPrimitiveTypeTriangle
	    vertexStart: 0
	    vertexCount: m_sky_vcount];
  }

  void
  MetalRenderer::draw_ocean (const OceanParams& params) {
    if (!m_ocean || !m_ocean_verts)
      return;

    id<MTLRenderCommandEncoder> enc = scene_encoder ();
    [enc setRenderPipelineState: m_ocean];
    [enc setDepthStencilState: m_ds[1][1]];
    [enc setCullMode: MTLCullModeNone];   // visible from below too

    MoppeOceanUniforms u;
    std::memset (&u, 0, sizeof (u));
    u.view_proj = m_fu.view_proj;
    u.camera_pos = m_fu.camera_pos;
    u.sun_dir = m_fu.sun_dir;
    u.fog_color = f4lin (params.fog_color, params.fog_scale);
    u.params.x = params.time;
    u.params.y = m_ocean_level;

    // Shore data: the fragment shader reads the height texture to
    // find the seabed for foam and shallows.
    if (m_have_terrain && m_heights) {
      u.shore.x = 1.0f / m_terrain_params.scale.x;
      u.shore.y = 1.0f / m_terrain_params.scale.z;
      u.shore.z = m_terrain_params.scale.y;
      u.shore.w = (float) m_terrain_params.width;
      [enc setFragmentTexture: m_heights
		      atIndex: MOPPE_TEX_HEIGHTS];
    }

    [enc setVertexBuffer: m_ocean_verts offset: 0
		 atIndex: MOPPE_BUF_VERTICES];
    [enc setVertexBytes: &u length: sizeof (u)
		atIndex: MOPPE_BUF_FRAME];
    [enc setFragmentBytes: &u length: sizeof (u)
		  atIndex: MOPPE_BUF_FRAME];
    [enc drawPrimitives: MTLPrimitiveTypeTriangle
	    vertexStart: 0
	    vertexCount: m_ocean_vcount];
  }

  // -- draw lists ----------------------------------------------------

  size_t
  MetalRenderer::stream_vertices (const std::vector<Vertex>& verts) {
    const size_t bytes = verts.size () * sizeof (Vertex);
    const size_t offset = m_stream_used;
    const size_t needed = offset + bytes;

    id<MTLBuffer> buf = m_stream[m_slot];
    if (!buf || buf.length < needed) {
      size_t cap = buf ? buf.length : (1 << 20);
      while (cap < needed)
	cap *= 2;
      // Old buffer stays retained by prior encoder commands.
      buf = [m_device newBufferWithLength: cap
				  options: MTLResourceStorageModeShared];
      if (offset > 0 && m_stream[m_slot])
	std::memcpy (buf.contents, m_stream[m_slot].contents, offset);
      m_stream[m_slot] = buf;
    }

    std::memcpy ((uint8_t*) buf.contents + offset, verts.data (),
		 bytes);
    m_stream_used = needed;
    return offset;
  }

  void
  MetalRenderer::set_run_state (id<MTLRenderCommandEncoder> enc,
				const DrawState& s, const Texture* tex,
				bool hud) {
    if (hud) {
      [enc setRenderPipelineState: m_hud];
      [enc setCullMode: MTLCullModeNone];
    } else {
      [enc setRenderPipelineState:
	     s.additive ? m_uber_add
	     : s.blend ? m_uber_blend : m_uber_opaque];
      [enc setDepthStencilState:
	     m_ds[s.depth_test ? 1 : 0][s.depth_write ? 1 : 0]];
      [enc setCullMode: s.cull ? MTLCullModeBack : MTLCullModeNone];
    }

    const MetalTexture* mt = (const MetalTexture*) tex;
    if (!mt)
      mt = (const MetalTexture*) m_white.get ();
    [enc setFragmentTexture: mt->texture atIndex: MOPPE_TEX_COLOR];
    [enc setFragmentSamplerState: mt->sampler atIndex: 0];
  }

  void
  MetalRenderer::play_list (id<MTLRenderCommandEncoder> enc,
			    const std::vector<Vertex>& verts,
			    const std::vector<DrawList::Run>& runs,
			    bool hud) {
    if (verts.empty ())
      return;
    const size_t base = stream_vertices (verts);
    [enc setVertexBuffer: m_stream[m_slot] offset: base
		 atIndex: MOPPE_BUF_VERTICES];

    for (size_t i = 0; i < runs.size (); ++i) {
      const DrawList::Run& r = runs[i];
      if (r.count == 0)
	continue;
      set_run_state (enc, r.state, r.texture, hud);
      [enc drawPrimitives: MTLPrimitiveTypeTriangle
	      vertexStart: r.first
	      vertexCount: r.count];
    }
  }

  void
  MetalRenderer::draw_list (const DrawList& list) {
    if (list.empty ())
      return;
    id<MTLRenderCommandEncoder> enc = scene_encoder ();

    MoppeDrawUniforms du;
    std::memset (&du, 0, sizeof (du));
    du.model = m4 (Mat4 ());
    du.nrm0.x = 1; du.nrm1.y = 1; du.nrm2.z = 1;
    [enc setVertexBytes: &du length: sizeof (du)
		atIndex: MOPPE_BUF_DRAW];
    [enc setVertexBytes: &m_fu length: sizeof (m_fu)
		atIndex: MOPPE_BUF_FRAME];
    [enc setFragmentBytes: &m_fu length: sizeof (m_fu)
		  atIndex: MOPPE_BUF_FRAME];

    play_list (enc, list.vertices (), list.runs (), false);
  }

  void
  MetalRenderer::draw_mesh (const Mesh& mesh, const Mat4& model) {
    const MetalMesh& m = (const MetalMesh&) mesh;
    if (!m.vertices)
      return;
    id<MTLRenderCommandEncoder> enc = scene_encoder ();

    MoppeDrawUniforms du;
    std::memset (&du, 0, sizeof (du));
    du.model = m4 (model);
    const NormalMat nm = NormalMat::from (model);
    du.nrm0 = f4 (nm.c0);
    du.nrm1 = f4 (nm.c1);
    du.nrm2 = f4 (nm.c2);
    [enc setVertexBytes: &du length: sizeof (du)
		atIndex: MOPPE_BUF_DRAW];
    [enc setVertexBytes: &m_fu length: sizeof (m_fu)
		atIndex: MOPPE_BUF_FRAME];
    [enc setFragmentBytes: &m_fu length: sizeof (m_fu)
		  atIndex: MOPPE_BUF_FRAME];

    [enc setVertexBuffer: m.vertices offset: 0
		 atIndex: MOPPE_BUF_VERTICES];
    for (size_t i = 0; i < m.runs.size (); ++i) {
      const DrawList::Run& r = m.runs[i];
      if (r.count == 0)
	continue;
      set_run_state (enc, r.state, r.texture, false);
      [enc drawPrimitives: MTLPrimitiveTypeTriangle
	      vertexStart: r.first
	      vertexCount: r.count];
    }
  }

  // -- post ----------------------------------------------------------

  void
  MetalRenderer::apply_underwater (float time) {
    if (!m_underwater)
      return;
    end_scene_encoder ();

    id<MTLTexture> src = m_current;
    id<MTLTexture> dst = (src == m_scene_a) ? m_scene_b : m_scene_a;

    MTLRenderPassDescriptor* rp =
      [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc =
      [m_cmd renderCommandEncoderWithDescriptor: rp];
    MoppeQuadUniforms q;
    std::memset (&q, 0, sizeof (q));
    q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1;
    q.params.x = 1;   // no zoom
    q.params.y = time;
    [enc setRenderPipelineState: m_underwater];
    [enc setVertexBytes: &q length: sizeof (q)
		atIndex: MOPPE_BUF_FRAME];
    [enc setFragmentBytes: &q length: sizeof (q)
		  atIndex: MOPPE_BUF_FRAME];
    [enc setFragmentTexture: src atIndex: MOPPE_TEX_SCENE];
    [enc drawPrimitives: MTLPrimitiveTypeTriangle
	    vertexStart: 0 vertexCount: 3];
    [enc endEncoding];

    m_current = dst;
  }

  void
  MetalRenderer::apply_motion_blur (float strength) {
    if (!m_ghost || strength <= 0.01f)
      return;
    end_scene_encoder ();

    // Ghost quads: previous frame drawn back 3x, zoomed, faded --
    // then the composite becomes the next "previous frame", which
    // is what makes the radial streaks build up.  A freshly
    // (re)created prev texture holds undefined memory: skip the
    // ghosts and just prime it (the old build's m_blur_valid).
    if (!m_prev_valid) {
      id<MTLBlitCommandEncoder> prime = [m_cmd blitCommandEncoder];
      [prime copyFromTexture: m_current toTexture: m_prev_frame];
      [prime endEncoding];
      m_prev_valid = true;
      return;
    }

    MTLRenderPassDescriptor* rp =
      [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = m_current;
    rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc =
      [m_cmd renderCommandEncoderWithDescriptor: rp];
    [enc setRenderPipelineState: m_ghost];
    for (int i = 1; i <= 3; ++i) {
      MoppeQuadUniforms q;
      std::memset (&q, 0, sizeof (q));
      const float alpha = 0.5f * strength / i;
      q.tint.x = q.tint.y = q.tint.z = 1;
      q.tint.w = alpha;
      q.params.x = 1.0f + 0.012f * i * strength;
      [enc setVertexBytes: &q length: sizeof (q)
		  atIndex: MOPPE_BUF_FRAME];
      [enc setFragmentBytes: &q length: sizeof (q)
		    atIndex: MOPPE_BUF_FRAME];
      [enc setFragmentTexture: m_prev_frame
		      atIndex: MOPPE_TEX_SCENE];
      [enc drawPrimitives: MTLPrimitiveTypeTriangle
	      vertexStart: 0 vertexCount: 3];
    }
    [enc endEncoding];

    id<MTLBlitCommandEncoder> blit = [m_cmd blitCommandEncoder];
    [blit copyFromTexture: m_current toTexture: m_prev_frame];
    [blit endEncoding];
  }

  // -- hud + present ---------------------------------------------------

  void
  MetalRenderer::draw_hud (const DrawList& list) {
    end_scene_encoder ();

    // One fullscreen pass into `dst` reading `src`.
    auto quad_pass = [&] (id<MTLRenderPipelineState> pso,
			  id<MTLTexture> src, id<MTLTexture> dst,
			  const MoppeQuadUniforms& q) {
      MTLRenderPassDescriptor* p =
	[MTLRenderPassDescriptor renderPassDescriptor];
      p.colorAttachments[0].texture = dst;
      p.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      p.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTLRenderCommandEncoder> e =
	[m_cmd renderCommandEncoderWithDescriptor: p];
      [e setRenderPipelineState: pso];
      [e setVertexBytes: &q length: sizeof (q)
		atIndex: MOPPE_BUF_FRAME];
      [e setFragmentBytes: &q length: sizeof (q)
		  atIndex: MOPPE_BUF_FRAME];
      [e setFragmentTexture: src atIndex: MOPPE_TEX_SCENE];
      [e drawPrimitives: MTLPrimitiveTypeTriangle
	    vertexStart: 0 vertexCount: 3];
      [e endEncoding];
    };

    // Bloom: bright-pass into quarter res, then a separable blur
    // (a -> b horizontal, b -> a vertical).  The bright pass sees
    // the exposed scene so the glow tracks the eye's adaptation.
    const bool bloom_ok = m_bloom_bright && m_bloom_blur && m_bloom_a;
    if (bloom_ok) {
      MoppeQuadUniforms q;
      std::memset (&q, 0, sizeof (q));
      q.tint.x = q.tint.y = q.tint.z = 1;
      q.tint.w = m_exposure;
      q.params.x = 1;
      quad_pass (m_bloom_bright, m_current, m_bloom_a, q);

      q.params.z = 1.0f / (float) m_bloom_a.width;
      q.params.w = 0;
      quad_pass (m_bloom_blur, m_bloom_a, m_bloom_b, q);
      q.params.z = 0;
      q.params.w = 1.0f / (float) m_bloom_a.height;
      quad_pass (m_bloom_blur, m_bloom_b, m_bloom_a, q);
    }

    // Auto-exposure probe: a 32x16 average of this frame, blitted
    // to a CPU-visible buffer and read FRAMES_IN_FLIGHT frames
    // later in update_exposure().
    if (m_probe && m_probe_tex && m_probe_buf[m_slot]) {
      MoppeQuadUniforms q;
      std::memset (&q, 0, sizeof (q));
      q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1;
      q.params.x = 1;
      quad_pass (m_probe, m_current, m_probe_tex, q);

      id<MTLBlitCommandEncoder> blit = [m_cmd blitCommandEncoder];
      [blit copyFromTexture: m_probe_tex
		sourceSlice: 0
		sourceLevel: 0
	       sourceOrigin: MTLOriginMake (0, 0, 0)
		 sourceSize: MTLSizeMake (PROBE_W, PROBE_H, 1)
		   toBuffer: m_probe_buf[m_slot]
	  destinationOffset: 0
     destinationBytesPerRow: PROBE_W * 16
   destinationBytesPerImage: PROBE_W * PROBE_H * 16];
      [blit endEncoding];
    }

    MTLRenderPassDescriptor* rp =
      [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = m_drawable.texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc =
      [m_cmd renderCommandEncoderWithDescriptor: rp];

    // Scene quad with the tonemap, grade, bloom, and lens flare.
    if (m_present) {
      MoppeQuadUniforms q;
      std::memset (&q, 0, sizeof (q));
      q.tint.x = q.tint.y = q.tint.z = 1;
      q.tint.w = m_exposure;
      q.params.x = 1;
      q.params.y = m_fp.time;
#if !TARGET_OS_IPHONE
      q.params.z = m_edr_headroom;
      q.params.w = 1;
#endif

      // Project the sun onto the screen for the flare; the game
      // supplies the occlusion term, we add the edge fade.
      if (m_fp.sun_visibility > 0.001f) {
	const Mat4 vp = m_fp.proj * m_fp.view;
	const Vector3D sp = m_fp.camera_pos + m_fp.sun_dir * 4000.0f;
	const float* m = vp.m;
	const float cx = m[0]*sp.x + m[4]*sp.y + m[8]*sp.z + m[12];
	const float cy = m[1]*sp.x + m[5]*sp.y + m[9]*sp.z + m[13];
	const float cw = m[3]*sp.x + m[7]*sp.y + m[11]*sp.z + m[15];
	if (cw > 0.01f) {
	  const float nx = cx / cw, ny = cy / cw;
	  const float edge =
	    1.0f - std::max (0.0f, (std::max (std::fabs (nx),
					      std::fabs (ny))
				    - 0.85f) / 0.45f);
	  if (edge > 0.0f) {
	    q.sun.x = nx * 0.5f + 0.5f;
	    q.sun.y = 1.0f - (ny * 0.5f + 0.5f);
	    // Exposure folds in so the flare adapts with the eye.
	    q.sun.z = m_fp.sun_visibility
	      * (edge > 1.0f ? 1.0f : edge) * m_exposure;
	    q.sun.w = (float) m_target_w / (float) m_target_h;
	  }
	}
      }

      [enc setRenderPipelineState: m_present];
      [enc setVertexBytes: &q length: sizeof (q)
		  atIndex: MOPPE_BUF_FRAME];
      [enc setFragmentBytes: &q length: sizeof (q)
		    atIndex: MOPPE_BUF_FRAME];
      [enc setFragmentTexture: m_current atIndex: MOPPE_TEX_SCENE];
      if (bloom_ok)
	[enc setFragmentTexture: m_bloom_a atIndex: MOPPE_TEX_BLOOM];
      [enc drawPrimitives: MTLPrimitiveTypeTriangle
	      vertexStart: 0 vertexCount: 3];
    }

    // HUD overlay in point coordinates.
    if (!list.empty () && m_hud) {
      MoppeHudUniforms hu;
      std::memset (&hu, 0, sizeof (hu));
      hu.proj = m4 (Mat4::hud_ortho ((float) m_width_pts,
				     (float) m_height_pts));
#if !TARGET_OS_IPHONE
      hu.params.x = 1;
#endif
      [enc setVertexBytes: &hu length: sizeof (hu)
		  atIndex: MOPPE_BUF_FRAME];
      [enc setFragmentBytes: &hu length: sizeof (hu)
		    atIndex: MOPPE_BUF_FRAME];
      play_list (enc, list.vertices (), list.runs (), true);
    }

    [enc endEncoding];
  }

  void
  MetalRenderer::end_frame () {
    end_scene_encoder ();   // in case nothing was drawn

    if (m_drawable)
      [m_cmd presentDrawable: m_drawable];

    dispatch_semaphore_t sem = m_inflight;
    [m_cmd addCompletedHandler: ^(id<MTLCommandBuffer>) {
      dispatch_semaphore_signal (sem);
    }];
    [m_cmd commit];

    m_cmd = nil;
    m_drawable = nil;
    m_current = nil;
  }

  // ------------------------------------------------------------------

  Renderer*
  create_metal_renderer (void* mtk_view, const std::string& lib_path) {
    return new MetalRenderer ((__bridge MTKView*) mtk_view, lib_path);
  }
}
}
