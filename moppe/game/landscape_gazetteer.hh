#ifndef MOPPE_GAME_LANDSCAPE_GAZETTEER_HH
#define MOPPE_GAME_LANDSCAPE_GAZETTEER_HH

#include <moppe/game/water_capture.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/trail.hh>

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace moppe::game {
  // A gazetteer is documentary rather than cinematic: each entry is one
  // independently useful reading of a completed landscape. Categories keep a
  // report balanced without pretending unlike physical measurements share a
  // generic score.
  enum class GazetteerShotKind {
    Gameplay,
    Habitat,
    Landform,
    Freshwater,
    Coast,
    Aerial,
    Lighting
  };

  std::string_view gazetteer_shot_kind_name (GazetteerShotKind kind) noexcept;

  // The actual typed readings at the place a shot documents. They travel with
  // the image so a visual regression can be interpreted against the world
  // that produced it rather than against a guessed biome label.
  struct GazetteerSiteReadings {
    terrain::SurfaceElevation ground_elevation {};
    terrain::StandingWaterDepth standing_water_depth {};
    terrain::WaterlineDistance waterline_distance {};
    terrain::SoilWetness soil_wetness {};
    map::ErosionExposure erosion_exposure {};
    map::DepositionCover deposition_cover {};
    map::TreeHabitat tree_habitat {};
    map::ForestCover forest_cover {};
    terrain::TrailInfluence trail_influence {};
  };

  struct GazetteerShot {
    std::string name;
    GazetteerShotKind kind = GazetteerShotKind::Landform;
    terrain::CellIndex site = terrain::no_cell;
    position_t eye {};
    position_t subject {};
    degrees_t vertical_field_of_view = 64.0f * u::deg;
    meters_t camera_clearance {};
    GazetteerSiteReadings readings {};
  };

  struct LandscapeGazetteer {
    std::vector<GazetteerShot> shots;

    bool empty () const noexcept {
      return shots.empty ();
    }
  };

  // The sun direction (toward the sun, in the frame the capture will light
  // with) lets the plan include deliberate lighting studies: forest against
  // the sun, shadows raking across the view, and shaded interiors.
  LandscapeGazetteer
  plan_landscape_gazetteer (const map::SurfaceGeometry& surface,
                            const map::SurfaceReadings& readings,
                            const terrain::FloodField& flood,
                            const terrain::LakeCensus& census,
                            const terrain::DrainageGraph& drainage,
                            const terrain::RiverNetwork& rivers,
                            const terrain::TrailNetwork& trail,
                            position_t spawn,
                            Vec3 sun_direction);

  // CSV is the deliberate numerical exit. Column names carry their units and
  // every row names the exact image file the application will write.
  void write_landscape_gazetteer_csv (std::ostream& output,
                                      const LandscapeGazetteer& gazetteer);

  std::string gazetteer_image_filename (std::size_t index,
                                        std::string_view name);
}

#endif
