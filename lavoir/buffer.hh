#pragma once

#include "lavoir/units.hh"

#include "third_party/nanoarrow/nanoarrow.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <span>
#include <type_traits>

/// Storage for the workshop, held as Arrow buffers so that a column
/// can travel unchanged between memory, an IPC stream on disk, and the
/// GPU. Every allocation is page-aligned and rounded to whole pages,
/// which is exactly what Metal's no-copy buffer wrapping requires; the
/// same bytes can later be lent upward to a texture or downward to a
/// file without ever being copied. Ownership lives here and nowhere
/// else: everything downstream borrows.

namespace lavoir {
  /// A move-only owner of one page-aligned Arrow buffer.
  class buffer {
  public:
    buffer () {
      ArrowBufferInit (&m_arrow);
    }

    /// Allocate zeroed, page-aligned storage covering `size`, rounded
    /// up to whole pages so the buffer stays wrappable by the GPU.
    static buffer with_size (bytes_t size) {
      const bytes_t rounded = pages_covering (size).in (iec::byte);
      void* memory = nullptr;
      if (rounded != bytes_t::zero () &&
          posix_memalign (&memory,
                          (1 * gpu_page).numerical_value_in (iec::byte),
                          rounded.numerical_value_in (iec::byte)) != 0)
        throw std::bad_alloc ();

      buffer result;
      ArrowBufferSetAllocator (
        &result.m_arrow,
        ArrowBufferDeallocator ([] (ArrowBufferAllocator*,
                                    uint8_t* data,
                                    int64_t) { std::free (data); },
                                nullptr));
      result.m_arrow.data = static_cast<uint8_t*> (memory);
      result.m_arrow.size_bytes =
        static_cast<int64_t> (size.numerical_value_in (iec::byte));
      result.m_arrow.capacity_bytes =
        static_cast<int64_t> (rounded.numerical_value_in (iec::byte));
      if (memory != nullptr)
        std::memset (memory, 0, rounded.numerical_value_in (iec::byte));
      return result;
    }

    buffer (buffer&& other) noexcept {
      ArrowBufferMove (&other.m_arrow, &m_arrow);
    }

    buffer& operator= (buffer&& other) noexcept {
      if (this != &other) {
        ArrowBufferReset (&m_arrow);
        ArrowBufferMove (&other.m_arrow, &m_arrow);
      }
      return *this;
    }

    buffer (const buffer&) = delete;
    buffer& operator= (const buffer&) = delete;

    ~buffer () {
      ArrowBufferReset (&m_arrow);
    }

    bytes_t size () const {
      return static_cast<std::size_t> (m_arrow.size_bytes) * iec::byte;
    }

    bytes_t capacity () const {
      return static_cast<std::size_t> (m_arrow.capacity_bytes) * iec::byte;
    }

    /// The whole allocation as borrowable bytes. The span is a lease:
    /// it must not outlive the buffer.
    std::span<std::byte> lease () {
      return { reinterpret_cast<std::byte*> (m_arrow.data),
               size ().numerical_value_in (iec::byte) };
    }

    std::span<const std::byte> lease () const {
      return { reinterpret_cast<const std::byte*> (m_arrow.data),
               size ().numerical_value_in (iec::byte) };
    }

    /// The underlying Arrow buffer, for storage and interchange code
    /// that speaks nanoarrow directly.
    ArrowBuffer* arrow () {
      return &m_arrow;
    }

  private:
    ArrowBuffer m_arrow;
  };

  /// A move-only owner of `count` values of one trivially copyable
  /// type, stored in a page-aligned Arrow buffer. The typed views it
  /// hands out are leases; the column is the landlord.
  template <typename T>
    requires std::is_trivially_copyable_v<T>
  class column {
  public:
    using value_type = T;

    /// The exchange rate between this column's two denominations:
    /// bytes of storage per unit held.
    static constexpr auto bytes_per_value = sizeof (T) * iec::byte / unit;

    column () = default;

    static column with_count (values_t count) {
      column result;
      result.m_storage = buffer::with_size (count * bytes_per_value);
      result.m_count = count;
      return result;
    }

    values_t count () const {
      return m_count;
    }

    std::span<T> lease () {
      return { reinterpret_cast<T*> (m_storage.lease ().data ()),
               m_count.numerical_value_in (unit) };
    }

    std::span<const T> lease () const {
      return { reinterpret_cast<const T*> (m_storage.lease ().data ()),
               m_count.numerical_value_in (unit) };
    }

    buffer& storage () {
      return m_storage;
    }

  private:
    buffer m_storage;
    values_t m_count = values_t::zero ();
  };
}
