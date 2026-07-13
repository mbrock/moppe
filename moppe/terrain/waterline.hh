#ifndef MOPPE_TERRAIN_WATERLINE_HH
#define MOPPE_TERRAIN_WATERLINE_HH

#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/terrain_view.hh>
#include <moppe/terrain/types.hh>

#include <vector>

namespace moppe::terrain {
  // The waterline as geometry: ordered polylines along the zero set of
  // water-minus-ground, extracted once per world by marching squares
  // over the terrain lattice.  Within a cell the zero set of the
  // bilinear difference is a hyperbola branch; one or two straight
  // segments per boundary cell approximate it to well under a
  // centimetre at metre-scale cells.  This is the one true wet/dry
  // curve that the water discard, the tile probe, and the terrain wet
  // band each rediscover per fragment -- extracted as a reusable
  // reading for conforming geometry, shore ribbons, audio, and
  // gameplay.  Deterministic: cells scan in index order and chains
  // start from the smallest lattice edge.
  struct WaterlineContour {
    // The wet body this stretch of shoreline belongs to.
    WaterBodyId body = no_water_body;
    // Closed loops (island coasts, lake shores) repeat no point; the
    // last segment implicitly returns to the front.  Open chains occur
    // only on bounded maps where the water body meets the map edge.
    bool closed = false;
    // Interleaved (x, z) positions in world meters on the unique
    // lattice's coordinates.
    std::vector<float> points;

    std::size_t size () const noexcept {
      return points.size () / 2;
    }
  };

  struct Waterline {
    TerrainGrid source_grid;
    std::vector<WaterlineContour> contours;
  };

  // surface is the painted water sheet (or the flood's water level):
  // ground height in dry cells, water level in wet ones, in the same
  // normalized units as the terrain samples.  wet_epsilon matches the
  // census convention: a cell is wet where surface - ground exceeds it.
  Waterline extract_waterline (const TerrainView& terrain,
                               const ScalarRaster& surface,
                               const LakeCensus& census,
                               float wet_epsilon = 1e-7f);

  // Horizontal distance in meters from every lattice node to the
  // nearest waterline segment, exact within the band and clamped to
  // band_m beyond it.  The terrain's wet-soil band keys off this
  // instead of the vertical water-column proxy, so damp ground hugs
  // the actual shoreline curve.
  ScalarRaster waterline_proximity (const Waterline& waterline,
                                    float band_m = 8.0f);
}

#endif
