#ifndef MOPPE_TERRAIN_LOWEST_READY_HH
#define MOPPE_TERRAIN_LOWEST_READY_HH

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace moppe::terrain {
  // The cells that are ready, to be taken lowest index first: a bit for
  // each, and over every sixty-four bits one more saying whether any is
  // set, to the top. A heap gives the same order at many times the cost,
  // since a ready cell is only ever its index.
  class LowestReady {
  public:
    explicit LowestReady (std::size_t count) {
      std::size_t words = (count + 63) / 64;
      for (;;) {
        m_levels.emplace_back (std::max<std::size_t> (1, words), 0);
        if (words <= 1)
          break;
        words = (words + 63) / 64;
      }
    }

    bool empty () const {
      return m_levels.back ()[0] == 0;
    }

    void push (std::uint32_t cell) {
      std::size_t index = cell;
      for (std::vector<std::uint64_t>& level : m_levels) {
        std::uint64_t& word = level[index >> 6];
        const bool marked = word != 0;
        word |= std::uint64_t { 1 } << (index & 63);
        // A word that already held a bit is already marked above.
        if (marked)
          return;
        index >>= 6;
      }
    }

    std::uint32_t pop () {
      std::size_t index = 0;
      for (std::size_t level = m_levels.size (); level-- > 0;)
        index = (index << 6) | static_cast<std::size_t> (
                                 std::countr_zero (m_levels[level][index]));
      const std::uint32_t cell = static_cast<std::uint32_t> (index);
      for (std::vector<std::uint64_t>& level : m_levels) {
        std::uint64_t& word = level[index >> 6];
        word &= ~(std::uint64_t { 1 } << (index & 63));
        if (word != 0)
          break;
        index >>= 6;
      }
      return cell;
    }

  private:
    // The cells' own bits first, the single top word last.
    std::vector<std::vector<std::uint64_t>> m_levels;
  };
}

#endif
