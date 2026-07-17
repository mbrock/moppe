#include <moppe/game/water_presentation.hh>

#include <moppe/profile.hh>

#include <stdexcept>

namespace moppe::game {
  void WaterPresentation::reset (render::OceanSetup ocean) {
    m_ocean = ocean;
    m_levels.clear ();
    m_flow.clear ();
  }

  void WaterPresentation::refresh (const map::WaterSurface& water,
                                   meters_t terrain_height_scale) {
    MOPPE_PROFILE_ZONE ("water.materialize_presentation");
    if (terrain_height_scale <= 0.0f * u::m)
      throw std::invalid_argument (
        "Water presentation needs a positive terrain height scale");
    const auto& elevation =
      spatial::get<map::surface_elevation> (water.sections ());
    const auto& amplitude =
      spatial::get<map::wave_amplitude> (water.sections ());
    const auto& velocity =
      spatial::get<map::water_velocity> (water.sections ());
    m_levels.resize (2 * water.sections ().size ());
    m_flow.resize (2 * water.sections ().size ());
    for (std::size_t offset = 0; offset < water.sections ().size (); ++offset) {
      m_levels[2 * offset] =
        elevation[offset].quantity_from_zero ().numerical_value_in (u::m) /
        meters_value (terrain_height_scale);
      m_levels[2 * offset + 1] = amplitude[offset].numerical_value_in (one);
      const Vec3 flow = velocity[offset].numerical_value_in (u::m / u::s);
      m_flow[2 * offset] = flow[0];
      m_flow[2 * offset + 1] = flow[2];
    }
  }

  void WaterPresentation::upload (render::Renderer& renderer) const {
    MOPPE_PROFILE_ZONE ("water.upload_presentation");
    renderer.set_ocean (m_ocean, m_levels);
    renderer.set_water_flow (m_flow);
  }
}
