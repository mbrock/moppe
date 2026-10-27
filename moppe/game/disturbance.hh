#ifndef MOPPE_GAME_DISTURBANCE_HH
#define MOPPE_GAME_DISTURBANCE_HH

#include <moppe/map/surface.hh>
#include <moppe/terrain/roads.hh>
#include <moppe/terrain/watercourse.hh>

#include <cstdint>

namespace moppe::game {
  // What has happened to the ground, written on it so that the vegetation
  // can read it: a world's history in its clearings. Lightning fires run
  // through the dry pine country, downwind and uphill, and stop at water,
  // bog, and bare rock; each place the roads join kept its pastures open
  // while it was lived in (the sanctuaries their upland grazing, after
  // Peatfield) and has been walked back into since it went quiet; and the
  // roads keep their verges cut. READINGS are the world as it would stand
  // had nothing happened, which is also what the fires burn through.
  map::DisturbanceMap plan_disturbance (const map::SurfaceGeometry& geometry,
                                        const map::SurfaceReadings& readings,
                                        const terrain::WaterSheets& water,
                                        const terrain::RoadNetwork& roads,
                                        std::uint32_t seed);
}

#endif
