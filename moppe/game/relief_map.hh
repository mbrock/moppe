#ifndef MOPPE_GAME_RELIEF_MAP_HH
#define MOPPE_GAME_RELIEF_MAP_HH

#include <moppe/map/surface.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/roads.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/watercourse.hh>

#include <cstdint>
#include <vector>

namespace moppe::game {
  // The world as a walker's map: one whole period of it, north at the top,
  // drawn the way a mountain sheet is -- relief shaded from the north-west,
  // open ground pale and forest green, brown contours every ten metres
  // with every fifth one heavier, water blue with its shoreline, the
  // rivers by their catchments, a kilometre grid, the trail in red
  // dashes, and the planned roads (roads.hh): cased roads, dotted
  // footpaths, and the peak sanctuaries with the sightlines between them.
  // Heights count from the sea.
  struct ReliefMap {
    // A top that stands over everything around it, to be marked with its
    // height.
    struct Summit {
      float x_m = 0.0f;
      float z_m = 0.0f;
      float height_m = 0.0f;
    };

    int size = 0;
    // Rows from the north edge southward, eight bits each of red, green,
    // blue, and an opaque alpha.
    std::vector<std::uint8_t> rgba;
    std::vector<Summit> summits;
    float period_x_m = 0.0f;
    float period_z_m = 0.0f;
    float contour_interval_m = 10.0f;
    float highest_m = 0.0f;
  };

  // Paints the sheet `size` pixels square. The world is periodic, so the
  // picture tiles, and a view of it may be scrolled across its edge.
  [[nodiscard]] ReliefMap
  paint_relief_map (const map::SurfaceGeometry& surface,
                    const map::SurfaceReadings& readings,
                    const terrain::WaterSheets& water,
                    const terrain::RiverNetwork& rivers,
                    const terrain::TrailNetwork& trails,
                    const terrain::RoadNetwork& roads,
                    meters_t sea_level,
                    int size);
}

#endif
