#include <moppe/game/water_capture.hh>

#include <tests/test.hh>

using namespace moppe;

MOPPE_TEST (water_capture_feature_names_round_trip) {
  const game::WaterShot shots[] = {
    game::WaterShot::Stream,     game::WaterShot::River,
    game::WaterShot::Confluence, game::WaterShot::Mouth,
    game::WaterShot::Waterfall,  game::WaterShot::Lake
  };
  for (const game::WaterShot shot : shots) {
    const std::string_view name = game::water_shot_name (shot);
    MOPPE_CHECK (game::parse_water_shot (name) == shot);
  }
  MOPPE_CHECK (game::parse_water_shot ("headwater") == game::WaterShot::Stream);
  MOPPE_CHECK (!game::parse_water_shot ("ocean"));
}
