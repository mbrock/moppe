#pragma once

#include <moppe/terrain/watercourse.hh>

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  // Dry sites reconstruct from the retained physics heightfield. A signed
  // bank height, waves or any component of current makes a site explicit.
  // Each 64-bit occupancy word indexes one compact run by population count.
  class RuntimeWater {
  public:
    RuntimeWater (const WaterSheets& sheets,
                  std::span<const SurfaceElevation> ground)
        : m_domain (sheets.domain ()), m_ground (ground),
          m_blocks ((ground.size () + 63) / 64) {
      if (ground.size () != sheets.size ())
        throw std::invalid_argument ("water and ground extents differ");
      const auto& levels = spatial::get<surface_elevation> (sheets);
      const auto& waves = spatial::get<wave_amplitude> (sheets);
      const auto& currents = spatial::get<water_velocity> (sheets);
      for (size_t i = 0; i < ground.size (); ++i) {
        auto& block = m_blocks[i / 64];
        if (i % 64 == 0)
          block.first = static_cast<uint32_t> (m_sites.size ());
        auto flow = currents[i].numerical_value_in (u::m / u::s);
        if (levels[i] == ground[i] && waves[i] == WaveAmplitude::zero () &&
            flow[0] == 0 && flow[1] == 0 && flow[2] == 0)
          continue;
        block.mask |= uint64_t (1) << (i % 64);
        m_sites.push_back (
          { levels[i], waves[i], { flow[0], flow[1], flow[2] } });
      }
      m_sites.shrink_to_fit ();
    }

    template <auto Field>
    auto sample (const position_t& at) const {
      return spatial::sample_field (m_domain, at, [&] (TerrainIndex index) {
        const size_t i = m_domain.offset (index);
        const Block& block = m_blocks[i / 64];
        const uint64_t bit = uint64_t (1) << (i % 64);
        const Site* site =
          (block.mask & bit)
            ? &m_sites[block.first + std::popcount (block.mask & (bit - 1))]
            : nullptr;
        if constexpr (Field == surface_elevation)
          return site ? site->level : m_ground[i];
        else if constexpr (Field == wave_amplitude)
          return site ? site->wave : WaveAmplitude::zero ();
        else {
          static_assert (Field == water_velocity);
          return site ? Vec3 (site->flow[0], site->flow[1], site->flow[2]) *
                          water_velocity[u::m / u::s]
                      : WaterVelocity::zero ();
        }
      });
    }

  private:
    struct Block {
      uint64_t mask {};
      uint32_t first {};
    };
    struct Site {
      SurfaceElevation level;
      WaveAmplitude wave;
      std::array<float, 3> flow;
    };
    TerrainDomain m_domain;
    std::span<const SurfaceElevation> m_ground;
    std::vector<Block> m_blocks;
    std::vector<Site> m_sites;
  };

  // A borrowed sampling view used by the boat, walker and camera, both
  // during placement on dense fields and after their compact handoff.
  class WaterView {
  public:
    WaterView (const WaterSheets* dense = nullptr) : m_dense (dense) {}
    WaterView (const RuntimeWater& compact) : m_compact (&compact) {}
    explicit operator bool () const {
      return m_dense || m_compact;
    }
    template <auto Field>
    auto sample (const position_t& at) const {
      return m_compact ? m_compact->sample<Field> (at)
                       : spatial::sample<Field> (*m_dense, at);
    }

  private:
    const WaterSheets* m_dense {};
    const RuntimeWater* m_compact {};
  };
}
