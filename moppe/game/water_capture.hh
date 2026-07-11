#ifndef MOPPE_GAME_WATER_CAPTURE_HH
#define MOPPE_GAME_WATER_CAPTURE_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <cstdint>
#include <optional>
#include <string_view>

namespace moppe::game {
  enum class WaterShot {
    River,
    Confluence,
    Mouth,
    Waterfall,
    Lake
  };

  struct WaterInspection {
    WaterShot kind;
    std::uint32_t cell;
    float score;
    Vector3D eye;
    Vector3D target;
  };

  std::optional<WaterShot> parse_water_shot (std::string_view name) noexcept;
  std::string_view water_shot_name (WaterShot shot) noexcept;

  std::optional<WaterInspection> choose_water_inspection
    (WaterShot shot, const map::HeightMap& map,
     const terrain::FloodField& flood,
     const terrain::LakeCensus& census,
     const terrain::DrainageGraph& drainage,
     const terrain::RiverNetwork& rivers);
}

#endif
