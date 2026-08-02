#ifndef MOPPE_METAL4_FRAME_HH
#define MOPPE_METAL4_FRAME_HH

#import <Metal/Metal.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace moppe::render::metal4 {
  struct ArenaSlice {
    MTLGPUAddress address = 0;
    void* contents = nullptr;
    std::size_t size = 0;
  };

  // One arena belongs to one in-flight slot. Its primary allocation handles
  // normal frames; rare oversized frames spill into additional allocations
  // that remain resident until the slot's completion event permits reset.
  struct FrameArena {
    id<MTLDevice> device = nil;
    id<MTLResidencySet> residency = nil;
    id<MTLBuffer> buffer = nil;
    id<MTLBuffer> active = nil;
    std::vector<id<MTLBuffer>> spills;
    std::size_t used = 0;

    void reset () {
      for (id<MTLBuffer> spill : spills)
        [residency removeAllocation:spill];
      if (!spills.empty ())
        [residency commit];
      spills.clear ();
      active = buffer;
      used = 0;
    }

    ArenaSlice allocate (std::size_t size, std::size_t alignment = 256) {
      std::size_t offset = (used + alignment - 1) & ~(alignment - 1);
      if (!active)
        active = buffer;
      if (!active || offset + size > active.length) {
        if (!device || !residency)
          throw std::runtime_error ("Metal frame arena is not initialized");
        const std::size_t capacity =
          std::max<std::size_t> (buffer.length, size + alignment);
        active = [device newBufferWithLength:capacity
                                     options:MTLResourceStorageModeShared];
        active.label = @"Moppe frame arena spill";
        [residency addAllocation:active];
        [residency commit];
        spills.push_back (active);
        used = 0;
        offset = 0;
      }
      used = offset + size;
      return { active.gpuAddress + offset,
               static_cast<std::byte*> (active.contents) + offset,
               size };
    }

    template <typename T>
    MTLGPUAddress write (const T& value) {
      ArenaSlice slice = allocate (sizeof (T), alignof (T));
      std::memcpy (slice.contents, &value, sizeof (T));
      return slice.address;
    }

    template <typename T>
    MTLGPUAddress write (std::span<const T> values) {
      const std::size_t bytes = values.size_bytes ();
      ArenaSlice slice = allocate (bytes, alignof (T));
      if (bytes)
        std::memcpy (slice.contents, values.data (), bytes);
      return slice.address;
    }
  };

  struct ArgumentTables {
    id<MTL4ArgumentTable> vertex = nil;
    id<MTL4ArgumentTable> fragment = nil;
    id<MTL4ArgumentTable> object = nil;
    id<MTL4ArgumentTable> mesh = nil;
  };

  inline id<MTL4ArgumentTable> argument_table (ArgumentTables& tables,
                                               MTLRenderStages stage) {
    switch (stage) {
    case MTLRenderStageVertex:
      return tables.vertex;
    case MTLRenderStageFragment:
      return tables.fragment;
    case MTLRenderStageObject:
      return tables.object;
    case MTLRenderStageMesh:
      return tables.mesh;
    default:
      throw std::logic_error ("Metal argument-table stage must be singular");
    }
  }

  inline void bind_address (ArgumentTables& tables,
                            MTLRenderStages stage,
                            NSUInteger index,
                            MTLGPUAddress address) {
    [argument_table (tables, stage) setAddress:address atIndex:index];
  }

  inline void bind_texture (ArgumentTables& tables,
                            MTLRenderStages stage,
                            NSUInteger index,
                            id<MTLTexture> texture) {
    [argument_table (tables, stage)
      setTexture:(texture ? texture.gpuResourceID : MTLResourceID {})
         atIndex:index];
  }

  inline void bind_sampler (ArgumentTables& tables,
                            MTLRenderStages stage,
                            NSUInteger index,
                            id<MTLSamplerState> sampler) {
    [argument_table (tables, stage)
      setSamplerState:(sampler ? sampler.gpuResourceID : MTLResourceID {})
              atIndex:index];
  }

  inline void use_arguments (id<MTL4RenderCommandEncoder> encoder,
                             ArgumentTables& tables,
                             MTLRenderStages stages) {
    for (MTLRenderStages stage : { MTLRenderStageVertex,
                                   MTLRenderStageFragment,
                                   MTLRenderStageObject,
                                   MTLRenderStageMesh })
      if (stages & stage)
        [encoder setArgumentTable:argument_table (tables, stage)
                         atStages:stage];
  }

  inline void
  wait_for_render_or_blit_writes (id<MTL4RenderCommandEncoder> encoder) {
    [encoder barrierAfterQueueStages:MTLStageFragment | MTLStageBlit
                        beforeStages:MTLStageVertex | MTLStageFragment
                   visibilityOptions:MTL4VisibilityOptionDevice];
  }

  inline void wait_for_render_writes (id<MTL4ComputeCommandEncoder> encoder) {
    [encoder barrierAfterQueueStages:MTLStageVertex | MTLStageFragment
                        beforeStages:MTLStageBlit
                   visibilityOptions:MTL4VisibilityOptionDevice];
  }
}

#endif
