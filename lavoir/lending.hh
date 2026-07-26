#pragma once

#include "lavoir/buffer.hh"
#include "lavoir/metal.hh"

/// The second book: a tenancy records the loan of a buffer's pages to
/// the GPU. Metal wraps the same physical memory without copying --
/// the covenant that every buffer is page-aligned and rounded to
/// whole pages is exactly what qualifies it -- so the CPU keeps title
/// while the GPU holds tenancy, and the lease's term is the tenancy
/// object's lifetime. No deallocator is granted: the landlord is the
/// buffer, and it must outlive its tenant.

namespace lavoir {
  class tenancy {
  public:
    tenancy (gpu& metal, buffer& estate) : m_metal (&metal) {
      m_mapped = NS::TransferPtr (metal.device ()->newBuffer (
        estate.lease ().data (),
        estate.capacity ().numerical_value_in (iec::byte),
        MTL::ResourceStorageModeShared,
        nullptr));
      if (!m_mapped)
        throw std::runtime_error ("Metal declined the tenancy");
      metal.make_resident (m_mapped.get ());
    }

    tenancy (tenancy&&) = default;
    tenancy& operator= (tenancy&&) = default;
    tenancy (const tenancy&) = delete;
    tenancy& operator= (const tenancy&) = delete;

    ~tenancy () {
      if (m_mapped && m_metal)
        m_metal->remove_resident (m_mapped.get ());
    }

    /// Where the tenancy stands in the GPU's address space.
    std::uint64_t gpu_address () const {
      return m_mapped->gpuAddress ();
    }

    MTL::Buffer* metal_buffer () const {
      return m_mapped.get ();
    }

  private:
    gpu* m_metal = nullptr;
    NS::SharedPtr<MTL::Buffer> m_mapped;
  };
}
