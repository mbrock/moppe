// Backend plumbing shared by the NHAL devices: generational handle tables
// and the retirement queue that keeps destroyed objects alive until the GPU
// has finished every frame that might still use them.
#ifndef MOPPE_NHAL_TABLE_HH
#define MOPPE_NHAL_TABLE_HH

#include <moppe/nhal/nhal.hh>

#include <cstdint>
#include <deque>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::nhal {
  template <typename Value>
  class Table {
  public:
    template <typename H>
    H insert (Value value) {
      std::uint32_t index;
      if (!m_free.empty ()) {
        index = m_free.back ();
        m_free.pop_back ();
      } else {
        index = static_cast<std::uint32_t> (m_rows.size ());
        m_rows.emplace_back ();
      }
      Row& row = m_rows[index];
      row.value = std::move (value);
      row.live = true;
      H handle;
      handle.index = index;
      handle.generation = row.generation;
      return handle;
    }

    Value& operator[] (Handle handle) {
      if (handle.index >= m_rows.size ()
          || !m_rows[handle.index].live
          || m_rows[handle.index].generation != handle.generation)
        throw std::invalid_argument ("NHAL: stale or invalid handle");
      return m_rows[handle.index].value;
    }

    Value take (Handle handle) {
      Value value = std::move ((*this)[handle]);
      Row& row = m_rows[handle.index];
      row.value = Value {};
      row.live = false;
      ++row.generation;
      if (row.generation == 0)
        row.generation = 1;
      m_free.push_back (handle.index);
      return value;
    }

  private:
    struct Row {
      Value value {};
      std::uint32_t generation = 1;
      bool live = false;
    };
    std::vector<Row> m_rows;
    std::vector<std::uint32_t> m_free;
  };

  // Values retired at frame `serial` are released once the GPU has
  // completed that frame.
  template <typename Value>
  class Retirement {
  public:
    void retire (std::uint64_t serial, Value value) {
      m_pending.emplace_back (serial, std::move (value));
    }

    template <typename Release>
    void collect (std::uint64_t completed, Release&& release) {
      while (!m_pending.empty () && m_pending.front ().first <= completed) {
        release (m_pending.front ().second);
        m_pending.pop_front ();
      }
    }

  private:
    std::deque<std::pair<std::uint64_t, Value>> m_pending;
  };

  // Bump allocation inside one frame slot's upload memory.
  struct Arena {
    std::byte* data = nullptr;
    std::uint64_t gpu_address = 0;
    std::uint64_t capacity = 0;
    std::uint64_t used = 0;

    bool fits (std::uint64_t size, std::uint64_t alignment) const {
      const std::uint64_t at = (used + alignment - 1) & ~(alignment - 1);
      return at + size <= capacity;
    }

    Transient allocate (std::uint64_t size, std::uint64_t alignment) {
      const std::uint64_t at = (used + alignment - 1) & ~(alignment - 1);
      used = at + size;
      return { data + at, gpu_address + at, size };
    }
  };
}

#endif
