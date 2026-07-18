#import <Metal/Metal.h>

#include <moppe/profile.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/metal/metal_stream_power_evolution.hh>
#include <moppe/terrain/metal/orogeny_shader_types.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace moppe::terrain::metal {
  namespace {
    constexpr std::array neighbour_offsets {
      std::array { 1, 0 },   std::array { 1, -1 }, std::array { 0, -1 },
      std::array { -1, -1 }, std::array { -1, 0 }, std::array { -1, 1 },
      std::array { 0, 1 },   std::array { 1, 1 }
    };

    struct FacetOffsets {
      int cardinal_x;
      int cardinal_y;
      int diagonal_x;
      int diagonal_y;
    };

    constexpr std::array facet_offsets {
      FacetOffsets { 1, 0, 1, -1 },   FacetOffsets { 0, -1, 1, -1 },
      FacetOffsets { 0, -1, -1, -1 }, FacetOffsets { -1, 0, -1, -1 },
      FacetOffsets { -1, 0, -1, 1 },  FacetOffsets { 0, 1, -1, 1 },
      FacetOffsets { 0, 1, 1, 1 },    FacetOffsets { 1, 0, 1, 1 }
    };

    NSString* ns_string (const std::string& value) {
      return [NSString stringWithUTF8String:value.c_str ()];
    }

    std::runtime_error metal_error (const char* action, NSError* error) {
      std::string message = action;
      message += ": ";
      message +=
        error ? error.localizedDescription.UTF8String : "unknown Metal error";
      return std::runtime_error (message);
    }

    float offset_distance (int columns, int rows, const TerrainGrid& grid) {
      return std::hypot (static_cast<float> (columns) * grid.spacing_x_m (),
                         static_cast<float> (rows) * grid.spacing_y_m ());
    }

    float normalized_angle (float radians) {
      constexpr float turn = 2.0f * std::numbers::pi_v<float>;
      radians = std::fmod (radians, turn);
      return radians < 0.0f ? radians + turn : radians;
    }

    MoppeOrogenyStencil make_stencil (const TerrainGrid& grid) {
      MoppeOrogenyStencil stencil {};
      for (std::size_t i = 0; i < neighbour_offsets.size (); ++i) {
        const int columns = neighbour_offsets[i][0];
        const int rows = neighbour_offsets[i][1];
        const float x = static_cast<float> (columns) * grid.spacing_x_m ();
        const float z = static_cast<float> (rows) * grid.spacing_y_m ();
        const float direction = normalized_angle (std::atan2 (z, x));
        stencil.neighbours[i] = { .columns = columns,
                                  .rows = rows,
                                  .distance_m = std::hypot (x, z),
                                  .direction = direction,
                                  .unit_x = std::cos (direction),
                                  .unit_z = std::sin (direction) };
      }
      for (std::size_t i = 0; i < facet_offsets.size (); ++i) {
        const FacetOffsets offsets = facet_offsets[i];
        const float d1 =
          offset_distance (offsets.cardinal_x, offsets.cardinal_y, grid);
        const float d2 =
          offset_distance (offsets.diagonal_x - offsets.cardinal_x,
                           offsets.diagonal_y - offsets.cardinal_y,
                           grid);
        stencil.facets[i] = {
          .cardinal_x = offsets.cardinal_x,
          .cardinal_y = offsets.cardinal_y,
          .diagonal_x = offsets.diagonal_x,
          .diagonal_y = offsets.diagonal_y,
          .d1_m = d1,
          .d2_m = d2,
          .extent = std::atan2 (d2, d1),
          .u1_x = offsets.cardinal_x * grid.spacing_x_m () / d1,
          .u1_z = offsets.cardinal_y * grid.spacing_y_m () / d1,
          .u2_x = (offsets.diagonal_x - offsets.cardinal_x) *
                  grid.spacing_x_m () / d2,
          .u2_z =
            (offsets.diagonal_y - offsets.cardinal_y) * grid.spacing_y_m () / d2
        };
      }
      return stencil;
    }
  }

  class MetalStreamPowerEvolutionBackend::Impl {
  public:
    explicit Impl (const std::string& library_path) {
      MOPPE_PROFILE_ZONE ("MetalStreamPowerEvolutionBackend::initialize");
      m_device = MTLCreateSystemDefaultDevice ();
      if (!m_device)
        throw std::runtime_error ("Metal has no default GPU device");

      NSError* error = nil;
      NSString* path = ns_string (library_path);
      if ([path.pathExtension isEqualToString:@"metal"]) {
        NSString* source =
          [NSString stringWithContentsOfFile:path
                                    encoding:NSUTF8StringEncoding
                                       error:&error];
        if (source)
          m_library = [m_device newLibraryWithSource:source
                                             options:nil
                                               error:&error];
      } else {
        m_library = [m_device newLibraryWithURL:[NSURL fileURLWithPath:path]
                                          error:&error];
      }
      if (!m_library)
        throw metal_error ("failed to load orogeny shader library", error);
      id<MTLFunction> function =
        [m_library newFunctionWithName:@"moppe_select_d_infinity_routes"];
      if (!function)
        throw std::runtime_error (
          "orogeny shader library has no D-infinity route kernel");
      m_pipeline = [m_device newComputePipelineStateWithFunction:function
                                                           error:&error];
      if (!m_pipeline)
        throw metal_error ("failed to create orogeny compute pipeline", error);
      m_queue = [m_device newCommandQueue];
      if (!m_queue)
        throw std::runtime_error ("failed to create Metal command queue");
    }

    void select (const TerrainGrid& grid,
                 std::span<const float> levels,
                 std::span<const ChannelTangent> previous_tangent,
                 ChannelPersistence persistence,
                 std::span<const std::uint8_t> ocean,
                 std::span<const WaterBodyId> water_body,
                 std::span<FractionalFlowRoute> routes,
                 std::span<DrainageDirection> directions,
                 std::span<slope_t> slopes) {
      MOPPE_PROFILE_ZONE ("metal_orogeny.select_d_infinity_routes");
      const std::size_t count = grid.unique_size ();
      if (levels.size () != count || ocean.size () != count ||
          water_body.size () != count || routes.size () != count ||
          directions.size () != count || slopes.size () != count ||
          (!previous_tangent.empty () && previous_tangent.size () != count))
        throw std::invalid_argument (
          "Metal D-infinity buffers do not match terrain");

      std::lock_guard<std::mutex> lock (m_mutex);
      ensure_buffer (m_levels, m_levels_capacity, count * sizeof (float));
      ensure_buffer (
        m_tangents, m_tangents_capacity, count * sizeof (MoppeOrogenyTangent));
      ensure_buffer (
        m_active, m_active_capacity, count * sizeof (std::uint8_t));
      ensure_buffer (
        m_routes, m_routes_capacity, count * sizeof (MoppeOrogenyRoute));
      ensure_buffer (
        m_parameters, m_parameters_capacity, sizeof (MoppeOrogenyParameters));
      ensure_buffer (
        m_stencil, m_stencil_capacity, sizeof (MoppeOrogenyStencil));

      auto* gpu_levels = static_cast<float*> (m_levels.contents);
      auto* gpu_tangent =
        static_cast<MoppeOrogenyTangent*> (m_tangents.contents);
      auto* active = static_cast<std::uint8_t*> (m_active.contents);
      for (std::size_t cell = 0; cell < count; ++cell) {
        // Match RoutingSurface's stored float rounding before the GPU sees
        // the elevation. Multiplying inside the shader can retain enough
        // precision to invent a slope across a CPU-flat edge.
        gpu_levels[cell] = levels[cell] * grid.height_scale_m ();
        const Vec3 tangent =
          previous_tangent.empty ()
            ? Vec3 ()
            : previous_tangent[cell].numerical_value_in (mp_units::one);
        gpu_tangent[cell] = { tangent[0], tangent[1], tangent[2], 0.0f };
        active[cell] = !ocean[cell] && water_body[cell] == LakeCensus::dry;
      }
      const MoppeOrogenyParameters parameters {
        .width = static_cast<std::uint32_t> (grid.unique_width ()),
        .height = static_cast<std::uint32_t> (grid.unique_height ()),
        .periodic = grid.topology == Topology::Torus,
        .has_previous_tangent = !previous_tangent.empty (),
        .height_scale_m = 1.0f,
        .persistence = persistence.numerical_value_in (mp_units::one)
      };
      const MoppeOrogenyStencil stencil = make_stencil (grid);
      std::memcpy (m_parameters.contents, &parameters, sizeof (parameters));
      std::memcpy (m_stencil.contents, &stencil, sizeof (stencil));

      id<MTLCommandBuffer> command = [m_queue commandBuffer];
      id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
      [encoder setComputePipelineState:m_pipeline];
      [encoder setBuffer:m_levels offset:0 atIndex:MOPPE_OROGENY_LEVEL_BUFFER];
      [encoder setBuffer:m_tangents
                  offset:0
                 atIndex:MOPPE_OROGENY_TANGENT_BUFFER];
      [encoder setBuffer:m_active offset:0 atIndex:MOPPE_OROGENY_ACTIVE_BUFFER];
      [encoder setBuffer:m_routes offset:0 atIndex:MOPPE_OROGENY_ROUTE_BUFFER];
      [encoder setBuffer:m_parameters
                  offset:0
                 atIndex:MOPPE_OROGENY_PARAMETER_BUFFER];
      [encoder setBuffer:m_stencil
                  offset:0
                 atIndex:MOPPE_OROGENY_STENCIL_BUFFER];
      const NSUInteger threads =
        std::min<NSUInteger> (256, m_pipeline.maxTotalThreadsPerThreadgroup);
      [encoder dispatchThreads:MTLSizeMake (count, 1, 1)
         threadsPerThreadgroup:MTLSizeMake (threads, 1, 1)];
      [encoder endEncoding];
      [command commit];
      [command waitUntilCompleted];
      if (command.status == MTLCommandBufferStatusError)
        throw metal_error ("Metal D-infinity dispatch failed", command.error);

      const auto* gpu_routes =
        static_cast<const MoppeOrogenyRoute*> (m_routes.contents);
      for (std::size_t cell = 0; cell < count; ++cell) {
        const MoppeOrogenyRoute& source = gpu_routes[cell];
        if (source.arc_count > 2 ||
            (source.arc_count > 0 && source.receiver0 >= count) ||
            (source.arc_count > 1 && source.receiver1 >= count))
          throw std::logic_error ("Metal produced an invalid D-infinity route");
        if (source.arc_count > 0 &&
            !(gpu_levels[source.receiver0] < gpu_levels[cell]))
          throw std::logic_error (
            "Metal produced a non-descending D-infinity receiver at cell " +
            std::to_string (cell) + " -> " + std::to_string (source.receiver0) +
            " (" + std::to_string (gpu_levels[cell]) + " -> " +
            std::to_string (gpu_levels[source.receiver0]) + ")");
        if (source.arc_count > 1 &&
            !(gpu_levels[source.receiver1] < gpu_levels[source.receiver0]))
          throw std::logic_error (
            "Metal produced a non-descending D-infinity facet");
        FractionalFlowRoute route;
        route.arc_count = static_cast<std::uint8_t> (source.arc_count);
        if (source.arc_count > 0)
          route.arcs[0] = { .receiver = CellIndex { source.receiver0 },
                            .fraction =
                              source.fraction0 * flow_fraction[mp_units::one] };
        if (source.arc_count > 1)
          route.arcs[1] = { .receiver = CellIndex { source.receiver1 },
                            .fraction =
                              source.fraction1 * flow_fraction[mp_units::one] };
        route.receiver_interpolation =
          source.interpolation * facet_coordinate[mp_units::one];
        route.run = source.run_m * mp_units::si::metre;
        routes[cell] = route;
        directions[cell] =
          source.direction * drainage_direction[mp_units::angular::radian];
        slopes[cell] = source.slope * terrain_slope[mp_units::one];
      }
    }

  private:
    void ensure_buffer (id<MTLBuffer> __strong& buffer,
                        std::size_t& capacity,
                        std::size_t required) {
      if (buffer && capacity >= required)
        return;
      buffer = [m_device newBufferWithLength:required
                                     options:MTLResourceStorageModeShared];
      if (!buffer)
        throw std::runtime_error ("failed to allocate Metal orogeny buffer");
      capacity = required;
    }

    id<MTLDevice> m_device = nil;
    id<MTLLibrary> m_library = nil;
    id<MTLComputePipelineState> m_pipeline = nil;
    id<MTLCommandQueue> m_queue = nil;
    id<MTLBuffer> m_levels = nil;
    id<MTLBuffer> m_tangents = nil;
    id<MTLBuffer> m_active = nil;
    id<MTLBuffer> m_routes = nil;
    id<MTLBuffer> m_parameters = nil;
    id<MTLBuffer> m_stencil = nil;
    std::size_t m_levels_capacity = 0;
    std::size_t m_tangents_capacity = 0;
    std::size_t m_active_capacity = 0;
    std::size_t m_routes_capacity = 0;
    std::size_t m_parameters_capacity = 0;
    std::size_t m_stencil_capacity = 0;
    std::mutex m_mutex;
  };

  MetalStreamPowerEvolutionBackend::MetalStreamPowerEvolutionBackend (
    const std::string& library_path)
      : m_impl (std::make_unique<Impl> (library_path)) {}

  MetalStreamPowerEvolutionBackend::~MetalStreamPowerEvolutionBackend () =
    default;
  MetalStreamPowerEvolutionBackend::MetalStreamPowerEvolutionBackend (
    MetalStreamPowerEvolutionBackend&&) noexcept = default;
  MetalStreamPowerEvolutionBackend&
  MetalStreamPowerEvolutionBackend::operator= (
    MetalStreamPowerEvolutionBackend&&) noexcept = default;

  FractionalDrainage MetalStreamPowerEvolutionBackend::route_fractional (
    const TerrainView& terrain,
    const FloodField& flood,
    const LakeCensus& census,
    std::span<const ChannelTangent> previous_tangent,
    ChannelPersistence persistence) const {
    return analyze_fractional_drainage (
      terrain, flood, census, previous_tangent, persistence, *this);
  }

  void MetalStreamPowerEvolutionBackend::select_dry_routes (
    const TerrainGrid& grid,
    std::span<const float> routing_surface_levels,
    std::span<const ChannelTangent> previous_tangent,
    ChannelPersistence persistence,
    std::span<const std::uint8_t> ocean,
    std::span<const WaterBodyId> water_body,
    std::span<FractionalFlowRoute> routes,
    std::span<DrainageDirection> directions,
    std::span<slope_t> slopes) const {
    m_impl->select (grid,
                    routing_surface_levels,
                    previous_tangent,
                    persistence,
                    ocean,
                    water_body,
                    routes,
                    directions,
                    slopes);
  }
}
