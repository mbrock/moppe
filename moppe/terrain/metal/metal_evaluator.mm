#import <Metal/Metal.h>

#include <moppe/terrain/metal/field_shader_types.h>
#include <moppe/terrain/metal/metal_evaluator.hh>
#include <moppe/terrain/noise.hh>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace moppe::terrain::metal {
  namespace {
    constexpr std::size_t no_node = std::numeric_limits<std::size_t>::max ();

    enum class NodeKind {
      Parameter,
      X,
      Y,
      Add,
      Subtract,
      Multiply,
      MultiplyAdd,
      Sine,
      Smoothstep,
      Perlin,
      Fbm,
      Ridged,
      PeriodicFbm,
      PeriodicRidged
    };

    struct PreparedNode {
      NodeKind kind;
      std::size_t first = no_node;
      std::size_t second = no_node;
      std::size_t third = no_node;
      std::uint32_t slot0 = 0;
      std::uint32_t slot1 = 0;
    };

    struct PreparedField {
      std::vector<PreparedNode> nodes;
      std::vector<float> parameters;
      std::vector<MoppeFieldNoise> noises;
      std::vector<std::int32_t> permutations;
      std::string structure_key;
    };

    void check_domain (const RecipeDomain2D& domain) {
      if (domain.width < 2 || domain.height < 2)
        throw std::invalid_argument (
          "a field domain needs at least two samples per axis");
      if (!(domain.max_x > domain.min_x) || !(domain.max_y > domain.min_y))
        throw std::invalid_argument (
          "a field domain needs increasing coordinate bounds");
      if (domain.width > std::numeric_limits<std::uint32_t>::max () ||
          domain.height > std::numeric_limits<std::uint32_t>::max () ||
          domain.width >
            std::numeric_limits<std::size_t>::max () / domain.height)
        throw std::length_error ("field domain is too large");
    }

    PreparedField prepare (const ScalarField& field) {
      PreparedField result;
      result.nodes.reserve (unique_node_count (field));
      std::unordered_map<const expression::Node*, std::size_t> emitted;
      std::unordered_map<std::uint32_t, std::uint32_t> table_offsets;

      const auto parameter = [&] (float value) {
        if (result.parameters.size () >=
            std::numeric_limits<std::uint32_t>::max ())
          throw std::length_error ("too many scalar field parameters");
        const auto slot =
          static_cast<std::uint32_t> (result.parameters.size ());
        result.parameters.push_back (value);
        return slot;
      };

      const auto table_for = [&] (Seed seed) {
        if (const auto found = table_offsets.find (seed.value);
            found != table_offsets.end ())
          return found->second;
        if (result.permutations.size () >
            std::numeric_limits<std::uint32_t>::max () - 512)
          throw std::length_error ("too many scalar field noise tables");
        const auto offset =
          static_cast<std::uint32_t> (result.permutations.size ());
        const PerlinPermutation permutation =
          make_perlin_permutation (seed.value);
        result.permutations.insert (
          result.permutations.end (), permutation.begin (), permutation.end ());
        table_offsets.emplace (seed.value, offset);
        return offset;
      };

      const auto noise = [&] (Seed seed,
                              int period_x,
                              int period_y,
                              int octaves,
                              float lacunarity,
                              float gain) {
        if (result.noises.size () >= std::numeric_limits<std::uint32_t>::max ())
          throw std::length_error ("too many scalar field noise operations");
        const auto slot = static_cast<std::uint32_t> (result.noises.size ());
        result.noises.push_back ({ .permutation_offset = table_for (seed),
                                   .period_x = period_x,
                                   .period_y = period_y,
                                   .octaves = octaves,
                                   .lacunarity = lacunarity,
                                   .gain = gain });
        return slot;
      };

      std::function<std::size_t (const expression::NodePtr&)> emit;
      emit = [&] (const expression::NodePtr& node) -> std::size_t {
        if (const auto found = emitted.find (node.get ());
            found != emitted.end ())
          return found->second;

        PreparedNode prepared = std::visit (
          [&] (const auto& operation) -> PreparedNode {
            using T = std::decay_t<decltype (operation)>;
            if constexpr (std::is_same_v<T, expression::Constant>)
              return { .kind = NodeKind::Parameter,
                       .slot0 = parameter (operation.value) };
            else if constexpr (std::is_same_v<T, expression::CoordinateX>)
              return { .kind = NodeKind::X };
            else if constexpr (std::is_same_v<T, expression::CoordinateY>)
              return { .kind = NodeKind::Y };
            else if constexpr (std::is_same_v<T, expression::Add>)
              return { .kind = NodeKind::Add,
                       .first = emit (operation.left),
                       .second = emit (operation.right) };
            else if constexpr (std::is_same_v<T, expression::Subtract>)
              return { .kind = NodeKind::Subtract,
                       .first = emit (operation.left),
                       .second = emit (operation.right) };
            else if constexpr (std::is_same_v<T, expression::Multiply>)
              return { .kind = NodeKind::Multiply,
                       .first = emit (operation.left),
                       .second = emit (operation.right) };
            else if constexpr (std::is_same_v<T, expression::MultiplyAdd>)
              return { .kind = NodeKind::MultiplyAdd,
                       .first = emit (operation.multiplier),
                       .second = emit (operation.multiplicand),
                       .third = emit (operation.addend) };
            else if constexpr (std::is_same_v<T, expression::Sine>)
              return { .kind = NodeKind::Sine,
                       .first = emit (operation.operand) };
            else if constexpr (std::is_same_v<T, expression::Smoothstep>)
              return { .kind = NodeKind::Smoothstep,
                       .first = emit (operation.operand),
                       .slot0 = parameter (operation.edge0),
                       .slot1 = parameter (operation.edge1) };
            else if constexpr (std::is_same_v<T, expression::PerlinNoise>)
              return { .kind = NodeKind::Perlin,
                       .first = emit (operation.x),
                       .second = emit (operation.y),
                       .slot0 =
                         noise (operation.seed, 256, 256, 1, 1.0f, 1.0f) };
            else if constexpr (std::is_same_v<T, expression::FbmNoise>)
              return { .kind = NodeKind::Fbm,
                       .first = emit (operation.x),
                       .second = emit (operation.y),
                       .slot0 = noise (operation.seed,
                                       256,
                                       256,
                                       operation.octaves,
                                       operation.lacunarity,
                                       operation.gain) };
            else if constexpr (std::is_same_v<T, expression::RidgedNoise>)
              return { .kind = NodeKind::Ridged,
                       .first = emit (operation.x),
                       .second = emit (operation.y),
                       .slot0 = noise (operation.seed,
                                       256,
                                       256,
                                       operation.octaves,
                                       operation.lacunarity,
                                       operation.gain) };
            else if constexpr (std::is_same_v<T, expression::PeriodicFbmNoise>)
              return { .kind = NodeKind::PeriodicFbm,
                       .first = emit (operation.x),
                       .second = emit (operation.y),
                       .slot0 =
                         noise (operation.seed,
                                operation.period_x,
                                operation.period_y,
                                operation.octaves,
                                static_cast<float> (operation.lacunarity),
                                operation.gain) };
            else
              return { .kind = NodeKind::PeriodicRidged,
                       .first = emit (operation.x),
                       .second = emit (operation.y),
                       .slot0 =
                         noise (operation.seed,
                                operation.period_x,
                                operation.period_y,
                                operation.octaves,
                                static_cast<float> (operation.lacunarity),
                                operation.gain) };
          },
          node->operation);

        const std::size_t index = result.nodes.size ();
        result.nodes.push_back (prepared);
        emitted.emplace (node.get (), index);
        return index;
      };

      (void)emit (field.node ());
      std::ostringstream key;
      for (const PreparedNode& node : result.nodes)
        key << static_cast<int> (node.kind) << ':' << node.first << ','
            << node.second << ',' << node.third << ';';
      result.structure_key = key.str ();
      return result;
    }

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
  }

  class MetalEvaluator::Impl {
  public:
    explicit Impl (const std::string& library_path) {
      if (@available (macOS 26.0, *)) {
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
          throw metal_error ("failed to load field shader library", error);

        MTL4CompilerDescriptor* descriptor = [MTL4CompilerDescriptor new];
        descriptor.label = @"Moppe scalar field compiler";
        m_compiler = [m_device newCompilerWithDescriptor:descriptor
                                                   error:&error];
        if (!m_compiler)
          throw metal_error ("failed to create Metal 4 compiler", error);
        m_queue = [m_device newCommandQueue];
        if (!m_queue)
          throw std::runtime_error ("failed to create Metal command queue");
        m_pipelines = [NSMutableDictionary dictionary];
      } else {
        throw std::runtime_error ("Metal 4 requires macOS 26 or later");
      }
    }

    ScalarRaster evaluate (const ScalarField& field,
                           const RecipeDomain2D& domain) {
      check_domain (domain);
      PreparedField prepared = prepare (field);
      std::scoped_lock lock (m_mutex);
      NSString* key = ns_string (prepared.structure_key);
      id<MTLComputePipelineState> pipeline = m_pipelines[key];
      if (!pipeline) {
        pipeline = compile (prepared);
        m_pipelines[key] = pipeline;
        m_pipeline_count.fetch_add (1, std::memory_order_relaxed);
      }

      const MoppeFieldDomain gpu_domain {
        .width = static_cast<std::uint32_t> (domain.width),
        .height = static_cast<std::uint32_t> (domain.height),
        .min_x = domain.min_x,
        .max_x = domain.max_x,
        .min_y = domain.min_y,
        .max_y = domain.max_y
      };
      const std::size_t sample_count = domain.width * domain.height;
      ensure_buffer (
        m_output, m_output_capacity, sample_count * sizeof (float));
      ensure_buffer (m_domain, m_domain_capacity, sizeof (gpu_domain));
      std::memcpy (m_domain.contents, &gpu_domain, sizeof (gpu_domain));
      const float zero_parameter = 0.0f;
      const std::size_t parameter_bytes =
        std::max<std::size_t> (1, prepared.parameters.size ()) * sizeof (float);
      ensure_buffer (m_parameters, m_parameter_capacity, parameter_bytes);
      std::memcpy (m_parameters.contents,
                   prepared.parameters.empty () ? &zero_parameter
                                                : prepared.parameters.data (),
                   parameter_bytes);
      const MoppeFieldNoise zero_noise {};
      const std::size_t noise_bytes =
        std::max<std::size_t> (1, prepared.noises.size ()) *
        sizeof (MoppeFieldNoise);
      ensure_buffer (m_noises, m_noise_capacity, noise_bytes);
      std::memcpy (m_noises.contents,
                   prepared.noises.empty () ? &zero_noise
                                            : prepared.noises.data (),
                   noise_bytes);
      const std::int32_t zero_permutation = 0;
      const std::size_t permutation_bytes =
        std::max<std::size_t> (1, prepared.permutations.size ()) *
        sizeof (std::int32_t);
      ensure_buffer (m_permutations, m_permutation_capacity, permutation_bytes);
      std::memcpy (m_permutations.contents,
                   prepared.permutations.empty ()
                     ? &zero_permutation
                     : prepared.permutations.data (),
                   permutation_bytes);

      id<MTLCommandBuffer> command = [m_queue commandBuffer];
      id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
      [encoder setComputePipelineState:pipeline];
      [encoder setBuffer:m_output offset:0 atIndex:MOPPE_FIELD_OUTPUT_BUFFER];
      [encoder setBuffer:m_domain offset:0 atIndex:MOPPE_FIELD_DOMAIN_BUFFER];
      [encoder setBuffer:m_parameters
                  offset:0
                 atIndex:MOPPE_FIELD_PARAMETER_BUFFER];
      [encoder setBuffer:m_noises offset:0 atIndex:MOPPE_FIELD_NOISE_BUFFER];
      [encoder setBuffer:m_permutations
                  offset:0
                 atIndex:MOPPE_FIELD_PERMUTATION_BUFFER];
      [encoder dispatchThreads:MTLSizeMake (domain.width, domain.height, 1)
         threadsPerThreadgroup:MTLSizeMake (16, 16, 1)];
      [encoder endEncoding];
      [command commit];
      [command waitUntilCompleted];
      if (command.status == MTLCommandBufferStatusError)
        throw metal_error ("Metal field dispatch failed", command.error);

      std::vector<float> values (sample_count);
      std::memcpy (
        values.data (), m_output.contents, values.size () * sizeof (float));
      return ScalarRaster (domain, std::move (values));
    }

    std::size_t compiled_pipeline_count () const noexcept {
      return m_pipeline_count.load (std::memory_order_relaxed);
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
        throw std::runtime_error ("failed to allocate Metal field buffer");
      capacity = required;
    }

    id<MTLComputePipelineState> compile (const PreparedField& prepared) {
      NSMutableArray<MTL4FunctionDescriptor*>* function_descriptors =
        [NSMutableArray array];
      NSMutableArray<MTLFunctionStitchingFunctionNode*>* graph_nodes =
        [NSMutableArray array];
      std::unordered_set<std::string> ordinary_functions;

      const auto library_function = [&] (NSString* name) {
        MTL4LibraryFunctionDescriptor* descriptor =
          [MTL4LibraryFunctionDescriptor new];
        descriptor.library = m_library;
        descriptor.name = name;
        return descriptor;
      };
      const auto add_ordinary = [&] (const char* name) {
        if (ordinary_functions.insert (name).second)
          [function_descriptors
            addObject:library_function ([NSString stringWithUTF8String:name])];
        return [NSString stringWithUTF8String:name];
      };
      const auto add_specialized =
        [&] (const char* base_name, const char* prefix, std::uint32_t slot) {
          MTLFunctionConstantValues* values = [MTLFunctionConstantValues new];
          [values setConstantValue:&slot type:MTLDataTypeUInt atIndex:0];
          MTL4SpecializedFunctionDescriptor* descriptor =
            [MTL4SpecializedFunctionDescriptor new];
          descriptor.functionDescriptor =
            library_function ([NSString stringWithUTF8String:base_name]);
          descriptor.specializedName =
            [NSString stringWithFormat:@"%s_%u", prefix, slot];
          descriptor.constantValues = values;
          [function_descriptors addObject:descriptor];
          return descriptor.specializedName;
        };
      const auto call = [&] (NSString* name,
                             NSArray<id<MTLFunctionStitchingNode>>* arguments) {
        MTLFunctionStitchingFunctionNode* node =
          [[MTLFunctionStitchingFunctionNode alloc] initWithName:name
                                                       arguments:arguments
                                             controlDependencies:@[]];
        [graph_nodes addObject:node];
        return node;
      };

      MTLFunctionStitchingInputNode* position =
        [[MTLFunctionStitchingInputNode alloc] initWithArgumentIndex:0];
      MTLFunctionStitchingInputNode* parameters =
        [[MTLFunctionStitchingInputNode alloc] initWithArgumentIndex:1];
      MTLFunctionStitchingInputNode* noises =
        [[MTLFunctionStitchingInputNode alloc] initWithArgumentIndex:2];
      MTLFunctionStitchingInputNode* permutations =
        [[MTLFunctionStitchingInputNode alloc] initWithArgumentIndex:3];
      std::vector<MTLFunctionStitchingFunctionNode*> lowered;
      lowered.reserve (prepared.nodes.size ());

      const auto parameter_node = [&] (std::uint32_t slot) {
        return call (add_specialized (
                       "moppe_field_parameter", "moppe_field_parameter", slot),
                     @[ parameters ]);
      };
      const auto argument =
        [&] (const std::vector<MTLFunctionStitchingFunctionNode*>& nodes,
             std::size_t index) -> id<MTLFunctionStitchingNode> {
        return nodes.at (index);
      };

      for (const PreparedNode& node : prepared.nodes) {
        MTLFunctionStitchingFunctionNode* lowered_node = nil;
        switch (node.kind) {
        case NodeKind::Parameter:
          lowered_node = parameter_node (node.slot0);
          break;
        case NodeKind::X:
          lowered_node = call (add_ordinary ("moppe_field_x"), @[ position ]);
          break;
        case NodeKind::Y:
          lowered_node = call (add_ordinary ("moppe_field_y"), @[ position ]);
          break;
        case NodeKind::Add:
          lowered_node = call (add_ordinary ("moppe_field_add"), @[
            argument (lowered, node.first),
            argument (lowered, node.second)
          ]);
          break;
        case NodeKind::Subtract:
          lowered_node = call (add_ordinary ("moppe_field_subtract"), @[
            argument (lowered, node.first),
            argument (lowered, node.second)
          ]);
          break;
        case NodeKind::Multiply:
          lowered_node = call (add_ordinary ("moppe_field_multiply"), @[
            argument (lowered, node.first),
            argument (lowered, node.second)
          ]);
          break;
        case NodeKind::MultiplyAdd:
          lowered_node = call (add_ordinary ("moppe_field_multiply_add"), @[
            argument (lowered, node.first),
            argument (lowered, node.second),
            argument (lowered, node.third)
          ]);
          break;
        case NodeKind::Sine:
          lowered_node = call (add_ordinary ("moppe_field_sine"),
                               @[ argument (lowered, node.first) ]);
          break;
        case NodeKind::Smoothstep: {
          MTLFunctionStitchingFunctionNode* edge0 = parameter_node (node.slot0);
          MTLFunctionStitchingFunctionNode* edge1 = parameter_node (node.slot1);
          lowered_node =
            call (add_ordinary ("moppe_field_smoothstep"),
                  @[ edge0, edge1, argument (lowered, node.first) ]);
          break;
        }
        case NodeKind::Perlin:
        case NodeKind::Fbm:
        case NodeKind::Ridged:
        case NodeKind::PeriodicFbm:
        case NodeKind::PeriodicRidged: {
          const char* base = nullptr;
          const char* prefix = nullptr;
          switch (node.kind) {
          case NodeKind::Perlin:
            base = prefix = "moppe_field_perlin";
            break;
          case NodeKind::Fbm:
            base = prefix = "moppe_field_fbm";
            break;
          case NodeKind::Ridged:
            base = prefix = "moppe_field_ridged";
            break;
          case NodeKind::PeriodicFbm:
            base = prefix = "moppe_field_periodic_fbm";
            break;
          case NodeKind::PeriodicRidged:
            base = prefix = "moppe_field_periodic_ridged";
            break;
          default:
            break;
          }
          lowered_node = call (add_specialized (base, prefix, node.slot0), @[
            argument (lowered, node.first),
            argument (lowered, node.second),
            noises,
            permutations
          ]);
          break;
        }
        }
        lowered.push_back (lowered_node);
      }

      // Function stitching derives the produced signature from graph inputs
      // that are actually referenced.  A uniform finish node keeps all four
      // fixed ABI arguments present even for a constant or coordinate field;
      // it costs nothing after AlwaysInline compilation.
      MTLFunctionStitchingFunctionNode* output =
        call (add_ordinary ("moppe_field_finish"),
              @[ lowered.back (), position, parameters, noises, permutations ]);
      [graph_nodes removeObjectIdenticalTo:output];
      MTLFunctionStitchingAttributeAlwaysInline* always_inline =
        [MTLFunctionStitchingAttributeAlwaysInline new];
      MTLFunctionStitchingGraph* graph = [[MTLFunctionStitchingGraph alloc]
        initWithFunctionName:@"moppe_evaluate_field"
                       nodes:graph_nodes
                  outputNode:output
                  attributes:@[ always_inline ]];
      MTL4StitchedFunctionDescriptor* stitched =
        [MTL4StitchedFunctionDescriptor new];
      stitched.functionGraph = graph;
      stitched.functionDescriptors = function_descriptors;

      MTL4LibraryFunctionDescriptor* wrapper =
        library_function (@"moppe_materialize_field");
      MTL4StaticLinkingDescriptor* linking = [MTL4StaticLinkingDescriptor new];
      linking.privateFunctionDescriptors = @[ stitched ];
      MTL4ComputePipelineDescriptor* descriptor =
        [MTL4ComputePipelineDescriptor new];
      descriptor.label = @"Moppe stitched scalar field";
      descriptor.computeFunctionDescriptor = wrapper;
      descriptor.staticLinkingDescriptor = linking;
      descriptor.maxTotalThreadsPerThreadgroup = 256;

      NSError* error = nil;
      id<MTLComputePipelineState> pipeline =
        [m_compiler newComputePipelineStateWithDescriptor:descriptor
                                      compilerTaskOptions:nil
                                                    error:&error];
      if (!pipeline)
        throw metal_error ("failed to compile stitched scalar field", error);
      return pipeline;
    }

    id<MTLDevice> m_device = nil;
    id<MTLLibrary> m_library = nil;
    id<MTL4Compiler> m_compiler = nil;
    id<MTLCommandQueue> m_queue = nil;
    NSMutableDictionary<NSString*, id<MTLComputePipelineState>>* m_pipelines =
      nil;
    id<MTLBuffer> m_output = nil;
    id<MTLBuffer> m_domain = nil;
    id<MTLBuffer> m_parameters = nil;
    id<MTLBuffer> m_noises = nil;
    id<MTLBuffer> m_permutations = nil;
    std::size_t m_output_capacity = 0;
    std::size_t m_domain_capacity = 0;
    std::size_t m_parameter_capacity = 0;
    std::size_t m_noise_capacity = 0;
    std::size_t m_permutation_capacity = 0;
    std::mutex m_mutex;
    std::atomic<std::size_t> m_pipeline_count = 0;
  };

  MetalEvaluator::MetalEvaluator (const std::string& library_path)
      : m_impl (std::make_unique<Impl> (library_path)) {}

  MetalEvaluator::~MetalEvaluator () = default;
  MetalEvaluator::MetalEvaluator (MetalEvaluator&&) noexcept = default;
  MetalEvaluator&
  MetalEvaluator::operator= (MetalEvaluator&&) noexcept = default;

  ScalarRaster MetalEvaluator::evaluate (const ScalarField& field,
                                         const RecipeDomain2D& domain) const {
    return m_impl->evaluate (field, domain);
  }

  std::size_t MetalEvaluator::compiled_pipeline_count () const noexcept {
    return m_impl->compiled_pipeline_count ();
  }
}
