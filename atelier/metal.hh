#pragma once

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

// MetalExecution: the device, its Metal 4 command queue, and the
// pacing of frames.  It owns the residency set for the atelier's
// resources and a shared event that lets the CPU wait for the GPU,
// keeping at most `frames_in_flight` frames in flight.

namespace atelier {
  inline constexpr std::size_t frames_in_flight = 3;

  inline std::runtime_error metal_error (const char* operation,
                                         NS::Error* error) {
    std::string message = operation;
    if (error && error->localizedDescription ()) {
      message += ": ";
      message += error->localizedDescription ()->utf8String ();
    }
    return std::runtime_error (message);
  }

  // One frame's turn on the GPU: its place in the ring of per-frame
  // buffers, and its sequence number for completion tracking.
  struct FrameSlot {
    uint64_t sequence;
    std::size_t buffer_index;
  };

  class MetalExecution {
  public:
    MetalExecution () {
      m_device = NS::TransferPtr (MTL::CreateSystemDefaultDevice ());
      if (!m_device)
        throw std::runtime_error ("Metal is unavailable");

      m_queue = NS::TransferPtr (m_device->newMTL4CommandQueue ());
      if (!m_queue)
        throw std::runtime_error ("Metal 4 is unavailable");
      m_command_buffer = NS::TransferPtr (m_device->newCommandBuffer ());
      for (auto& allocator : m_command_allocators)
        allocator = NS::TransferPtr (m_device->newCommandAllocator ());

      m_completion_event = NS::TransferPtr (m_device->newSharedEvent ());
      m_completion_event->setSignaledValue (0);

      NS::Error* error = nullptr;
      auto descriptor =
        NS::TransferPtr (MTL::ResidencySetDescriptor::alloc ()->init ());
      descriptor->setInitialCapacity (1 + frames_in_flight + 2);
      m_residency_set =
        NS::TransferPtr (m_device->newResidencySet (descriptor.get (), &error));
      if (!m_residency_set)
        throw metal_error ("Could not create Metal 4 residency set", error);
      m_queue->addResidencySet (m_residency_set.get ());
    }

    MTL::Device* device () const {
      return m_device.get ();
    }

    MTL4::CommandBuffer* command_buffer () const {
      return m_command_buffer.get ();
    }

    void use_residency_set (const MTL::ResidencySet* residency_set) {
      m_queue->addResidencySet (residency_set);
    }

    void make_resident (const MTL::Allocation* allocation) {
      m_residency_set->addAllocation (allocation);
    }

    void remove_resident (const MTL::Allocation* allocation) {
      m_residency_set->removeAllocation (allocation);
    }

    void commit_residency () {
      m_residency_set->commit ();
    }

    FrameSlot begin_frame () {
      const uint64_t sequence = ++m_frame_sequence;
      if (sequence > frames_in_flight)
        wait_until_complete (sequence - frames_in_flight);

      const std::size_t buffer_index = sequence % frames_in_flight;
      MTL4::CommandAllocator* allocator =
        m_command_allocators[buffer_index].get ();
      allocator->reset ();
      m_command_buffer->beginCommandBuffer (allocator);
      return { sequence, buffer_index };
    }

    void submit (CA::MetalDrawable* drawable, FrameSlot frame) {
      m_command_buffer->endCommandBuffer ();
      const MTL4::CommandBuffer* submitted = m_command_buffer.get ();
      m_queue->wait (drawable);
      m_queue->commit (&submitted, 1);
      m_queue->signalEvent (m_completion_event.get (), frame.sequence);
      m_queue->signalDrawable (drawable);
      drawable->present ();
    }

    void submit_offscreen (FrameSlot frame) {
      m_command_buffer->endCommandBuffer ();
      const MTL4::CommandBuffer* submitted = m_command_buffer.get ();
      m_queue->commit (&submitted, 1);
      m_queue->signalEvent (m_completion_event.get (), frame.sequence);
    }

    void wait_until_idle () {
      wait_until_complete (m_frame_sequence);
    }

  private:
    void wait_until_complete (uint64_t sequence) {
      if (sequence != 0 &&
          !m_completion_event->waitUntilSignaledValue (sequence, 1000)) {
        throw std::runtime_error ("Timed out waiting for a Metal 4 frame");
      }
    }

    NS::SharedPtr<MTL::Device> m_device;
    NS::SharedPtr<MTL4::CommandQueue> m_queue;
    NS::SharedPtr<MTL4::CommandBuffer> m_command_buffer;
    std::array<NS::SharedPtr<MTL4::CommandAllocator>, frames_in_flight>
      m_command_allocators;
    NS::SharedPtr<MTL::SharedEvent> m_completion_event;
    NS::SharedPtr<MTL::ResidencySet> m_residency_set;
    uint64_t m_frame_sequence = 0;
  };
}
