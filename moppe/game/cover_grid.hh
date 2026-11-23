#pragma once

#include <moppe/terrain/domain.hh>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace moppe::game {
  // One runtime ground-cover channel. Each 8x8 tile stores its minimum
  // and just the bits needed for the range of its normalized bytes.
  // An animal or a wheel can read one site without expanding the tile.
  class CoverGrid {
  public:
    template <class Column>
    void assign (const terrain::TerrainDomain& domain, const Column& column) {
      m_across = (domain.width () + 7) / 8;
      m_tiles.clear ();
      m_bytes.clear ();
      m_tiles.reserve (m_across * ((domain.height () + 7) / 8));
      for (size_t z = 0; z < domain.height (); z += 8)
        for (size_t x = 0; x < domain.width (); x += 8) {
          std::array<uint8_t, 64> values {};
          uint8_t low = 255, high = 0;
          for (size_t j = 0; j < 8 && z + j < domain.height (); ++j)
            for (size_t i = 0; i < 8 && x + i < domain.width (); ++i) {
              const auto value = column[(z + j) * domain.width () + x + i];
              const auto byte = static_cast<uint8_t> (std::lround (
                255.f * std::clamp (
                          value.numerical_value_in (mp_units::one), 0.f, 1.f)));
              values[8 * j + i] = byte;
              low = std::min (low, byte);
              high = std::max (high, byte);
            }
          const auto bits = uint8_t (std::bit_width (unsigned (high - low)));
          m_tiles.push_back (
            { static_cast<uint32_t> (m_bytes.size ()), low, bits });
          if (!bits)
            continue;
          for (size_t j = 0; j < 8; ++j) {
            uint64_t row = 0;
            for (size_t i = 0; i < 8; ++i) {
              const auto delta = std::max (values[8 * j + i], low) - low;
              row |= uint64_t (delta) << (i * bits);
            }
            for (unsigned b = 0; b < bits; ++b)
              m_bytes.push_back (uint8_t (row >> (8 * b)));
          }
        }
      // Two-byte reads may extend one byte past the last encoded row.
      m_bytes.push_back (0);
      m_bytes.shrink_to_fit ();
    }

    uint8_t at (terrain::TerrainIndex site) const {
      const auto& tile = m_tiles[(site.row / 8) * m_across + site.column / 8];
      if (!tile.bits)
        return tile.low;
      const unsigned bit = (site.column % 8) * tile.bits;
      const size_t offset = tile.first + (site.row % 8) * tile.bits + bit / 8;
      const unsigned pair =
        m_bytes[offset] | (unsigned (m_bytes[offset + 1]) << 8);
      return tile.low + ((pair >> (bit % 8)) & ((1u << tile.bits) - 1));
    }

    size_t storage_bytes () const {
      return m_bytes.capacity () + m_tiles.capacity () * sizeof (Tile);
    }

  private:
    struct Tile {
      uint32_t first;
      uint8_t low, bits;
    };
    size_t m_across {};
    std::vector<Tile> m_tiles;
    std::vector<uint8_t> m_bytes;
  };
}
