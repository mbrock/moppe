#pragma once

#include "lavoir/units.hh"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

/// The device, its Metal 4 command queue, and the pacing of frames.
/// One residency set holds the workshop's resources, and a shared
/// event lets the CPU wait on the GPU, keeping at most
/// `frames_in_flight` frames in flight.

namespace lavoir {
  /// The pacing budget: how many frames may be in flight at once.
  /// A count of frames, and the ring of per-frame command allocators
  /// is exactly this many slots long.
  inline constexpr frames_t frames_in_flight = frames_t (3 * frame);

  inline constexpr std::size_t allocator_ring_size =
    frames_in_flight.numerical_value_in (frame);

  inline std::runtime_error metal_error (const char* operation,
                                         NS::Error* error) {
    std::string message = operation;
    if (error && error->localizedDescription ()) {
      message += ": ";
      message += error->localizedDescription ()->utf8String ();
    }
    return std::runtime_error (message);
  }

  /// One frame's turn on the GPU: its sequence number along the
  /// frame axis, and its place in the allocator ring -- the sequence
  /// taken modulo the pacing budget, still a count of frames.
  struct frame_slot {
    frames_t sequence;
    frames_t buffer_index;
  };

  class gpu {
  public:
    gpu () {
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
      descriptor->setInitialCapacity (1 + allocator_ring_size + 2);
      m_residency_set =
        NS::TransferPtr (m_device->newResidencySet (descriptor.get (), &error));
      if (!m_residency_set)
        throw metal_error ("Could not create a Metal 4 residency set", error);
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

    frame_slot begin_frame () {
      m_frame_sequence += frames_t (1 * frame);
      const frames_t sequence = m_frame_sequence;
      if (sequence > frames_in_flight)
        wait_until_complete (sequence - frames_in_flight);

      const frames_t buffer_index = sequence % frames_in_flight;
      MTL4::CommandAllocator* allocator =
        m_command_allocators[buffer_index.numerical_value_in (frame)].get ();
      allocator->reset ();
      m_command_buffer->beginCommandBuffer (allocator);
      return { sequence, buffer_index };
    }

    void submit (CA::MetalDrawable* drawable, frame_slot frame) {
      m_command_buffer->endCommandBuffer ();
      const MTL4::CommandBuffer* submitted = m_command_buffer.get ();
      m_queue->wait (drawable);
      m_queue->commit (&submitted, 1);
      m_queue->signalEvent (m_completion_event.get (),
                            frame.sequence.numerical_value_in (lavoir::frame));
      m_queue->signalDrawable (drawable);
      drawable->present ();
    }

    void submit_offscreen (frame_slot frame) {
      m_command_buffer->endCommandBuffer ();
      const MTL4::CommandBuffer* submitted = m_command_buffer.get ();
      m_queue->commit (&submitted, 1);
      m_queue->signalEvent (m_completion_event.get (),
                            frame.sequence.numerical_value_in (lavoir::frame));
    }

    void wait_until_idle () {
      wait_until_complete (m_frame_sequence);
    }

  private:
    void wait_until_complete (frames_t sequence) {
      if (sequence != frames_t::zero () &&
          !m_completion_event->waitUntilSignaledValue (
            sequence.numerical_value_in (frame), 1000))
        throw std::runtime_error ("Timed out waiting for a Metal 4 frame");
    }

    NS::SharedPtr<MTL::Device> m_device;
    NS::SharedPtr<MTL4::CommandQueue> m_queue;
    NS::SharedPtr<MTL4::CommandBuffer> m_command_buffer;
    std::array<NS::SharedPtr<MTL4::CommandAllocator>, allocator_ring_size>
      m_command_allocators;
    NS::SharedPtr<MTL::SharedEvent> m_completion_event;
    NS::SharedPtr<MTL::ResidencySet> m_residency_set;
    frames_t m_frame_sequence = frames_t::zero ();
  };
}
