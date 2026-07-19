#include <moppe/render/webgpu/webgpu_renderer.hh>

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::render {
  namespace {
    constexpr const char* shader_source = R"wgsl(
struct FrameUniforms {
  view_proj: mat4x4<f32>,
  hud_proj: mat4x4<f32>,
  model: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(1) @binding(0) var color_texture: texture_2d<f32>;
@group(1) @binding(1) var color_sampler: sampler;

struct VertexInput {
  @location(0) position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) color: vec4<f32>,
};

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>,
  @location(1) color: vec4<f32>,
};

@vertex
fn world_vertex(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  output.position = frame.view_proj * frame.model *
                    vec4<f32>(input.position, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}

@vertex
fn hud_vertex(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  output.position = frame.hud_proj * vec4<f32>(input.position, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}

@fragment
fn color_fragment(input: VertexOutput) -> @location(0) vec4<f32> {
  return input.color * textureSample(color_texture, color_sampler, input.uv);
}
)wgsl";

    constexpr const char* terrain_shader_source = R"wgsl(
struct TerrainUniforms {
  view_proj: mat4x4<f32>,
  camera_fog: vec4<f32>,
  sun_direction: vec4<f32>,
  sun_diffuse: vec4<f32>,
  ambient: vec4<f32>,
  fog_color: vec4<f32>,
  terrain_scale: vec4<f32>,
};

struct ChunkUniforms {
  origin_step: vec4<f32>,
  world_offset: vec4<f32>,
};

@group(0) @binding(0) var<uniform> terrain: TerrainUniforms;
@group(0) @binding(1) var heights: texture_2d<f32>;
@group(0) @binding(2) var normals: texture_2d<f32>;
@group(1) @binding(0) var<uniform> chunk: ChunkUniforms;

struct VertexInput {
  @location(0) local_grid: vec2<f32>,
};

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) world_position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) normalized_height: f32,
  @location(3) fog: f32,
};

@vertex
fn terrain_vertex(input: VertexInput) -> VertexOutput {
  let grid = chunk.origin_step.xy + input.local_grid * chunk.origin_step.z;
  let dimensions = vec2<i32>(textureDimensions(heights));
  let sample_position = clamp(vec2<i32>(round(grid)),
                              vec2<i32>(0), dimensions - vec2<i32>(1));
  let height = textureLoad(heights, sample_position, 0).r;
  let packed_normal = textureLoad(normals, sample_position, 0).xyz;
  let normal = normalize(vec3<f32>(packed_normal.x,
                                   max(packed_normal.y, 0.001),
                                   packed_normal.z));
  let world = vec3<f32>(grid.x * terrain.terrain_scale.x +
                          chunk.world_offset.x,
                        height * terrain.terrain_scale.y,
                        grid.y * terrain.terrain_scale.z +
                          chunk.world_offset.z);
  let distance_to_camera = length(world - terrain.camera_fog.xyz);
  var fog = 1.0 - exp(-pow(distance_to_camera * terrain.camera_fog.w, 1.5));
  let lowness = 1.0 - smoothstep(45.0, 170.0, world.y);
  fog += 0.3 * lowness * smoothstep(150.0, 1500.0, distance_to_camera);

  var output: VertexOutput;
  output.position = terrain.view_proj * vec4<f32>(world, 1.0);
  output.world_position = world;
  output.normal = normal;
  output.normalized_height = height;
  output.fog = clamp(fog, 0.0, 1.0);
  return output;
}

@fragment
fn terrain_fragment(input: VertexOutput) -> @location(0) vec4<f32> {
  let slope = 1.0 - clamp(input.normal.y, 0.0, 1.0);
  let sea = terrain.terrain_scale.w;
  let beach = 1.0 - smoothstep(0.012, 0.045,
                               abs(input.normalized_height - sea));
  let grass = vec3<f32>(0.16, 0.34, 0.09);
  let dirt = vec3<f32>(0.34, 0.24, 0.12);
  let rock = vec3<f32>(0.34, 0.35, 0.32);
  let sand = vec3<f32>(0.54, 0.47, 0.29);
  let snow = vec3<f32>(0.78, 0.84, 0.82);
  var material = mix(grass, dirt, smoothstep(0.18, 0.5, slope));
  material = mix(material, rock, smoothstep(0.48, 0.78, slope));
  material = mix(material, sand, beach * (1.0 - smoothstep(0.5, 0.8, slope)));
  let snow_line = smoothstep(0.58, 0.72, input.normalized_height) *
                    (1.0 - smoothstep(0.4, 0.75, slope));
  material = mix(material, snow, snow_line);

  let direct = max(dot(normalize(input.normal),
                       normalize(terrain.sun_direction.xyz)), 0.0);
  let light = terrain.ambient.rgb + terrain.sun_diffuse.rgb * direct;
  let lit = material * max(light, vec3<f32>(0.15));
  return vec4<f32>(mix(lit, terrain.fog_color.rgb, input.fog), 1.0);
}
)wgsl";

    struct FrameUniforms {
      Mat4 view_proj;
      Mat4 hud_proj;
      Mat4 model;
    };

    struct alignas (16) TerrainUniforms {
      Mat4 view_proj;
      std::array<float, 4> camera_fog;
      std::array<float, 4> sun_direction;
      std::array<float, 4> sun_diffuse;
      std::array<float, 4> ambient;
      std::array<float, 4> fog_color;
      std::array<float, 4> terrain_scale;
    };

    struct alignas (16) TerrainChunkUniforms {
      std::array<float, 4> origin_step;
      std::array<float, 4> world_offset;
    };

    constexpr int terrain_lod_count = 4;
    constexpr std::array<float, terrain_lod_count> terrain_lod_steps = {
      1.0f, 2.0f, 4.0f, 8.0f
    };
    constexpr std::array<int, terrain_lod_count> terrain_lod_vertices = {
      129, 65, 33, 17
    };
    constexpr uint32_t frame_uniform_stride = 256;
    constexpr uint32_t frame_uniform_slots = 4096;

    struct WebGpuTexture final : Texture {
      wgpu::Texture texture;
      wgpu::TextureView view;
      wgpu::Sampler sampler;
      wgpu::BindGroup bind_group;
    };

    struct WebGpuMesh final : Mesh {
      std::vector<Vertex> vertices;
      std::vector<DrawList::Run> runs;
      wgpu::Buffer vertex_buffer;
    };

    int pipeline_key (const DrawState& state, bool hud) {
      return (hud ? 1 : 0) | (state.blend ? 2 : 0) | (state.additive ? 4 : 0) |
             (state.depth_test ? 8 : 0) | (state.depth_write ? 16 : 0) |
             (state.cull ? 32 : 0);
    }

    template <typename T>
    wgpu::Buffer upload_buffer (const wgpu::Device& device,
                                const std::vector<T>& values,
                                wgpu::BufferUsage usage) {
      if (values.empty ())
        return {};
      wgpu::BufferDescriptor descriptor {};
      descriptor.size = values.size () * sizeof (T);
      descriptor.usage = usage;
      descriptor.mappedAtCreation = true;
      wgpu::Buffer buffer = device.CreateBuffer (&descriptor);
      std::memcpy (buffer.GetMappedRange (), values.data (), descriptor.size);
      buffer.Unmap ();
      return buffer;
    }
  }

  struct WebGpuRenderer::State {
    std::string canvas_selector;
    int width_points;
    int height_points;
    float scale;
    int width_pixels;
    int height_pixels;

    wgpu::Instance instance;
    wgpu::Surface surface;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::TextureFormat surface_format = wgpu::TextureFormat::Undefined;
    wgpu::Texture depth_texture;
    wgpu::TextureView depth_view;

    wgpu::ShaderModule shader;
    wgpu::BindGroupLayout frame_layout;
    wgpu::BindGroupLayout texture_layout;
    wgpu::PipelineLayout pipeline_layout;
    std::map<int, wgpu::RenderPipeline> pipelines;
    std::shared_ptr<WebGpuTexture> white;

    wgpu::ShaderModule terrain_shader;
    wgpu::BindGroupLayout terrain_frame_layout;
    wgpu::BindGroupLayout terrain_chunk_layout;
    wgpu::PipelineLayout terrain_pipeline_layout;
    wgpu::RenderPipeline terrain_pipeline;
    wgpu::Texture terrain_heights;
    wgpu::TextureView terrain_height_view;
    wgpu::Texture terrain_normals;
    wgpu::TextureView terrain_normal_view;
    std::array<wgpu::Buffer, terrain_lod_count> terrain_vertices;
    std::array<wgpu::Buffer, terrain_lod_count> terrain_indices;
    std::array<uint32_t, terrain_lod_count> terrain_index_counts {};
    TerrainParams terrain_params {};
    TexturePtr terrain_grass;
    TexturePtr terrain_dirt;
    TexturePtr terrain_rock;
    TexturePtr terrain_snow;
    MeshPtr ocean_mesh;
    bool have_terrain = false;

    wgpu::SurfaceTexture surface_texture;
    wgpu::TextureView surface_view;
    wgpu::CommandEncoder command_encoder;
    wgpu::RenderPassEncoder pass;
    wgpu::Buffer frame_buffer;
    wgpu::BindGroup frame_bind_group;
    uint32_t frame_uniform_cursor = 0;
    std::vector<wgpu::Buffer> frame_vertex_buffers;
    std::vector<wgpu::Buffer> frame_uniform_buffers;
    std::vector<wgpu::BindGroup> frame_bind_groups;
    FrameParams frame_params;
    bool frame_open = false;

    State (const char* selector, int width, int height, float factor)
        : canvas_selector (selector), width_points (width),
          height_points (height), scale (factor),
          width_pixels (std::max (1, int (width * factor))),
          height_pixels (std::max (1, int (height * factor))) {}

    void configure_surface () {
      if (!device || !surface)
        return;
      width_pixels = std::max (1, int (width_points * scale));
      height_pixels = std::max (1, int (height_points * scale));

      wgpu::SurfaceConfiguration configuration {};
      configuration.device = device;
      configuration.format = surface_format;
      configuration.usage = wgpu::TextureUsage::RenderAttachment;
      configuration.width = width_pixels;
      configuration.height = height_pixels;
      configuration.alphaMode = wgpu::CompositeAlphaMode::Opaque;
      configuration.presentMode = wgpu::PresentMode::Fifo;
      surface.Configure (&configuration);

      wgpu::TextureDescriptor depth_descriptor {};
      depth_descriptor.size = {
        static_cast<uint32_t> (width_pixels),
        static_cast<uint32_t> (height_pixels),
        1,
      };
      depth_descriptor.format = wgpu::TextureFormat::Depth32Float;
      depth_descriptor.usage = wgpu::TextureUsage::RenderAttachment;
      depth_texture = device.CreateTexture (&depth_descriptor);
      depth_view = depth_texture.CreateView ();
    }

    void build_shared_resources () {
      wgpu::ShaderSourceWGSL wgsl {};
      wgsl.code = shader_source;
      wgpu::ShaderModuleDescriptor shader_descriptor {};
      shader_descriptor.nextInChain = &wgsl;
      shader = device.CreateShaderModule (&shader_descriptor);

      wgpu::BindGroupLayoutEntry frame_entry {};
      frame_entry.binding = 0;
      frame_entry.visibility =
        wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
      frame_entry.buffer.type = wgpu::BufferBindingType::Uniform;
      frame_entry.buffer.hasDynamicOffset = true;
      frame_entry.buffer.minBindingSize = sizeof (FrameUniforms);
      wgpu::BindGroupLayoutDescriptor frame_descriptor {};
      frame_descriptor.entryCount = 1;
      frame_descriptor.entries = &frame_entry;
      frame_layout = device.CreateBindGroupLayout (&frame_descriptor);

      wgpu::BufferDescriptor frame_buffer_descriptor {};
      frame_buffer_descriptor.size = frame_uniform_stride * frame_uniform_slots;
      frame_buffer_descriptor.usage =
        wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
      frame_buffer = device.CreateBuffer (&frame_buffer_descriptor);
      wgpu::BindGroupEntry persistent_frame_entry {};
      persistent_frame_entry.binding = 0;
      persistent_frame_entry.buffer = frame_buffer;
      persistent_frame_entry.size = sizeof (FrameUniforms);
      wgpu::BindGroupDescriptor persistent_frame_descriptor {};
      persistent_frame_descriptor.layout = frame_layout;
      persistent_frame_descriptor.entryCount = 1;
      persistent_frame_descriptor.entries = &persistent_frame_entry;
      frame_bind_group = device.CreateBindGroup (&persistent_frame_descriptor);

      std::array<wgpu::BindGroupLayoutEntry, 2> texture_entries {};
      texture_entries[0].binding = 0;
      texture_entries[0].visibility = wgpu::ShaderStage::Fragment;
      texture_entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
      texture_entries[0].texture.viewDimension =
        wgpu::TextureViewDimension::e2D;
      texture_entries[1].binding = 1;
      texture_entries[1].visibility = wgpu::ShaderStage::Fragment;
      texture_entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
      wgpu::BindGroupLayoutDescriptor texture_descriptor {};
      texture_descriptor.entryCount = texture_entries.size ();
      texture_descriptor.entries = texture_entries.data ();
      texture_layout = device.CreateBindGroupLayout (&texture_descriptor);

      std::array<wgpu::BindGroupLayout, 2> layouts = {
        frame_layout,
        texture_layout,
      };
      wgpu::PipelineLayoutDescriptor layout_descriptor {};
      layout_descriptor.bindGroupLayoutCount = layouts.size ();
      layout_descriptor.bindGroupLayouts = layouts.data ();
      pipeline_layout = device.CreatePipelineLayout (&layout_descriptor);

      TextureDesc white_descriptor;
      white_descriptor.width = 1;
      white_descriptor.height = 1;
      const std::array<unsigned char, 4> white_pixel = { 255, 255, 255, 255 };
      white = std::static_pointer_cast<WebGpuTexture> (
        make_texture (white_descriptor, white_pixel.data ()));
      build_terrain_resources ();
    }

    void build_terrain_resources () {
      wgpu::ShaderSourceWGSL wgsl {};
      wgsl.code = terrain_shader_source;
      wgpu::ShaderModuleDescriptor shader_descriptor {};
      shader_descriptor.nextInChain = &wgsl;
      terrain_shader = device.CreateShaderModule (&shader_descriptor);

      std::array<wgpu::BindGroupLayoutEntry, 3> frame_entries {};
      frame_entries[0].binding = 0;
      frame_entries[0].visibility =
        wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
      frame_entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
      frame_entries[0].buffer.minBindingSize = sizeof (TerrainUniforms);
      frame_entries[1].binding = 1;
      frame_entries[1].visibility = wgpu::ShaderStage::Vertex;
      frame_entries[1].texture.sampleType =
        wgpu::TextureSampleType::UnfilterableFloat;
      frame_entries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
      frame_entries[2].binding = 2;
      frame_entries[2].visibility = wgpu::ShaderStage::Vertex;
      frame_entries[2].texture.sampleType = wgpu::TextureSampleType::Float;
      frame_entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
      wgpu::BindGroupLayoutDescriptor frame_descriptor {};
      frame_descriptor.entryCount = frame_entries.size ();
      frame_descriptor.entries = frame_entries.data ();
      terrain_frame_layout = device.CreateBindGroupLayout (&frame_descriptor);

      wgpu::BindGroupLayoutEntry chunk_entry {};
      chunk_entry.binding = 0;
      chunk_entry.visibility = wgpu::ShaderStage::Vertex;
      chunk_entry.buffer.type = wgpu::BufferBindingType::Uniform;
      chunk_entry.buffer.minBindingSize = sizeof (TerrainChunkUniforms);
      wgpu::BindGroupLayoutDescriptor chunk_descriptor {};
      chunk_descriptor.entryCount = 1;
      chunk_descriptor.entries = &chunk_entry;
      terrain_chunk_layout = device.CreateBindGroupLayout (&chunk_descriptor);

      std::array<wgpu::BindGroupLayout, 2> layouts = {
        terrain_frame_layout,
        terrain_chunk_layout,
      };
      wgpu::PipelineLayoutDescriptor layout_descriptor {};
      layout_descriptor.bindGroupLayoutCount = layouts.size ();
      layout_descriptor.bindGroupLayouts = layouts.data ();
      terrain_pipeline_layout =
        device.CreatePipelineLayout (&layout_descriptor);

      wgpu::VertexAttribute attribute {};
      attribute.format = wgpu::VertexFormat::Float32x2;
      attribute.offset = 0;
      attribute.shaderLocation = 0;
      wgpu::VertexBufferLayout vertex_layout {};
      vertex_layout.arrayStride = sizeof (float) * 2;
      vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
      vertex_layout.attributeCount = 1;
      vertex_layout.attributes = &attribute;

      wgpu::ColorTargetState target {};
      target.format = surface_format;
      target.writeMask = wgpu::ColorWriteMask::All;
      wgpu::FragmentState fragment {};
      fragment.module = terrain_shader;
      fragment.entryPoint = "terrain_fragment";
      fragment.targetCount = 1;
      fragment.targets = &target;
      wgpu::DepthStencilState depth {};
      depth.format = wgpu::TextureFormat::Depth32Float;
      depth.depthWriteEnabled = wgpu::OptionalBool::True;
      depth.depthCompare = wgpu::CompareFunction::GreaterEqual;

      wgpu::RenderPipelineDescriptor pipeline_descriptor {};
      pipeline_descriptor.layout = terrain_pipeline_layout;
      pipeline_descriptor.vertex.module = terrain_shader;
      pipeline_descriptor.vertex.entryPoint = "terrain_vertex";
      pipeline_descriptor.vertex.bufferCount = 1;
      pipeline_descriptor.vertex.buffers = &vertex_layout;
      pipeline_descriptor.primitive.topology =
        wgpu::PrimitiveTopology::TriangleList;
      pipeline_descriptor.primitive.frontFace = wgpu::FrontFace::CCW;
      pipeline_descriptor.primitive.cullMode = wgpu::CullMode::Back;
      pipeline_descriptor.multisample.count = 1;
      pipeline_descriptor.fragment = &fragment;
      pipeline_descriptor.depthStencil = &depth;
      terrain_pipeline = device.CreateRenderPipeline (&pipeline_descriptor);

      for (int lod = 0; lod < terrain_lod_count; ++lod) {
        const int vertices_per_row = terrain_lod_vertices[lod];
        std::vector<std::array<float, 2>> vertices;
        vertices.reserve (vertices_per_row * vertices_per_row);
        for (int z = 0; z < vertices_per_row; ++z)
          for (int x = 0; x < vertices_per_row; ++x)
            vertices.push_back (
              { static_cast<float> (x), static_cast<float> (z) });
        terrain_vertices[lod] =
          upload_buffer (device, vertices, wgpu::BufferUsage::Vertex);

        std::vector<uint32_t> indices;
        const int cells = vertices_per_row - 1;
        indices.reserve (cells * cells * 6);
        for (int z = 0; z < cells; ++z)
          for (int x = 0; x < cells; ++x) {
            const uint32_t a = z * vertices_per_row + x;
            const uint32_t b = a + 1;
            const uint32_t c = a + vertices_per_row;
            const uint32_t d = c + 1;
            indices.insert (indices.end (), { a, c, b, b, c, d });
          }
        terrain_index_counts[lod] = indices.size ();
        terrain_indices[lod] =
          upload_buffer (device, indices, wgpu::BufferUsage::Index);
      }
    }

    TexturePtr make_texture (const TextureDesc& descriptor,
                             const void* pixels) {
      if (!device)
        throw std::logic_error ("WebGPU texture created before device");
      auto result = std::make_shared<WebGpuTexture> ();
      result->width = descriptor.width;
      result->height = descriptor.height;

      wgpu::TextureDescriptor texture_descriptor {};
      texture_descriptor.size = {
        static_cast<uint32_t> (descriptor.width),
        static_cast<uint32_t> (descriptor.height),
        1,
      };
      texture_descriptor.format = wgpu::TextureFormat::RGBA8Unorm;
      texture_descriptor.usage =
        wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
      result->texture = device.CreateTexture (&texture_descriptor);
      result->view = result->texture.CreateView ();

      std::vector<unsigned char> expanded;
      const void* upload = pixels;
      if (descriptor.format == TextureFormat::RGB8) {
        const auto* source = static_cast<const unsigned char*> (pixels);
        expanded.resize (descriptor.width * descriptor.height * 4);
        for (int i = 0; i < descriptor.width * descriptor.height; ++i) {
          expanded[i * 4 + 0] = source[i * 3 + 0];
          expanded[i * 4 + 1] = source[i * 3 + 1];
          expanded[i * 4 + 2] = source[i * 3 + 2];
          expanded[i * 4 + 3] = 255;
        }
        upload = expanded.data ();
      }

      wgpu::TexelCopyTextureInfo destination {};
      destination.texture = result->texture;
      wgpu::TexelCopyBufferLayout layout {};
      layout.bytesPerRow = descriptor.width * 4;
      layout.rowsPerImage = descriptor.height;
      wgpu::Extent3D extent = {
        static_cast<uint32_t> (descriptor.width),
        static_cast<uint32_t> (descriptor.height),
        1,
      };
      queue.WriteTexture (&destination,
                          upload,
                          descriptor.width * descriptor.height * 4,
                          &layout,
                          &extent);

      wgpu::SamplerDescriptor sampler_descriptor {};
      const bool nearest = descriptor.filter == TextureFilter::Nearest;
      sampler_descriptor.magFilter =
        nearest ? wgpu::FilterMode::Nearest : wgpu::FilterMode::Linear;
      sampler_descriptor.minFilter = sampler_descriptor.magFilter;
      sampler_descriptor.mipmapFilter = nearest
                                          ? wgpu::MipmapFilterMode::Nearest
                                          : wgpu::MipmapFilterMode::Linear;
      const wgpu::AddressMode address = descriptor.wrap == TextureWrap::Repeat
                                          ? wgpu::AddressMode::Repeat
                                          : wgpu::AddressMode::ClampToEdge;
      sampler_descriptor.addressModeU = address;
      sampler_descriptor.addressModeV = address;
      result->sampler = device.CreateSampler (&sampler_descriptor);

      std::array<wgpu::BindGroupEntry, 2> entries {};
      entries[0].binding = 0;
      entries[0].textureView = result->view;
      entries[1].binding = 1;
      entries[1].sampler = result->sampler;
      wgpu::BindGroupDescriptor bind_group_descriptor {};
      bind_group_descriptor.layout = texture_layout;
      bind_group_descriptor.entryCount = entries.size ();
      bind_group_descriptor.entries = entries.data ();
      result->bind_group = device.CreateBindGroup (&bind_group_descriptor);
      return result;
    }

    wgpu::RenderPipeline pipeline_for (const DrawState& state, bool hud) {
      const int key = pipeline_key (state, hud);
      if (const auto found = pipelines.find (key); found != pipelines.end ())
        return found->second;

      std::array<wgpu::VertexAttribute, 4> attributes {};
      attributes[0].format = wgpu::VertexFormat::Float32x3;
      attributes[0].offset = 0;
      attributes[0].shaderLocation = 0;
      attributes[1].format = wgpu::VertexFormat::Float32x3;
      attributes[1].offset = 12;
      attributes[1].shaderLocation = 1;
      attributes[2].format = wgpu::VertexFormat::Float32x2;
      attributes[2].offset = 24;
      attributes[2].shaderLocation = 2;
      attributes[3].format = wgpu::VertexFormat::Unorm8x4;
      attributes[3].offset = 32;
      attributes[3].shaderLocation = 3;
      wgpu::VertexBufferLayout vertex_layout {};
      vertex_layout.arrayStride = sizeof (Vertex);
      vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
      vertex_layout.attributeCount = attributes.size ();
      vertex_layout.attributes = attributes.data ();

      wgpu::BlendComponent color_blend {};
      color_blend.operation = wgpu::BlendOperation::Add;
      color_blend.srcFactor = wgpu::BlendFactor::SrcAlpha;
      color_blend.dstFactor = state.additive
                                ? wgpu::BlendFactor::One
                                : wgpu::BlendFactor::OneMinusSrcAlpha;
      wgpu::BlendComponent alpha_blend {};
      alpha_blend.operation = wgpu::BlendOperation::Add;
      alpha_blend.srcFactor = wgpu::BlendFactor::One;
      alpha_blend.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
      wgpu::BlendState blend { color_blend, alpha_blend };

      wgpu::ColorTargetState target {};
      target.format = surface_format;
      if (hud || state.blend || state.additive)
        target.blend = &blend;
      target.writeMask = wgpu::ColorWriteMask::All;

      wgpu::FragmentState fragment {};
      fragment.module = shader;
      fragment.entryPoint = "color_fragment";
      fragment.targetCount = 1;
      fragment.targets = &target;

      wgpu::DepthStencilState depth {};
      depth.format = wgpu::TextureFormat::Depth32Float;
      depth.depthWriteEnabled = !hud && state.depth_write
                                  ? wgpu::OptionalBool::True
                                  : wgpu::OptionalBool::False;
      depth.depthCompare = hud || !state.depth_test
                             ? wgpu::CompareFunction::Always
                             : wgpu::CompareFunction::GreaterEqual;

      wgpu::RenderPipelineDescriptor descriptor {};
      descriptor.layout = pipeline_layout;
      descriptor.vertex.module = shader;
      descriptor.vertex.entryPoint = hud ? "hud_vertex" : "world_vertex";
      descriptor.vertex.bufferCount = 1;
      descriptor.vertex.buffers = &vertex_layout;
      descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
      descriptor.primitive.frontFace = wgpu::FrontFace::CCW;
      descriptor.primitive.cullMode =
        !hud && state.cull ? wgpu::CullMode::Back : wgpu::CullMode::None;
      descriptor.multisample.count = 1;
      descriptor.fragment = &fragment;
      descriptor.depthStencil = &depth;

      wgpu::RenderPipeline pipeline = device.CreateRenderPipeline (&descriptor);
      pipelines.emplace (key, pipeline);
      return pipeline;
    }

    void play_buffer (const wgpu::Buffer& vertex_buffer,
                      std::size_t vertex_count,
                      const std::vector<DrawList::Run>& runs,
                      bool hud,
                      const Mat4& model) {
      if (!frame_open || !vertex_buffer || vertex_count == 0)
        return;
      if (frame_uniform_cursor >= frame_uniform_slots)
        throw std::runtime_error ("WebGPU frame uniform ring exhausted");
      const FrameUniforms uniforms {
        .view_proj = frame_params.proj * frame_params.view,
        .hud_proj = Mat4::hud_ortho (static_cast<float> (width_points),
                                     static_cast<float> (height_points)),
        .model = model,
      };
      const uint32_t uniform_offset =
        frame_uniform_cursor++ * frame_uniform_stride;
      queue.WriteBuffer (
        frame_buffer, uniform_offset, &uniforms, sizeof (uniforms));
      pass.SetVertexBuffer (0, vertex_buffer);
      pass.SetBindGroup (0, frame_bind_group, 1, &uniform_offset);

      for (const DrawList::Run& run : runs) {
        if (run.count == 0)
          continue;
        pass.SetPipeline (pipeline_for (run.state, hud));
        const auto* texture = static_cast<const WebGpuTexture*> (run.texture);
        if (!texture)
          texture = white.get ();
        pass.SetBindGroup (1, texture->bind_group);
        pass.Draw (run.count, 1, run.first, 0);
      }
    }

    void play (const std::vector<Vertex>& vertices,
               const std::vector<DrawList::Run>& runs,
               bool hud) {
      if (!frame_open || vertices.empty ())
        return;
      wgpu::Buffer vertex_buffer =
        upload_buffer (device, vertices, wgpu::BufferUsage::Vertex);
      frame_vertex_buffers.push_back (vertex_buffer);
      play_buffer (
        vertex_buffer, vertices.size (), runs, hud, Mat4::identity ());
    }
  };

  WebGpuRenderer::WebGpuRenderer (const char* canvas_selector,
                                  int width_points,
                                  int height_points,
                                  float scale_factor)
      : m_state (std::make_unique<State> (
          canvas_selector, width_points, height_points, scale_factor)) {
    m_state->instance = wgpu::Instance::Acquire (wgpuCreateInstance (nullptr));
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas {};
    canvas.selector = m_state->canvas_selector.c_str ();
    wgpu::SurfaceDescriptor surface_descriptor {};
    surface_descriptor.nextInChain = &canvas;
    m_state->surface = m_state->instance.CreateSurface (&surface_descriptor);
  }

  WebGpuRenderer::~WebGpuRenderer () = default;

  void WebGpuRenderer::initialize (Ready ready) {
    wgpu::RequestAdapterOptions options {};
    options.compatibleSurface = m_state->surface;
    options.powerPreference = wgpu::PowerPreference::HighPerformance;
    m_state->instance.RequestAdapter (
      &options,
      wgpu::CallbackMode::AllowSpontaneous,
      [this, ready = std::move (ready)] (wgpu::RequestAdapterStatus status,
                                         wgpu::Adapter adapter,
                                         wgpu::StringView message) mutable {
        if (status != wgpu::RequestAdapterStatus::Success) {
          ready (
            false,
            std::string (message.data ? message.data : "", message.length));
          return;
        }
        m_state->adapter = std::move (adapter);
        wgpu::DeviceDescriptor descriptor {};
        descriptor.SetUncapturedErrorCallback ([] (const wgpu::Device&,
                                                   wgpu::ErrorType type,
                                                   wgpu::StringView error) {
          std::cerr << "moppe: WebGPU error " << static_cast<int> (type) << ": "
                    << std::string (error.data ? error.data : "", error.length)
                    << std::endl;
        });
        m_state->adapter.RequestDevice (
          &descriptor,
          wgpu::CallbackMode::AllowSpontaneous,
          [this, ready = std::move (ready)] (wgpu::RequestDeviceStatus result,
                                             wgpu::Device device,
                                             wgpu::StringView error) mutable {
            if (result != wgpu::RequestDeviceStatus::Success) {
              ready (false,
                     std::string (error.data ? error.data : "", error.length));
              return;
            }
            m_state->device = std::move (device);
            m_state->queue = m_state->device.GetQueue ();
            wgpu::SurfaceCapabilities capabilities {};
            m_state->surface.GetCapabilities (m_state->adapter, &capabilities);
            if (capabilities.formatCount == 0) {
              ready (false, "WebGPU surface has no color formats");
              return;
            }
            m_state->surface_format = capabilities.formats[0];
            m_state->configure_surface ();
            m_state->build_shared_resources ();
            ready (true, {});
          });
      });
  }

  void WebGpuRenderer::resize (int width, int height, float factor) {
    m_state->width_points = std::max (1, width);
    m_state->height_points = std::max (1, height);
    m_state->scale = std::max (1.0f, factor);
    m_state->configure_surface ();
  }

  TexturePtr WebGpuRenderer::create_texture (const TextureDesc& descriptor,
                                             const void* pixels) {
    return m_state->make_texture (descriptor, pixels);
  }

  MeshPtr WebGpuRenderer::create_mesh (const DrawList& recorded) {
    auto mesh = std::make_shared<WebGpuMesh> ();
    mesh->vertices = recorded.vertices ();
    mesh->runs = recorded.runs ();
    mesh->vertex_buffer = upload_buffer (
      m_state->device, mesh->vertices, wgpu::BufferUsage::Vertex);
    return mesh;
  }

  void WebGpuRenderer::set_terrain (const TerrainParams& params,
                                    const float* heights,
                                    const Vec3* normals) {
    if (params.width < 2 || params.height < 2 || !heights)
      throw std::invalid_argument ("invalid WebGPU terrain raster");
    m_state->terrain_params = params;

    wgpu::TextureDescriptor height_descriptor {};
    height_descriptor.size = {
      static_cast<uint32_t> (params.width),
      static_cast<uint32_t> (params.height),
      1,
    };
    height_descriptor.format = wgpu::TextureFormat::R32Float;
    height_descriptor.usage =
      wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    m_state->terrain_heights =
      m_state->device.CreateTexture (&height_descriptor);
    m_state->terrain_height_view = m_state->terrain_heights.CreateView ();
    wgpu::TexelCopyTextureInfo height_destination {};
    height_destination.texture = m_state->terrain_heights;
    wgpu::TexelCopyBufferLayout height_layout {};
    height_layout.bytesPerRow = params.width * sizeof (float);
    height_layout.rowsPerImage = params.height;
    const wgpu::Extent3D extent = {
      static_cast<uint32_t> (params.width),
      static_cast<uint32_t> (params.height),
      1,
    };
    m_state->queue.WriteTexture (&height_destination,
                                 heights,
                                 params.width * params.height * sizeof (float),
                                 &height_layout,
                                 &extent);

    std::vector<int8_t> packed_normals (
      static_cast<std::size_t> (params.width) * params.height * 4);
    for (std::size_t i = 0;
         i < static_cast<std::size_t> (params.width) * params.height;
         ++i) {
      const Vec3 normal = normals ? normals[i] : Vec3 (0.0f, 1.0f, 0.0f);
      for (int component = 0; component < 3; ++component) {
        const float value = std::clamp (normal[component], -1.0f, 1.0f);
        packed_normals[i * 4 + component] =
          static_cast<int8_t> (std::round (value * 127.0f));
      }
      packed_normals[i * 4 + 3] = 127;
    }
    wgpu::TextureDescriptor normal_descriptor = height_descriptor;
    normal_descriptor.format = wgpu::TextureFormat::RGBA8Snorm;
    m_state->terrain_normals =
      m_state->device.CreateTexture (&normal_descriptor);
    m_state->terrain_normal_view = m_state->terrain_normals.CreateView ();
    wgpu::TexelCopyTextureInfo normal_destination {};
    normal_destination.texture = m_state->terrain_normals;
    wgpu::TexelCopyBufferLayout normal_layout {};
    normal_layout.bytesPerRow = params.width * 4;
    normal_layout.rowsPerImage = params.height;
    m_state->queue.WriteTexture (&normal_destination,
                                 packed_normals.data (),
                                 packed_normals.size (),
                                 &normal_layout,
                                 &extent);
    m_state->have_terrain = true;
  }
  void WebGpuRenderer::set_terrain_topology_overlay (bool enabled) {
    m_state->terrain_params.topology_overlay = enabled;
  }
  void WebGpuRenderer::set_terrain_textures (TexturePtr grass,
                                             TexturePtr dirt,
                                             TexturePtr rock,
                                             TexturePtr snow) {
    m_state->terrain_grass = std::move (grass);
    m_state->terrain_dirt = std::move (dirt);
    m_state->terrain_rock = std::move (rock);
    m_state->terrain_snow = std::move (snow);
  }
  void WebGpuRenderer::set_terrain_overlay (const TerrainOverlayParams&,
                                            std::span<const float>) {}
  void WebGpuRenderer::clear_terrain_overlay () {}
  void WebGpuRenderer::render_terrain_shadow (const Mat4&) {}
  void WebGpuRenderer::set_ocean (const OceanSetup& setup,
                                  std::span<const float>) {
    DrawList water;
    DrawState state;
    state.blend = true;
    state.cull = false;
    water.state (state);
    water.lit (false);
    water.color (0.08f, 0.38f, 0.52f, 0.72f);
    water.normal (Vec3 (0.0f, 1.0f, 0.0f));
    const float x0 = setup.center[0] - setup.half_extent;
    const float x1 = setup.center[0] + setup.half_extent;
    const float z0 = setup.center[2] - setup.half_extent;
    const float z1 = setup.center[2] + setup.half_extent;
    water.begin (Prim::Triangles);
    water.vertex (x0, setup.level, z0);
    water.vertex (x0, setup.level, z1);
    water.vertex (x1, setup.level, z0);
    water.vertex (x1, setup.level, z0);
    water.vertex (x0, setup.level, z1);
    water.vertex (x1, setup.level, z1);
    water.end ();
    m_state->ocean_mesh = create_mesh (water);
  }

  bool WebGpuRenderer::begin_frame (const FrameParams& params) {
    if (!m_state->device || m_state->frame_open)
      return false;
    m_state->surface_texture = {};
    m_state->surface.GetCurrentTexture (&m_state->surface_texture);
    if (!m_state->surface_texture.texture)
      return false;
    m_state->surface_view = m_state->surface_texture.texture.CreateView ();
    m_state->command_encoder = m_state->device.CreateCommandEncoder ();
    m_state->frame_params = params;
    m_state->frame_vertex_buffers.clear ();
    m_state->frame_uniform_buffers.clear ();
    m_state->frame_bind_groups.clear ();
    m_state->frame_uniform_cursor = 0;

    wgpu::RenderPassColorAttachment color {};
    color.view = m_state->surface_view;
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = {
      params.clear_color.red,
      params.clear_color.green,
      params.clear_color.blue,
      1.0,
    };
    wgpu::RenderPassDepthStencilAttachment depth {};
    depth.view = m_state->depth_view;
    depth.depthClearValue = 0.0f;
    depth.depthLoadOp = wgpu::LoadOp::Clear;
    depth.depthStoreOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor pass_descriptor {};
    pass_descriptor.colorAttachmentCount = 1;
    pass_descriptor.colorAttachments = &color;
    pass_descriptor.depthStencilAttachment = &depth;
    m_state->pass = m_state->command_encoder.BeginRenderPass (&pass_descriptor);
    m_state->frame_open = true;
    return true;
  }

  void WebGpuRenderer::draw_terrain (const ChunkDraw* chunks, int count) {
    if (!m_state->frame_open || !m_state->have_terrain || count < 1)
      return;
    const FrameParams& frame = m_state->frame_params;
    const TerrainParams& terrain = m_state->terrain_params;
    const TerrainUniforms uniforms {
      .view_proj = frame.proj * frame.view,
      .camera_fog = { frame.camera_pos[0],
                      frame.camera_pos[1],
                      frame.camera_pos[2],
                      terrain.fog_scale },
      .sun_direction = { frame.sun_dir[0],
                         frame.sun_dir[1],
                         frame.sun_dir[2],
                         0.0f },
      .sun_diffuse = { frame.sun_diffuse.red,
                       frame.sun_diffuse.green,
                       frame.sun_diffuse.blue,
                       1.0f },
      .ambient = { frame.ambient.red,
                   frame.ambient.green,
                   frame.ambient.blue,
                   1.0f },
      .fog_color = { frame.clear_color.red,
                     frame.clear_color.green,
                     frame.clear_color.blue,
                     1.0f },
      .terrain_scale = { terrain.scale[0],
                         terrain.scale[1],
                         terrain.scale[2],
                         terrain.sea_level_norm },
    };
    std::vector<TerrainUniforms> terrain_uniforms { uniforms };
    wgpu::Buffer terrain_buffer = upload_buffer (
      m_state->device, terrain_uniforms, wgpu::BufferUsage::Uniform);
    m_state->frame_uniform_buffers.push_back (terrain_buffer);
    std::array<wgpu::BindGroupEntry, 3> terrain_entries {};
    terrain_entries[0].binding = 0;
    terrain_entries[0].buffer = terrain_buffer;
    terrain_entries[0].size = sizeof (TerrainUniforms);
    terrain_entries[1].binding = 1;
    terrain_entries[1].textureView = m_state->terrain_height_view;
    terrain_entries[2].binding = 2;
    terrain_entries[2].textureView = m_state->terrain_normal_view;
    wgpu::BindGroupDescriptor terrain_descriptor {};
    terrain_descriptor.layout = m_state->terrain_frame_layout;
    terrain_descriptor.entryCount = terrain_entries.size ();
    terrain_descriptor.entries = terrain_entries.data ();
    wgpu::BindGroup terrain_group =
      m_state->device.CreateBindGroup (&terrain_descriptor);
    m_state->frame_bind_groups.push_back (terrain_group);

    m_state->pass.SetPipeline (m_state->terrain_pipeline);
    m_state->pass.SetBindGroup (0, terrain_group);
    for (int i = 0; i < count; ++i) {
      const int requested_lod = static_cast<int> (chunks[i].lod);
      const int lod = std::clamp (requested_lod - 1, 0, terrain_lod_count - 1);
      const TerrainChunkUniforms chunk_uniforms {
        .origin_step = { static_cast<float> (chunks[i].x0),
                         static_cast<float> (chunks[i].z0),
                         terrain_lod_steps[lod],
                         0.0f },
        .world_offset = { chunks[i].offset_x, 0.0f, chunks[i].offset_z, 0.0f },
      };
      std::vector<TerrainChunkUniforms> chunk_values { chunk_uniforms };
      wgpu::Buffer chunk_buffer = upload_buffer (
        m_state->device, chunk_values, wgpu::BufferUsage::Uniform);
      m_state->frame_uniform_buffers.push_back (chunk_buffer);
      wgpu::BindGroupEntry chunk_entry {};
      chunk_entry.binding = 0;
      chunk_entry.buffer = chunk_buffer;
      chunk_entry.size = sizeof (TerrainChunkUniforms);
      wgpu::BindGroupDescriptor chunk_descriptor {};
      chunk_descriptor.layout = m_state->terrain_chunk_layout;
      chunk_descriptor.entryCount = 1;
      chunk_descriptor.entries = &chunk_entry;
      wgpu::BindGroup chunk_group =
        m_state->device.CreateBindGroup (&chunk_descriptor);
      m_state->frame_bind_groups.push_back (chunk_group);
      m_state->pass.SetBindGroup (1, chunk_group);
      m_state->pass.SetVertexBuffer (0, m_state->terrain_vertices[lod]);
      m_state->pass.SetIndexBuffer (m_state->terrain_indices[lod],
                                    wgpu::IndexFormat::Uint32);
      m_state->pass.DrawIndexed (m_state->terrain_index_counts[lod]);
    }
  }
  void WebGpuRenderer::draw_sky (const SkyParams&) {}
  void WebGpuRenderer::draw_ocean (const OceanParams& params) {
    if (!m_state->ocean_mesh)
      return;
    draw_mesh (*m_state->ocean_mesh, Mat4::translation (params.world_offset));
  }
  void WebGpuRenderer::draw_rivers (const Mesh& mesh, const Mat4& model) {
    draw_mesh (mesh, model);
  }

  void WebGpuRenderer::draw_mesh (const Mesh& mesh, const Mat4& model) {
    const auto& source = static_cast<const WebGpuMesh&> (mesh);
    m_state->play_buffer (
      source.vertex_buffer, source.vertices.size (), source.runs, false, model);
  }

  void WebGpuRenderer::draw_list (const DrawList& list) {
    m_state->play (list.vertices (), list.runs (), false);
  }

  void WebGpuRenderer::apply_underwater (float) {}
  void WebGpuRenderer::apply_motion_blur (float) {}
  void WebGpuRenderer::apply_scene_blur () {}

  void WebGpuRenderer::draw_hud (const DrawList& list) {
    m_state->play (list.vertices (), list.runs (), true);
  }

  void WebGpuRenderer::end_frame () {
    if (!m_state->frame_open)
      return;
    m_state->pass.End ();
    wgpu::CommandBuffer command = m_state->command_encoder.Finish ();
    m_state->queue.Submit (1, &command);
    m_state->frame_open = false;
    m_state->pass = {};
    m_state->command_encoder = {};
    m_state->surface_view = {};
    m_state->surface_texture = {};
  }

  int WebGpuRenderer::width_pts () const {
    return m_state->width_points;
  }
  int WebGpuRenderer::height_pts () const {
    return m_state->height_points;
  }
  float WebGpuRenderer::scale_factor () const {
    return m_state->scale;
  }
}
