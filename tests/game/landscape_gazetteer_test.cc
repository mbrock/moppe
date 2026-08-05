#include <moppe/game/landscape_gazetteer.hh>

#include <tests/test.hh>

#include <sstream>
#include <vector>

using namespace moppe;

namespace {
  struct GazetteerFixture {
    static constexpr int side = 17;
    static constexpr std::size_t count = side * side;

    terrain::TerrainDomain domain = terrain::TerrainDomain (
      side, side, spatial_extent_in_metres (Vec3 (1600, 0, 1600)));
    map::SurfaceGeometry surface = map::SurfaceGeometry (domain);
    map::SurfaceReadings readings = map::SurfaceReadings (domain);
    terrain::FloodField flood;
    terrain::LakeCensus census;
    terrain::DrainageGraph drainage;
    terrain::RiverNetwork rivers;
    terrain::TrailNetwork trail;

    GazetteerFixture ()
        : flood { .surface = terrain::make_flood_surface (
                    domain,
                    std::vector<float> (count, 0.0f),
                    std::vector<float> (count, 0.0f)),
                  .sea_level = 0.0f,
                  .has_ocean = false,
                  .ocean = std::vector<std::uint8_t> (count, 0),
                  .spill_receiver = std::vector<terrain::CellIndex> (count) },
          census (
            std::vector<terrain::WaterBodyId> (count, terrain::LakeCensus::dry),
            {}),
          drainage { .readings = terrain::DrainageReadings (domain),
                     .receiver = std::vector<terrain::CellIndex> (count) },
          trail { .domain = domain, .use = terrain::TrailUseMap (domain) } {
      for (int z = 0; z < side; ++z)
        for (int x = 0; x < side; ++x) {
          const float hill =
            45.0f + 2.0f * x + 0.7f * z + 18.0f * std::sin (0.45f * x);
          spatial::get<terrain::surface_elevation> (surface[{
            static_cast<std::size_t> (x), static_cast<std::size_t> (z) }]) =
            terrain::surface_elevation_point (hill * u::m);
          const std::size_t cell = z * side + x;
          const auto cell_index = static_cast<std::uint32_t> (cell);
          flood.spill_receiver[cell] = terrain::CellIndex { cell_index };
          drainage.receiver[cell] = terrain::CellIndex { cell_index };
        }
      map::rebuild_geometry (surface);
    }
  };
}

MOPPE_TEST (landscape_gazetteer_is_a_deterministic_typed_landscape_reading) {
  GazetteerFixture fixture;
  const position_t spawn = position (Vec3 (400, 70, 400));
  const Vec3 sun = normalized (Vec3 (0.42f, 0.37f, 0.83f));
  const game::LandscapeGazetteer first =
    game::plan_landscape_gazetteer (fixture.surface,
                                    fixture.readings,
                                    fixture.flood,
                                    fixture.census,
                                    fixture.drainage,
                                    fixture.rivers,
                                    fixture.trail,
                                    spawn,
                                    sun);
  const game::LandscapeGazetteer second =
    game::plan_landscape_gazetteer (fixture.surface,
                                    fixture.readings,
                                    fixture.flood,
                                    fixture.census,
                                    fixture.drainage,
                                    fixture.rivers,
                                    fixture.trail,
                                    spawn,
                                    sun);

  MOPPE_CHECK (first.shots.size () >= 5);
  MOPPE_CHECK (first.shots.size () == second.shots.size ());
  for (std::size_t index = 0; index < first.shots.size (); ++index) {
    const game::GazetteerShot& left = first.shots[index];
    const game::GazetteerShot& right = second.shots[index];
    MOPPE_CHECK (left.name == right.name);
    MOPPE_CHECK (left.site == right.site);
    MOPPE_CHECK (position_value (left.eye) == position_value (right.eye));
    MOPPE_CHECK (position_value (left.subject) ==
                 position_value (right.subject));
    MOPPE_CHECK (left.vertical_field_of_view == right.vertical_field_of_view);
    MOPPE_CHECK (left.camera_clearance >= 1.7f * u::m);
  }
}

MOPPE_TEST (landscape_gazetteer_csv_names_units_and_exact_images) {
  game::LandscapeGazetteer gazetteer;
  gazetteer.shots.push_back ({
    .name = "open-meadow",
    .kind = game::GazetteerShotKind::Habitat,
    .site = terrain::CellIndex { 42 },
    .eye = position (Vec3 (1, 2, 3)),
    .subject = position (Vec3 (4, 5, 6)),
    .vertical_field_of_view = 63.0f * u::deg,
    .camera_clearance = 2.0f * u::m,
  });
  std::ostringstream output;
  game::write_landscape_gazetteer_csv (output, gazetteer);
  const std::string csv = output.str ();

  MOPPE_CHECK (game::gazetteer_image_filename (0, "open-meadow") ==
               "frame-00-open-meadow.png");
  MOPPE_CHECK (csv.find ("eye_x_m") != std::string::npos);
  MOPPE_CHECK (csv.find ("vertical_fov_deg") != std::string::npos);
  MOPPE_CHECK (csv.find ("frame-00-open-meadow.png,open-meadow,habitat") !=
               std::string::npos);
}
