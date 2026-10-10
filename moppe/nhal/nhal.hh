// NHAL: a small hardware layer for Metal 4, Direct3D 12, Vulkan, and
// WebGPU (docs/nhal.md).
//
// Resources are handles into the device's tables. Destroying one retires it
// once the frames that might still read it have completed. Between
// begin_frame and end_frame the device records one command stream: render
// passes, draws, and their bindings by binding number, in the families the
// program's reflection declares. Uniforms and other per-frame data go into
// the frame's upload arena and are bound by GPU address.
//
// Barriers are the device's business: Direct3D 12 tracks each texture's
// state and transitions it where a pass or a binding needs it; Metal 4
// orders passes with queue-stage barriers; Vulkan tracks image layouts and
// puts a full memory barrier before each pass, dispatch, and copy; WebGPU
// orders and synchronizes passes itself.
#ifndef MOPPE_NHAL_NHAL_HH
#define MOPPE_NHAL_NHAL_HH

#include <moppe/nhal/reflection.hh>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace moppe::nhal {
  enum class Format : std::uint8_t {
    undefined,
    rgba8_unorm,
    rgba8_unorm_srgb,
    bgra8_unorm,
    bgra8_unorm_srgb,
    rgb10a2_unorm,
    rgba16_float,
    rg16_float,
    rg16_snorm,
    r16_float,
    r8_unorm,
    r32_float,
    rg32_float,
    rgba32_float,
    r32_uint,
    d32_float,
  };

  constexpr bool is_depth (Format format) {
    return format == Format::d32_float;
  }

  constexpr std::uint32_t bytes_per_pixel (Format format) {
    switch (format) {
    case Format::undefined: return 0;
    case Format::rgba8_unorm:
    case Format::rgba8_unorm_srgb:
    case Format::bgra8_unorm:
    case Format::bgra8_unorm_srgb:
    case Format::rgb10a2_unorm:
    case Format::rg16_float:
    case Format::rg16_snorm:
    case Format::r32_float:
    case Format::r32_uint:
    case Format::d32_float: return 4;
    case Format::r16_float: return 2;
    case Format::r8_unorm: return 1;
    case Format::rgba16_float:
    case Format::rg32_float: return 8;
    case Format::rgba32_float: return 16;
    }
    return 0;
  }

  // Upload memory is written by the CPU and read by the GPU in place;
  // device memory is the GPU's own and is filled by staged copies.
  enum class Memory : std::uint8_t { device, upload };

  using BufferUsage = std::uint8_t;
  // Read by shaders as a storage buffer, or as indices: always allowed.
  inline constexpr BufferUsage buffer_read = 0;
  // Written by shaders (read-write storage buffers).
  inline constexpr BufferUsage buffer_storage_write = 1;
  // Holds draw or dispatch arguments read by indirect commands.
  inline constexpr BufferUsage buffer_indirect = 2;

  struct BufferDesc {
    std::uint64_t size = 0;
    Memory memory = Memory::device;
    BufferUsage usage = buffer_read;
    const char* label = nullptr;
  };

  // Indirect argument records, laid out as both APIs read them.
  struct DrawIndirectArgs {
    std::uint32_t vertex_count, instance_count, first_vertex, first_instance;
  };
  struct DrawIndexedIndirectArgs {
    std::uint32_t index_count, instance_count, first_index;
    std::int32_t base_vertex;
    std::uint32_t first_instance;
  };
  struct DispatchIndirectArgs {
    std::uint32_t x, y, z;
  };

  using TextureUsage = std::uint8_t;
  inline constexpr TextureUsage usage_sampled = 1;
  inline constexpr TextureUsage usage_render_target = 2;
  inline constexpr TextureUsage usage_depth = 4;
  inline constexpr TextureUsage usage_storage = 8;

  struct TextureDesc {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    Format format = Format::rgba8_unorm;
    TextureUsage usage = usage_sampled;
    std::uint32_t samples = 1;
    const char* label = nullptr;
  };

  // Handles index the device's tables; the generation catches stale use.
  struct Handle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    explicit operator bool () const { return generation != 0; }
    bool operator== (const Handle&) const = default;
  };
  struct Buffer : Handle {};
  struct Texture : Handle {};
  struct Pipeline : Handle {};

  // One stage's code in each backend's form: MSL source, compiled when the
  // pipeline is made, DXIL compiled ahead of time by DXC, SPIR-V lowered
  // ahead of time by luv-shaderc, and WGSL source, compiled by the browser
  // when the pipeline is made.
  struct StageCode {
    std::string_view msl;
    std::span<const unsigned char> dxil;
    std::span<const std::uint32_t> spirv;
    std::string_view wgsl;
  };

  enum class CompareOp : std::uint8_t {
    never, less, equal, less_equal, greater, not_equal, greater_equal, always
  };
  // Alpha: source alpha over one minus it. Additive: source alpha onto
  // what is there, a glow that sums toward white. Premultiplied: the source
  // colour whole over one minus source alpha, so a surface can add light it
  // reflects while covering only what it fails to transmit.
  enum class Blend : std::uint8_t { none, alpha, additive, premultiplied };
  enum class Cull : std::uint8_t { none, back, front };
  enum class Topology : std::uint8_t { triangle_list, triangle_strip,
                                       line_list };

  struct RenderPipelineDesc {
    const Program* program = nullptr;
    StageCode vertex;
    StageCode fragment;
    std::array<Format, 8> color_formats {};
    std::array<Blend, 8> blend {};
    std::uint32_t color_count = 1;
    Format depth_format = Format::undefined;
    // Reversed-Z by default: nearer is greater.
    CompareOp depth_compare = CompareOp::greater_equal;
    bool depth_write = true;
    Cull cull = Cull::none;
    bool front_counter_clockwise = true;
    // Shadow casters push their depth away from the light: a constant in
    // units of the depth format's resolution, plus a multiple of the
    // primitive's depth slope.
    float depth_bias = 0.0f;
    float slope_scaled_depth_bias = 0.0f;
    std::uint32_t samples = 1;
    Topology topology = Topology::triangle_list;
    const char* label = nullptr;
  };

  struct ComputePipelineDesc {
    const Program* program = nullptr;
    StageCode compute;
    const char* label = nullptr;
  };

  enum class Load : std::uint8_t { load, clear, discard };
  enum class Store : std::uint8_t { store, discard };

  struct ColorAttachment {
    Texture texture;
    Load load = Load::clear;
    Store store = Store::store;
    std::array<float, 4> clear {0, 0, 0, 1};
    // Resolves a multisampled attachment into this single-sampled one.
    Texture resolve;
  };

  struct DepthAttachment {
    Texture texture;
    Load load = Load::clear;
    Store store = Store::discard;
    float clear = 0.0f; // reversed-Z: far is zero
  };

  struct RenderPassDesc {
    std::array<ColorAttachment, 8> colors {};
    std::uint32_t color_count = 0;
    DepthAttachment depth {};
    const char* label = nullptr;
  };

  enum class IndexType : std::uint8_t { uint16, uint32 };

  struct DeviceInfo {
    std::string backend;
    std::string adapter;
    std::uint32_t frames_in_flight = 0;
    // Whether the vertex and instance indices a shader sees count from a
    // draw's first vertex and first instance (Metal, Vulkan, WebGPU) or
    // from zero (Direct3D 12). Where they do, a renderer draws part of a
    // buffer by its first vertex; elsewhere, by binding the buffer from it.
    bool indices_count_from_first = true;
  };

  // A slice of the frame's upload arena: CPU-writable, GPU-readable until
  // the frame completes.
  struct Transient {
    void* data = nullptr;
    std::uint64_t gpu_address = 0;
    std::uint64_t size = 0;
  };

  // A completed frame's drawable, read back for screenshots and tests.
  struct Capture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Format format = Format::undefined;
    std::uint32_t row_bytes = 0;
    std::span<const std::byte> pixels;
  };

  // When a frame is expected on screen and how often the display refreshes,
  // in seconds on the host's steady clock (std::chrono::steady_clock).
  // `predicted` is false where the device cannot observe its presentations;
  // the host then keeps its own time.
  struct FrameTiming {
    double display_seconds = 0;
    double refresh_seconds = 0;
    bool predicted = false;
  };

  // GPU time spent in one pass of a completed frame.
  struct PassTiming {
    std::string label;
    double milliseconds = 0;
  };

  class Device {
  public:
    virtual ~Device () = default;

    virtual DeviceInfo info () const = 0;

    // -- resources ----------------------------------------------------
    virtual Buffer create_buffer (const BufferDesc& desc,
                                  std::span<const std::byte> initial = {})
      = 0;
    // Upload buffers only: the CPU address, valid for the buffer's life.
    virtual void* contents (Buffer buffer) = 0;
    virtual Texture create_texture (const TextureDesc& desc) = 0;
    // Replaces the whole texture; rows are tightly packed unless
    // row_bytes says otherwise. Completes before the next frame's work.
    virtual void write_texture (Texture texture,
                                std::span<const std::byte> pixels,
                                std::uint32_t row_bytes = 0)
      = 0;
    virtual Pipeline create_render_pipeline (const RenderPipelineDesc& desc)
      = 0;
    virtual Pipeline create_compute_pipeline (const ComputePipelineDesc& desc)
      = 0;
    virtual void destroy (Buffer buffer) = 0;
    virtual void destroy (Texture texture) = 0;
    virtual void destroy (Pipeline pipeline) = 0;

    // -- the drawable -------------------------------------------------
    virtual Format surface_format () const = 0;
    virtual std::uint32_t surface_width () const = 0;
    virtual std::uint32_t surface_height () const = 0;
    virtual void resize_surface (std::uint32_t width, std::uint32_t height)
      = 0;

    // A host pacing frames by a display link offers the next frame's
    // drawable (a CAMetalDrawable on Metal); devices that acquire their own
    // ignore it.
    virtual void offer_drawable (void* drawable) { (void)drawable; }

    // -- a frame ------------------------------------------------------
    // Waits for the frame slot and the next drawable. False means there
    // is nothing to draw into this time (a minimized window, say).
    virtual bool begin_frame () = 0;
    // This frame's drawable, valid until end_frame.
    virtual Texture backbuffer () = 0;
    virtual Transient allocate (std::uint64_t size,
                                std::uint64_t alignment = 256)
      = 0;

    virtual void begin_render_pass (const RenderPassDesc& desc) = 0;
    virtual void end_render_pass () = 0;
    // Compute passes hold dispatches; they may not overlap render passes.
    virtual void begin_compute_pass (const char* label = nullptr) = 0;
    virtual void end_compute_pass () = 0;
    // Outside any pass: copies bytes from the frame arena into a buffer,
    // ordered before the work that follows.
    virtual void copy_to_buffer (Buffer target, std::uint64_t offset,
                                 const Transient& source)
      = 0;
    virtual void set_pipeline (Pipeline pipeline) = 0;
    // WebGPU binds buffers only from multiples of 256 bytes.
    virtual void set_buffer (std::uint32_t binding, Buffer buffer,
                             std::uint64_t offset = 0)
      = 0;
    virtual void set_buffer (std::uint32_t binding, const Transient& slice)
      = 0;
    virtual void set_texture (std::uint32_t binding, Texture texture) = 0;
    virtual void set_storage_texture (std::uint32_t binding, Texture texture)
      = 0;
    virtual void set_viewport (float x, float y, float width, float height)
      = 0;
    virtual void draw (std::uint32_t vertex_count,
                       std::uint32_t instance_count = 1,
                       std::uint32_t first_vertex = 0,
                       std::uint32_t first_instance = 0)
      = 0;
    virtual void draw_indexed (Buffer indices, IndexType type,
                               std::uint32_t index_count,
                               std::uint32_t instance_count = 1,
                               std::uint32_t first_index = 0,
                               std::int32_t base_vertex = 0,
                               std::uint32_t first_instance = 0)
      = 0;
    // Arguments come from a buffer made with buffer_indirect, often
    // written by an earlier compute pass.
    virtual void draw_indirect (Buffer arguments, std::uint64_t offset = 0)
      = 0;
    virtual void draw_indexed_indirect (Buffer indices, IndexType type,
                                        Buffer arguments,
                                        std::uint64_t offset = 0)
      = 0;
    virtual void dispatch (std::uint32_t groups_x, std::uint32_t groups_y = 1,
                           std::uint32_t groups_z = 1)
      = 0;
    virtual void dispatch_indirect (Buffer arguments,
                                    std::uint64_t offset = 0)
      = 0;

    // Reads this frame's drawable back once the frame completes. `done`
    // runs on the rendering thread, in a later begin_frame or wait_idle.
    virtual void capture_frame (std::function<void (const Capture&)> done)
      = 0;

    // The passes of the most recently completed frame, in encoding order,
    // by their labels; the GPU's timestamps around each pass.
    virtual std::span<const PassTiming> pass_timings () const = 0;

    // Before begin_frame: when the next frame is expected on screen, from
    // the presentations the device has observed (presentation.hh). A host
    // steps its simulation by the difference between consecutive
    // predictions.
    virtual FrameTiming next_frame_timing () const { return {}; }

    // Submits the frame and presents its drawable.
    virtual void end_frame () = 0;
    // Waits until the GPU has finished everything submitted.
    virtual void wait_idle () = 0;

    // Copies a value into the arena and binds it.
    template <typename T>
    void set_uniforms (std::uint32_t binding, const T& value) {
      Transient slice = allocate (sizeof (T));
      std::memcpy (slice.data, &value, sizeof (T));
      set_buffer (binding, slice);
    }

    template <typename T>
    Transient upload (std::span<const T> values) {
      Transient slice = allocate (values.size_bytes ());
      if (!values.empty ())
        std::memcpy (slice.data, values.data (), values.size_bytes ());
      return slice;
    }
  };
}

#endif
