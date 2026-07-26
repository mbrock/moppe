#pragma once

#include "third_party/nanoarrow/nanoarrow.h"

#include <cstddef>
#include <cstdlib>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

/// Storage for the workshop, held as Arrow buffers so that a column
/// can travel unchanged between memory, an IPC stream on disk, and the
/// GPU. Every allocation is page-aligned and page-rounded, which is
/// exactly what Metal's no-copy buffer wrapping requires; the same
/// bytes can later be lent upward to a texture or downward to a file
/// without ever being copied. Ownership lives here and nowhere else:
/// everything downstream borrows.

namespace lavoir {
  /// The alignment and granularity Metal requires of memory it wraps
  /// without copying, and a comfortable alignment for SIMD besides.
  inline constexpr std::size_t page_size = 16384;

  inline constexpr std::size_t pages_covering (std::size_t bytes) {
    return (bytes + page_size - 1) / page_size * page_size;
  }

  /// A move-only owner of one page-aligned Arrow buffer.
  class buffer {
  public:
    buffer () {
      ArrowBufferInit (&m_arrow);
    }

    /// Allocate `bytes` of zeroed, page-aligned storage, rounded up to
    /// whole pages so the buffer stays wrappable by the GPU.
    static buffer with_size (std::size_t bytes) {
      const std::size_t rounded = pages_covering (bytes);
      void* memory = nullptr;
      if (rounded != 0 && posix_memalign (&memory, page_size, rounded) != 0)
        throw std::bad_alloc ();

      buffer result;
      ArrowBufferSetAllocator (
        &result.m_arrow,
        ArrowBufferDeallocator ([] (ArrowBufferAllocator*,
                                    uint8_t* data,
                                    int64_t) { std::free (data); },
                                nullptr));
      result.m_arrow.data = static_cast<uint8_t*> (memory);
      result.m_arrow.size_bytes = static_cast<int64_t> (bytes);
      result.m_arrow.capacity_bytes = static_cast<int64_t> (rounded);
      if (memory != nullptr)
        std::memset (memory, 0, rounded);
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

    std::size_t size () const {
      return static_cast<std::size_t> (m_arrow.size_bytes);
    }

    std::size_t capacity () const {
      return static_cast<std::size_t> (m_arrow.capacity_bytes);
    }

    /// The whole allocation as borrowable bytes. The span is a lease:
    /// it must not outlive the buffer.
    std::span<std::byte> lease () {
      return { reinterpret_cast<std::byte*> (m_arrow.data), size () };
    }

    std::span<const std::byte> lease () const {
      return { reinterpret_cast<const std::byte*> (m_arrow.data), size () };
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
    column () = default;

    static column with_count (std::size_t count) {
      column result;
      result.m_storage = buffer::with_size (count * sizeof (T));
      result.m_count = count;
      return result;
    }

    std::size_t count () const {
      return m_count;
    }

    std::span<T> lease () {
      return { reinterpret_cast<T*> (m_storage.lease ().data ()), m_count };
    }

    std::span<const T> lease () const {
      return { reinterpret_cast<const T*> (m_storage.lease ().data ()),
               m_count };
    }

    buffer& storage () {
      return m_storage;
    }

  private:
    buffer m_storage;
    std::size_t m_count = 0;
  };
}
