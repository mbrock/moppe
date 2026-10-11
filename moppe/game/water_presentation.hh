#ifndef MOPPE_GAME_WATER_PRESENTATION_HH
#define MOPPE_GAME_WATER_PRESENTATION_HH

#include <moppe/render/renderer.hh>
#include <moppe/terrain/watercourse.hh>

#include <span>

namespace moppe::game {
  // Present the typed water bundle and coarse ocean mesh. The bundle is
  // borrowed for the call; each texture writes its final bytes directly into
  // backend staging storage.
  // The ground under it says which terrain patches are dry.
  void upload_water (render::Renderer& renderer,
                     const terrain::WaterSheets& water,
                     std::span<const terrain::SurfaceElevation> ground,
                     meters_t water_datum,
                     const spatial_extent_t& world_extent);
}

#endif
