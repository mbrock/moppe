#include <moppe/game/landscape_gazetteer.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <ostream>
#include <ranges>
#include <sstream>
#include <stdexcept>

namespace moppe::game {
  namespace {
    struct SiteSample {
      terrain::CellIndex cell = terrain::no_cell;
      Vec3 position {};
      Vec3 normal { 0, 1, 0 };
      float wetness = 0.0f;
      float waterline_m = 0.0f;
      float erosion = 0.0f;
      float deposition = 0.0f;
      float habitat = 0.0f;
      float forest = 0.0f;
      float trail = 0.0f;
      bool water = false;
    };

    struct GazetteerCandidate {
      SiteSample site {};
      float score = -std::numeric_limits<float>::infinity ();
      Vec3 direction { 0, 0, 1 };

      bool present () const noexcept {
        return site.cell != terrain::no_cell;
      }
    };

    float metres (meters_t value) {
      return value.numerical_value_in (u::m);
    }

    std::size_t cell_offset (terrain::CellIndex cell) {
      return static_cast<std::size_t> (cell.value);
    }

    terrain::TerrainIndex terrain_index (terrain::CellIndex cell,
                                         std::size_t width) {
      return { cell_offset (cell) % width, cell_offset (cell) / width };
    }

    SiteSample sample_site (terrain::CellIndex cell,
                            const map::SurfaceGeometry& surface,
                            const map::SurfaceReadings& readings,
                            const terrain::FloodField& flood,
                            const terrain::LakeCensus& census) {
      const terrain::TerrainIndex index =
        terrain_index (cell, surface.domain ().width ());
      const auto geometry = surface[index];
      const auto reading = readings[index];
      const float x = static_cast<float> (index.column) *
                      metres (surface.domain ().spacing_x ());
      const float z = static_cast<float> (index.row) *
                      metres (surface.domain ().spacing_z ());
      const float ground = terrain::surface_elevation_value (
        spatial::get<terrain::surface_elevation> (geometry));
      const Vec3 normal =
        spatial::get<terrain::terrain_normal> (geometry).numerical_value_in (
          one);
      const std::size_t offset = cell_offset (cell);
      return {
        .cell = cell,
        .position = Vec3 (x, ground, z),
        .normal = normal,
        .wetness =
          spatial::get<terrain::soil_wetness> (reading).numerical_value_in (
            one),
        .waterline_m = spatial::get<terrain::waterline_distance> (reading)
                         .numerical_value_in (u::m),
        .erosion =
          spatial::get<map::erosion_exposure> (reading).numerical_value_in (
            one),
        .deposition =
          spatial::get<map::deposition_cover> (reading).numerical_value_in (
            one),
        .habitat =
          spatial::get<map::tree_habitat> (reading).numerical_value_in (one),
        .forest =
          spatial::get<map::forest_cover> (reading).numerical_value_in (one),
        .trail =
          spatial::get<terrain::trail_influence> (reading).numerical_value_in (
            one),
        .water = census.body_at (cell) != terrain::LakeCensus::dry ||
                 (offset < flood.ocean.size () && flood.ocean[offset] != 0),
      };
    }

    GazetteerSiteReadings site_readings (terrain::CellIndex cell,
                                         const map::SurfaceGeometry& surface,
                                         const map::SurfaceReadings& readings,
                                         const terrain::FloodField& flood) {
      const terrain::TerrainIndex index =
        terrain_index (cell, surface.domain ().width ());
      const auto geometry = surface[index];
      const auto reading = readings[index];
      return {
        .ground_elevation = spatial::get<terrain::surface_elevation> (geometry),
        .standing_water_depth = flood.water_depths ()[cell_offset (cell)],
        .waterline_distance =
          spatial::get<terrain::waterline_distance> (reading),
        .soil_wetness = spatial::get<terrain::soil_wetness> (reading),
        .erosion_exposure = spatial::get<map::erosion_exposure> (reading),
        .deposition_cover = spatial::get<map::deposition_cover> (reading),
        .tree_habitat = spatial::get<map::tree_habitat> (reading),
        .forest_cover = spatial::get<map::forest_cover> (reading),
        .trail_influence = spatial::get<terrain::trail_influence> (reading),
      };
    }

    Vec3 fallback_direction (terrain::CellIndex cell) {
      std::uint32_t hash = cell.value * 0x9e3779b9U + 0x85ebca6bU;
      hash ^= hash >> 16;
      const float angle =
        static_cast<float> (hash & 0xffffU) / 65536.0f * 2.0f * PI;
      return Vec3 (std::cos (angle), 0, std::sin (angle));
    }

    terrain::CellIndex cell_near (position_t position,
                                  const terrain::TerrainDomain& domain) {
      const Vec3& point = position_value (position);
      const float x =
        terrain::wrap_coordinate (point[0] / metres (domain.spacing_x ()),
                                  static_cast<float> (domain.width ()));
      const float z =
        terrain::wrap_coordinate (point[2] / metres (domain.spacing_z ()),
                                  static_cast<float> (domain.height ()));
      const terrain::TerrainIndex index {
        static_cast<std::size_t> (std::floor (x + 0.5f)) % domain.width (),
        static_cast<std::size_t> (std::floor (z + 0.5f)) % domain.height ()
      };
      return terrain::CellIndex { static_cast<std::uint32_t> (
        domain.offset (index)) };
    }

    Vec3 ground_direction (const SiteSample& site) {
      Vec3 direction (site.normal[0], 0, site.normal[2]);
      if (length2 (direction) < 1e-4f)
        return fallback_direction (site.cell);
      return normalized (direction);
    }

    float periodic_distance_m (terrain::CellIndex a,
                               terrain::CellIndex b,
                               const terrain::TerrainDomain& domain) {
      const int width = static_cast<int> (domain.width ());
      const int height = static_cast<int> (domain.height ());
      const int ax = static_cast<int> (a.value % domain.width ());
      const int az = static_cast<int> (a.value / domain.width ());
      const int bx = static_cast<int> (b.value % domain.width ());
      const int bz = static_cast<int> (b.value / domain.width ());
      const int dx = static_cast<int> (
        terrain::minimum_image_delta (static_cast<float> (ax - bx), width));
      const int dz = static_cast<int> (
        terrain::minimum_image_delta (static_cast<float> (az - bz), height));
      return std::hypot (dx * metres (domain.spacing_x ()),
                         dz * metres (domain.spacing_z ()));
    }

    bool separated_from (terrain::CellIndex cell,
                         const std::vector<terrain::CellIndex>& selected,
                         const terrain::TerrainDomain& domain) {
      const float period =
        std::min (metres (domain.period_x ()), metres (domain.period_z ()));
      const float minimum = std::clamp (period * 0.055f, 90.0f, 420.0f);
      return std::ranges::all_of (selected, [&] (terrain::CellIndex other) {
        return periodic_distance_m (cell, other, domain) >= minimum;
      });
    }

    template <typename Score>
    GazetteerCandidate
    choose_land_site (const map::SurfaceGeometry& surface,
                      const map::SurfaceReadings& readings,
                      const terrain::FloodField& flood,
                      const terrain::LakeCensus& census,
                      const std::vector<terrain::CellIndex>& selected,
                      Score score) {
      GazetteerCandidate best;
      const std::size_t width = surface.domain ().width ();
      const std::size_t height = surface.domain ().height ();
      const std::size_t stride = std::max<std::size_t> (1, width / 256);
      for (std::size_t z = 0; z < height; z += stride)
        for (std::size_t x = 0; x < width; x += stride) {
          const terrain::CellIndex cell { static_cast<std::uint32_t> (
            z * width + x) };
          const SiteSample site =
            sample_site (cell, surface, readings, flood, census);
          if (site.water || !separated_from (cell, selected, surface.domain ()))
            continue;
          const float candidate_score = score (site);
          if (candidate_score > best.score ||
              (candidate_score == best.score && cell < best.site.cell))
            best = { .site = site,
                     .score = candidate_score,
                     .direction = ground_direction (site) };
        }
      return best;
    }

    float
    surface_height (const map::SurfaceGeometry& surface, float x, float z) {
      return terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (x, 0, z))));
    }

    Vec3 clear_eye (Vec3 eye,
                    const map::SurfaceGeometry& surface,
                    meters_t minimum_clearance) {
      eye[1] = std::max (eye[1],
                         surface_height (surface, eye[0], eye[2]) +
                           metres (minimum_clearance));
      return eye;
    }

    void append_shot (LandscapeGazetteer& gazetteer,
                      std::vector<terrain::CellIndex>& selected,
                      std::string name,
                      GazetteerShotKind kind,
                      terrain::CellIndex site,
                      Vec3 eye,
                      Vec3 subject,
                      degrees_t field_of_view,
                      const map::SurfaceGeometry& surface,
                      const map::SurfaceReadings& readings,
                      const terrain::FloodField& flood,
                      meters_t minimum_clearance = 1.7f * u::m) {
      eye = clear_eye (eye, surface, minimum_clearance);
      const meters_t clearance =
        (eye[1] - surface_height (surface, eye[0], eye[2])) * u::m;
      gazetteer.shots.push_back ({
        .name = std::move (name),
        .kind = kind,
        .site = site,
        .eye = position (eye),
        .subject = position (subject),
        .vertical_field_of_view = field_of_view,
        .camera_clearance = clearance,
        .readings = site_readings (site, surface, readings, flood),
      });
      selected.push_back (site);
    }

    void append_candidate_view (LandscapeGazetteer& gazetteer,
                                std::vector<terrain::CellIndex>& selected,
                                std::string name,
                                GazetteerShotKind kind,
                                const GazetteerCandidate& candidate,
                                meters_t back,
                                meters_t sideways,
                                meters_t height,
                                meters_t look_ahead,
                                degrees_t field_of_view,
                                const map::SurfaceGeometry& surface,
                                const map::SurfaceReadings& readings,
                                const terrain::FloodField& flood) {
      if (!candidate.present ())
        return;
      const Vec3 forward = candidate.direction;
      const Vec3 side (-forward[2], 0, forward[0]);
      Vec3 eye = candidate.site.position - forward * metres (back) +
                 side * metres (sideways) + Vec3 (0, metres (height), 0);
      Vec3 subject = candidate.site.position + forward * metres (look_ahead);
      subject[1] = surface_height (surface, subject[0], subject[2]) + 1.4f;
      append_shot (gazetteer,
                   selected,
                   std::move (name),
                   kind,
                   candidate.site.cell,
                   eye,
                   subject,
                   field_of_view,
                   surface,
                   readings,
                   flood);
    }

    GazetteerCandidate
    choose_highland (const map::SurfaceGeometry& surface,
                     const map::SurfaceReadings& readings,
                     const terrain::FloodField& flood,
                     const terrain::LakeCensus& census,
                     const std::vector<terrain::CellIndex>& selected) {
      GazetteerCandidate best;
      const int width = static_cast<int> (surface.domain ().width ());
      const int height = static_cast<int> (surface.domain ().height ());
      const int stride = std::max (1, width / 256);
      const int radius = std::max (2, width / 64);
      for (int z = 0; z < height; z += stride)
        for (int x = 0; x < width; x += stride) {
          const terrain::CellIndex cell { static_cast<std::uint32_t> (
            z * width + x) };
          const SiteSample site =
            sample_site (cell, surface, readings, flood, census);
          if (site.water || site.normal[1] < 0.58f ||
              !separated_from (cell, selected, surface.domain ()))
            continue;
          float low = site.position[1];
          Vec3 direction = fallback_direction (cell);
          for (const std::array<int, 2> offset :
               { std::array { -radius, 0 },
                 std::array { radius, 0 },
                 std::array { 0, -radius },
                 std::array { 0, radius },
                 std::array { -radius, -radius },
                 std::array { radius, -radius },
                 std::array { -radius, radius },
                 std::array { radius, radius } }) {
            const int sx = terrain::wrap_index (x + offset[0], width);
            const int sz = terrain::wrap_index (z + offset[1], height);
            const float h =
              surface_height (surface,
                              sx * metres (surface.domain ().spacing_x ()),
                              sz * metres (surface.domain ().spacing_z ()));
            if (h < low) {
              low = h;
              direction = normalized (Vec3 (static_cast<float> (offset[0]),
                                            0,
                                            static_cast<float> (offset[1])));
            }
          }
          const float prominence = site.position[1] - low;
          const float score =
            site.position[1] + 2.4f * prominence - 120.0f * site.trail;
          if (score > best.score)
            best = { .site = site, .score = score, .direction = direction };
        }
      return best;
    }

    std::optional<GazetteerCandidate>
    choose_coast (const map::SurfaceGeometry& surface,
                  const map::SurfaceReadings& readings,
                  const terrain::FloodField& flood,
                  const terrain::LakeCensus& census) {
      if (!flood.has_ocean)
        return std::nullopt;
      GazetteerCandidate best;
      const int width = static_cast<int> (surface.domain ().width ());
      const int height = static_cast<int> (surface.domain ().height ());
      for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x) {
          const terrain::CellIndex cell { static_cast<std::uint32_t> (
            z * width + x) };
          const SiteSample site =
            sample_site (cell, surface, readings, flood, census);
          if (site.water)
            continue;
          for (const std::array<int, 2> offset : { std::array { -1, 0 },
                                                   std::array { 1, 0 },
                                                   std::array { 0, -1 },
                                                   std::array { 0, 1 } }) {
            const int nx = terrain::wrap_index (x + offset[0], width);
            const int nz = terrain::wrap_index (z + offset[1], height);
            const std::size_t neighbour =
              static_cast<std::size_t> (nz) * width + nx;
            if (neighbour >= flood.ocean.size () || !flood.ocean[neighbour])
              continue;
            const float above_sea = site.position[1] - flood.sea_level;
            const float useful_height =
              1.0f - std::min (std::abs (above_sea - 9.0f) / 35.0f, 1.0f);
            const float score =
              3.0f * useful_height + site.normal[1] - 0.8f * site.forest;
            if (score > best.score)
              best = { .site = site,
                       .score = score,
                       .direction =
                         normalized (Vec3 (static_cast<float> (offset[0]),
                                           0,
                                           static_cast<float> (offset[1]))) };
          }
        }
      return best.present () ? std::optional (best) : std::nullopt;
    }

    std::optional<GazetteerCandidate>
    choose_lake_shore (const map::SurfaceGeometry& surface,
                       const map::SurfaceReadings& readings,
                       const terrain::FloodField& flood,
                       const terrain::LakeCensus& census) {
      const terrain::WaterBody* lake = nullptr;
      for (const terrain::WaterBody& body : census.water_bodies ())
        if (body.classification != terrain::WaterBodyClass::Sea &&
            water_body_is_permanent (body) && !body.channel_like &&
            (!lake || body.area > lake->area))
          lake = &body;
      if (!lake)
        return std::nullopt;

      GazetteerCandidate best;
      const int width = static_cast<int> (surface.domain ().width ());
      const int height = static_cast<int> (surface.domain ().height ());
      for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x) {
          const terrain::CellIndex water_cell { static_cast<std::uint32_t> (
            z * width + x) };
          if (census.body_at (water_cell) != lake->id)
            continue;
          for (const std::array<int, 2> offset : { std::array { -1, 0 },
                                                   std::array { 1, 0 },
                                                   std::array { 0, -1 },
                                                   std::array { 0, 1 } }) {
            const int lx = terrain::wrap_index (x - offset[0], width);
            const int lz = terrain::wrap_index (z - offset[1], height);
            const terrain::CellIndex land_cell { static_cast<std::uint32_t> (
              lz * width + lx) };
            const SiteSample site =
              sample_site (land_cell, surface, readings, flood, census);
            if (site.water)
              continue;
            const float score =
              site.normal[1] + 0.35f * site.habitat - 0.6f * site.trail;
            if (score > best.score)
              best = { .site = site,
                       .score = score,
                       .direction =
                         normalized (Vec3 (static_cast<float> (offset[0]),
                                           0,
                                           static_cast<float> (offset[1]))) };
          }
        }
      return best.present () ? std::optional (best) : std::nullopt;
    }

    Vec3 trail_point (const terrain::TrailAlignmentPoint& point,
                      const map::SurfaceGeometry& surface) {
      return Vec3 (
        point.x_m, surface_height (surface, point.x_m, point.z_m), point.z_m);
    }

    void append_trail_views (LandscapeGazetteer& gazetteer,
                             std::vector<terrain::CellIndex>& selected,
                             const terrain::TrailNetwork& trail,
                             position_t spawn,
                             const map::SurfaceGeometry& surface,
                             const map::SurfaceReadings& readings,
                             const terrain::FloodField& flood) {
      Vec3 anchor = position_value (spawn);
      anchor[1] = surface_height (surface, anchor[0], anchor[2]);
      Vec3 direction (0, 0, 1);
      if (trail.alignment.points.size () >= 2) {
        const Vec3 first =
          trail_point (trail.alignment.points.front (), surface);
        const Vec3 ahead =
          trail_point (trail.alignment.points[std::min<std::size_t> (
                         8, trail.alignment.points.size () - 1)],
                       surface);
        direction = ahead - first;
        direction[1] = 0;
        if (length2 (direction) > 1e-5f)
          direction = normalized (direction);
      }
      const terrain::CellIndex home = trail.plan.home_base != terrain::no_cell
                                        ? trail.plan.home_base
                                        : cell_near (spawn, surface.domain ());
      Vec3 eye = anchor - direction * 7.0f + Vec3 (0, 2.8f, 0);
      Vec3 subject = anchor + direction * 13.0f + Vec3 (0, 1.2f, 0);
      append_shot (gazetteer,
                   selected,
                   "trailhead-rider",
                   GazetteerShotKind::Gameplay,
                   home,
                   eye,
                   subject,
                   82.0f * u::deg,
                   surface,
                   readings,
                   flood);

      if (trail.alignment.points.size () < 12)
        return;
      const std::size_t middle = trail.alignment.points.size () / 2;
      const std::size_t ahead_index =
        (middle +
         std::max<std::size_t> (3, trail.alignment.points.size () / 24)) %
        trail.alignment.points.size ();
      anchor = trail_point (trail.alignment.points[middle], surface);
      Vec3 ahead = trail_point (trail.alignment.points[ahead_index], surface);
      direction = ahead - anchor;
      direction[1] = 0;
      if (length2 (direction) < 1e-5f)
        direction = fallback_direction (home);
      else
        direction = normalized (direction);
      const Vec3 side (-direction[2], 0, direction[0]);
      eye = anchor - direction * 10.0f + side * 2.5f + Vec3 (0, 3.4f, 0);
      subject = anchor + direction * 22.0f + Vec3 (0, 1.4f, 0);
      append_shot (gazetteer,
                   selected,
                   "trail-scenic-turn",
                   GazetteerShotKind::Gameplay,
                   cell_near (position (anchor), surface.domain ()),
                   eye,
                   subject,
                   76.0f * u::deg,
                   surface,
                   readings,
                   flood);
    }

    void append_water_inspection (LandscapeGazetteer& gazetteer,
                                  std::vector<terrain::CellIndex>& selected,
                                  std::string name,
                                  WaterShot shot,
                                  degrees_t field_of_view,
                                  const map::SurfaceGeometry& surface,
                                  const map::SurfaceReadings& readings,
                                  const terrain::FloodField& flood,
                                  const terrain::LakeCensus& census,
                                  const terrain::DrainageGraph& drainage,
                                  const terrain::RiverNetwork& rivers) {
      const std::optional<WaterInspection> inspection =
        choose_water_inspection (
          shot, surface, flood, census, drainage, rivers);
      if (!inspection)
        return;
      append_shot (gazetteer,
                   selected,
                   std::move (name),
                   GazetteerShotKind::Freshwater,
                   terrain::CellIndex { inspection->cell },
                   inspection->eye,
                   inspection->target,
                   field_of_view,
                   surface,
                   readings,
                   flood,
                   3.5f * u::m);
    }

  }

  std::string_view gazetteer_shot_kind_name (GazetteerShotKind kind) noexcept {
    switch (kind) {
    case GazetteerShotKind::Gameplay:
      return "gameplay";
    case GazetteerShotKind::Habitat:
      return "habitat";
    case GazetteerShotKind::Landform:
      return "landform";
    case GazetteerShotKind::Freshwater:
      return "freshwater";
    case GazetteerShotKind::Coast:
      return "coast";
    case GazetteerShotKind::Aerial:
      return "aerial";
    }
    return "landform";
  }

  LandscapeGazetteer
  plan_landscape_gazetteer (const map::SurfaceGeometry& surface,
                            const map::SurfaceReadings& readings,
                            const terrain::FloodField& flood,
                            const terrain::LakeCensus& census,
                            const terrain::DrainageGraph& drainage,
                            const terrain::RiverNetwork& rivers,
                            const terrain::TrailNetwork& trail,
                            position_t spawn) {
    if (surface.domain () != readings.domain () ||
        surface.domain () != flood.domain () ||
        surface.domain () != drainage.readings.domain () ||
        surface.domain () != trail.domain ||
        surface.domain ().size () != census.cell_count ())
      throw std::invalid_argument (
        "gazetteer inputs do not share one terrain domain");

    LandscapeGazetteer gazetteer;
    std::vector<terrain::CellIndex> selected;
    append_trail_views (
      gazetteer, selected, trail, spawn, surface, readings, flood);

    const GazetteerCandidate forest = choose_land_site (
      surface, readings, flood, census, selected, [] (const SiteSample& site) {
        return 4.0f * site.forest + 1.3f * site.habitat + 0.4f * site.wetness +
               1.5f * site.normal[1] - 1.2f * site.trail;
      });
    append_candidate_view (gazetteer,
                           selected,
                           "forest-floor",
                           GazetteerShotKind::Habitat,
                           forest,
                           2.0f * u::m,
                           0.8f * u::m,
                           2.0f * u::m,
                           20.0f * u::m,
                           68.0f * u::deg,
                           surface,
                           readings,
                           flood);

    const GazetteerCandidate meadow = choose_land_site (
      surface, readings, flood, census, selected, [] (const SiteSample& site) {
        return 3.2f * (1.0f - site.forest) + 1.8f * site.normal[1] +
               0.6f * site.habitat + 0.25f * site.wetness - 1.1f * site.trail;
      });
    append_candidate_view (gazetteer,
                           selected,
                           "open-meadow",
                           GazetteerShotKind::Habitat,
                           meadow,
                           5.0f * u::m,
                           2.0f * u::m,
                           4.5f * u::m,
                           55.0f * u::m,
                           72.0f * u::deg,
                           surface,
                           readings,
                           flood);

    const GazetteerCandidate wetland = choose_land_site (
      surface, readings, flood, census, selected, [] (const SiteSample& site) {
        const float shore =
          1.0f - std::min (std::abs (site.waterline_m - 24.0f) / 110.0f, 1.0f);
        return 3.0f * site.wetness + 1.7f * shore + site.normal[1] -
               0.5f * site.forest;
      });
    append_candidate_view (gazetteer,
                           selected,
                           "wetland-edge",
                           GazetteerShotKind::Habitat,
                           wetland,
                           3.5f * u::m,
                           -1.5f * u::m,
                           2.4f * u::m,
                           28.0f * u::m,
                           66.0f * u::deg,
                           surface,
                           readings,
                           flood);

    const GazetteerCandidate geology = choose_land_site (
      surface, readings, flood, census, selected, [] (const SiteSample& site) {
        return 4.0f * site.erosion + 1.1f * (1.0f - site.normal[1]) +
               0.4f * site.deposition - 0.8f * site.forest;
      });
    append_candidate_view (gazetteer,
                           selected,
                           "eroded-slope",
                           GazetteerShotKind::Landform,
                           geology,
                           24.0f * u::m,
                           15.0f * u::m,
                           18.0f * u::m,
                           6.0f * u::m,
                           62.0f * u::deg,
                           surface,
                           readings,
                           flood);

    const GazetteerCandidate highland =
      choose_highland (surface, readings, flood, census, selected);
    append_candidate_view (gazetteer,
                           selected,
                           "highland-vista",
                           GazetteerShotKind::Landform,
                           highland,
                           9.0f * u::m,
                           8.0f * u::m,
                           7.5f * u::m,
                           130.0f * u::m,
                           64.0f * u::deg,
                           surface,
                           readings,
                           flood);

    append_water_inspection (gazetteer,
                             selected,
                             "headwater",
                             WaterShot::Stream,
                             66.0f * u::deg,
                             surface,
                             readings,
                             flood,
                             census,
                             drainage,
                             rivers);
    append_water_inspection (gazetteer,
                             selected,
                             "river-reach",
                             WaterShot::River,
                             68.0f * u::deg,
                             surface,
                             readings,
                             flood,
                             census,
                             drainage,
                             rivers);
    append_water_inspection (gazetteer,
                             selected,
                             "confluence",
                             WaterShot::Confluence,
                             64.0f * u::deg,
                             surface,
                             readings,
                             flood,
                             census,
                             drainage,
                             rivers);
    append_water_inspection (gazetteer,
                             selected,
                             "river-mouth",
                             WaterShot::Mouth,
                             68.0f * u::deg,
                             surface,
                             readings,
                             flood,
                             census,
                             drainage,
                             rivers);
    append_water_inspection (gazetteer,
                             selected,
                             "waterfall",
                             WaterShot::Waterfall,
                             58.0f * u::deg,
                             surface,
                             readings,
                             flood,
                             census,
                             drainage,
                             rivers);

    if (const std::optional<GazetteerCandidate> shore =
          choose_lake_shore (surface, readings, flood, census)) {
      append_candidate_view (gazetteer,
                             selected,
                             "lake-shore",
                             GazetteerShotKind::Freshwater,
                             *shore,
                             18.0f * u::m,
                             10.0f * u::m,
                             7.0f * u::m,
                             34.0f * u::m,
                             64.0f * u::deg,
                             surface,
                             readings,
                             flood);
    }
    append_water_inspection (gazetteer,
                             selected,
                             "lake-overview",
                             WaterShot::Lake,
                             58.0f * u::deg,
                             surface,
                             readings,
                             flood,
                             census,
                             drainage,
                             rivers);

    if (const std::optional<GazetteerCandidate> coast =
          choose_coast (surface, readings, flood, census)) {
      const Vec3 side (-coast->direction[2], 0, coast->direction[0]);
      // Stand just off the beach: an inland documentary camera can be
      // swallowed by the very forest edge this view is trying to establish.
      Vec3 eye = coast->site.position + coast->direction * 18.0f - side * 12.0f;
      eye[1] = flood.sea_level + 7.5f;
      // Read along the coast here; the following shot is deliberately the
      // seaward horizon. Their subjects should document different geometry,
      // not merely change the lens on one composition.
      Vec3 subject =
        coast->site.position + coast->direction * 8.0f + side * 165.0f;
      subject[1] = flood.sea_level + 1.5f;
      append_shot (gazetteer,
                   selected,
                   "coastline",
                   GazetteerShotKind::Coast,
                   coast->site.cell,
                   eye,
                   subject,
                   68.0f * u::deg,
                   surface,
                   readings,
                   flood,
                   4.0f * u::m);

      eye = coast->site.position - coast->direction * 10.0f + Vec3 (0, 5.5f, 0);
      subject = coast->site.position + coast->direction * 420.0f;
      subject[1] = flood.sea_level + 3.0f;
      append_shot (gazetteer,
                   selected,
                   "ocean-horizon",
                   GazetteerShotKind::Coast,
                   coast->site.cell,
                   eye,
                   subject,
                   62.0f * u::deg,
                   surface,
                   readings,
                   flood,
                   4.0f * u::m);
    }

    if (highland.present ()) {
      const GazetteerCandidate destination =
        meadow.present () ? meadow : forest;
      Vec3 subject = destination.present ()
                       ? destination.site.position
                       : highland.site.position + highland.direction * 400.0f;
      Vec3 eye = highland.site.position - highland.direction * 180.0f +
                 Vec3 (0, 260.0f, 0);
      append_shot (gazetteer,
                   selected,
                   "aerial-basin",
                   GazetteerShotKind::Aerial,
                   highland.site.cell,
                   eye,
                   subject,
                   56.0f * u::deg,
                   surface,
                   readings,
                   flood,
                   160.0f * u::m);
    }

    return gazetteer;
  }

  void write_landscape_gazetteer_csv (std::ostream& output,
                                      const LandscapeGazetteer& gazetteer) {
    output << "index,filename,name,category,site_cell,eye_x_m,eye_y_m,eye_z_m,"
              "subject_x_m,subject_y_m,subject_z_m,vertical_fov_deg,"
              "camera_clearance_m,ground_elevation_m,water_depth_m,"
              "waterline_distance_m,soil_wetness,erosion_exposure,"
              "deposition_cover,tree_habitat,forest_cover,trail_influence\n";
    output << std::setprecision (7);
    for (std::size_t index = 0; index < gazetteer.shots.size (); ++index) {
      const GazetteerShot& shot = gazetteer.shots[index];
      const Vec3& eye = position_value (shot.eye);
      const Vec3& subject = position_value (shot.subject);
      output
        << index << ',' << gazetteer_image_filename (index, shot.name) << ','
        << shot.name << ',' << gazetteer_shot_kind_name (shot.kind) << ','
        << shot.site.value << ',' << eye[0] << ',' << eye[1] << ',' << eye[2]
        << ',' << subject[0] << ',' << subject[1] << ',' << subject[2] << ','
        << shot.vertical_field_of_view.numerical_value_in (u::deg) << ','
        << shot.camera_clearance.numerical_value_in (u::m) << ','
        << terrain::surface_elevation_value (shot.readings.ground_elevation)
        << ',' << shot.readings.standing_water_depth.numerical_value_in (u::m)
        << ',' << shot.readings.waterline_distance.numerical_value_in (u::m)
        << ',' << shot.readings.soil_wetness.numerical_value_in (one) << ','
        << shot.readings.erosion_exposure.numerical_value_in (one) << ','
        << shot.readings.deposition_cover.numerical_value_in (one) << ','
        << shot.readings.tree_habitat.numerical_value_in (one) << ','
        << shot.readings.forest_cover.numerical_value_in (one) << ','
        << shot.readings.trail_influence.numerical_value_in (one) << '\n';
    }
  }

  std::string gazetteer_image_filename (std::size_t index,
                                        std::string_view name) {
    std::ostringstream filename;
    filename << "frame-" << std::setfill ('0') << std::setw (2) << index << '-'
             << name << ".png";
    return filename.str ();
  }
}
