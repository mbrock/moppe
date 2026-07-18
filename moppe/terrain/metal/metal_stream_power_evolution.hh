#ifndef MOPPE_TERRAIN_METAL_STREAM_POWER_EVOLUTION_HH
#define MOPPE_TERRAIN_METAL_STREAM_POWER_EVOLUTION_HH

#include <moppe/terrain/stream_power_evolution.hh>

#include <memory>
#include <string>

namespace moppe::terrain::metal {
  // Hybrid evolution backend: Metal selects independent dry-cell D-infinity
  // routes, while the shared portable path retains lake fallback, fractional
  // accumulation, and the downstream implicit solve.
  class MetalStreamPowerEvolutionBackend final
      : public StreamPowerEvolutionBackend,
        private FractionalRouteBackend {
  public:
    explicit MetalStreamPowerEvolutionBackend (const std::string& library_path);
    ~MetalStreamPowerEvolutionBackend () override;

    MetalStreamPowerEvolutionBackend (
      MetalStreamPowerEvolutionBackend&&) noexcept;
    MetalStreamPowerEvolutionBackend&
    operator= (MetalStreamPowerEvolutionBackend&&) noexcept;

    MetalStreamPowerEvolutionBackend (const MetalStreamPowerEvolutionBackend&) =
      delete;
    MetalStreamPowerEvolutionBackend&
    operator= (const MetalStreamPowerEvolutionBackend&) = delete;

    FractionalDrainage
    route_fractional (const TerrainView& terrain,
                      const FloodField& flood,
                      const LakeCensus& census,
                      std::span<const ChannelTangent> previous_tangent,
                      ChannelPersistence persistence) const override;

  private:
    void select_dry_routes (const TerrainGrid& grid,
                            std::span<const float> routing_surface_levels,
                            std::span<const ChannelTangent> previous_tangent,
                            ChannelPersistence persistence,
                            std::span<const std::uint8_t> ocean,
                            std::span<const WaterBodyId> water_body,
                            std::span<FractionalFlowRoute> routes,
                            std::span<DrainageDirection> directions,
                            std::span<slope_t> slopes) const override;

    class Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
