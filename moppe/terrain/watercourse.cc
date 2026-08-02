#include <moppe/terrain/watercourse.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    float rapid_signal (float slope) {
      return std::clamp ((slope - 0.035f) / 0.24f, 0.0f, 1.0f);
    }

    float wave_factor (const FloodField& flood,
                       const LakeCensus& census,
                       std::size_t cell) {
      if (flood.ocean[cell])
        return 1.0f;
      const WaterBodyId body =
        census.body_at (CellIndex { static_cast<std::uint32_t> (cell) });
      if (!census.domain ().contains (body))
        return 0.0f;
      switch (census.water_body (body).classification) {
      case WaterBodyClass::Sea:
        return 1.0f;
      case WaterBodyClass::Lake:
        return 0.10f;
      case WaterBodyClass::Pond:
        return 0.04f;
      case WaterBodyClass::Puddle:
        return 0.0f;
      }
      return 0.0f;
    }
  }

  WaterSheets
  detail::paint_watercourses (const TerrainDomain& terrain_domain,
                              std::span<const SurfaceElevation> elevations,
                              const FloodField& flood,
                              const LakeCensus& census,
                              const DrainageGraph& drainage,
                              const RiverNetwork& rivers,
                              const WatercoursePaint& parameters) {
    MOPPE_PROFILE_ZONE ("paint_watercourses");
    const TerrainDomain& grid = flood.domain ();
    if (terrain_domain != grid)
      throw std::invalid_argument (
        "watercourse terrain does not match hydrology");
    const int width = static_cast<int> (grid.width ());
    const int height = static_cast<int> (grid.height ());
    const std::size_t count = grid.width () * grid.height ();
    if (census.cell_count () != count || drainage.receiver.size () != count)
      throw std::invalid_argument (
        "watercourse painting inputs do not share a grid");

    // Standing bodies establish the first surface. Running alignments are
    // painted into this same field below; the renderer's exact cell clipper
    // turns the signed field into geometry without reach seams or dry-corner
    // flaps.
    const ElevationMap permanent =
      permanent_water_surface (flood, census, parameters.permanence);
    const auto& permanent_elevations =
      spatial::get<surface_elevation> (permanent);
    std::vector<float> surface;
    surface.reserve (count);
    for (SurfaceElevation elevation : permanent_elevations)
      surface.push_back (surface_elevation_value (elevation));
    std::vector<float> amplitude (count);
    for (std::size_t cell = 0; cell < count; ++cell)
      amplitude[cell] = wave_factor (flood, census, cell);

    std::vector<float> flow_x (count, 0.0f);
    std::vector<float> flow_z (count, 0.0f);
    std::vector<float> flow_weight (count, 0.0f);

    struct RunningPoint {
      float x_m;
      float z_m;
      float level_m;
      float half_width_m;
      float rapid;
      float waterfall;
      float source_profile;
      float standing_water;
    };

    const float spacing_x = (grid.spacing_x ()).numerical_value_in (u::m);
    const float spacing_z = (grid.spacing_z ()).numerical_value_in (u::m);
    const auto sample_ground = [&] (float world_x, float world_z) {
      float gx = world_x / spacing_x;
      float gz = world_z / spacing_z;
      gx -= std::floor (gx / width) * width;
      gz -= std::floor (gz / height) * height;
      const int x0 = static_cast<int> (std::floor (gx));
      const int z0 = static_cast<int> (std::floor (gz));
      const int x1 = wrap_index (x0 + 1, width);
      const int z1 = wrap_index (z0 + 1, height);
      const float tx = gx - x0;
      const float tz = gz - z0;
      const float h00 = elevation_at (grid, elevations, x0, z0);
      const float h10 = elevation_at (grid, elevations, x1, z0);
      const float h01 = elevation_at (grid, elevations, x0, z1);
      const float h11 = elevation_at (grid, elevations, x1, z1);
      return std::lerp (std::lerp (h00, h10, tx), std::lerp (h01, h11, tx), tz);
    };

    std::vector<std::vector<RiverReachId>> upstream_reaches (
      rivers.reaches.size ());
    for (const RiverReach& reach : rivers.reaches)
      if (reach.downstream_reach != RiverReach::no_id)
        upstream_reaches[reach.downstream_reach].push_back (reach.id);

    std::vector<std::vector<RunningPoint>> reach_points (
      rivers.reaches.size ());
    {
      MOPPE_PROFILE_ZONE ("watercourse.prepare_running_surface");
      for (const RiverReach& reach : rivers.reaches) {
        const auto& points = reach.alignment.points;
        auto& prepared = reach_points[reach.id];
        prepared.reserve (points.size ());
        const bool fade_source =
          upstream_reaches[reach.id].empty () &&
          (points.empty () || points.front ().standing_water < 0.99f);
        const float source_width =
          points.empty ()
            ? 0.0f
            : river_width (points.front ().contributing_area_m2 * u::m * u::m)
                .numerical_value_in (u::m);
        const float source_length = std::max (14.0f, 3.0f * source_width);
        for (std::size_t point = 0; point < points.size (); ++point) {
          const RiverAlignmentPoint& center = points[point];
          const RiverAlignmentPoint& before = points[point ? point - 1 : point];
          const RiverAlignmentPoint& after =
            points[point + 1 < points.size () ? point + 1 : point];
          const float run_x = after.x_m - before.x_m;
          const float run_z = after.z_m - before.z_m;
          const float run = std::hypot (run_x, run_z);
          const float tangent_x = run > 1e-5f ? run_x / run : 0.0f;
          const float tangent_z = run > 1e-5f ? run_z / run : 1.0f;
          const float across_x = -tangent_z;
          const float across_z = tangent_x;
          const auto area = center.contributing_area_m2 * u::m * u::m;
          const float width_m = river_width (area).numerical_value_in (u::m);
          const float fill_depth = parameters.channel_fill *
                                   river_depth (area).numerical_value_in (u::m);
          const float ground = sample_ground (center.x_m, center.z_m);
          const float bank_distance = 0.5f * width_m + 0.75f;
          const float bank_limit =
            std::min (sample_ground (center.x_m - across_x * bank_distance,
                                     center.z_m - across_z * bank_distance),
                      sample_ground (center.x_m + across_x * bank_distance,
                                     center.z_m + across_z * bank_distance)) -
            0.08f;
          const float running_level = std::max (
            ground + 0.035f, std::min (ground + fill_depth, bank_limit));
          const float standing = smoothstep (0.0f, 1.0f, center.standing_water);
          float level =
            std::lerp (running_level, center.water_level_m + 0.045f, standing);
          if (center.pooled > 0.25f)
            level = std::max (level, center.water_level_m + 0.045f);
          const float source_profile =
            fade_source ? smoothstep (0.0f, source_length, center.distance_m)
                        : 1.0f;
          level = ground + 0.01f + source_profile * (level - ground - 0.01f);
          prepared.push_back ({ .x_m = center.x_m,
                                .z_m = center.z_m,
                                .level_m = level,
                                .half_width_m = 0.5f * width_m * source_profile,
                                .rapid = rapid_signal (center.slope),
                                .waterfall = center.waterfall,
                                .source_profile = source_profile,
                                .standing_water = center.standing_water });
        }
        // The running surface is a monotone downstream profile. Raise the
        // upstream sample over a shoulder; never lower downstream water into
        // the terrain to manufacture that monotonicity.
        for (std::size_t point = prepared.size (); point > 1; --point)
          prepared[point - 2].level_m =
            std::max (prepared[point - 2].level_m, prepared[point - 1].level_m);
      }
    }

    // A confluence owns one level. Tributaries and the trunk meet at the
    // highest of their independently sampled sections, avoiding a crack
    // without assigning a junction polygon to any reach.
    for (const RiverReach& downstream : rivers.reaches) {
      if (reach_points[downstream.id].empty () ||
          upstream_reaches[downstream.id].empty ())
        continue;
      float junction_level = reach_points[downstream.id].front ().level_m;
      for (RiverReachId upstream : upstream_reaches[downstream.id])
        if (!reach_points[upstream].empty ())
          junction_level =
            std::max (junction_level, reach_points[upstream].back ().level_m);
      reach_points[downstream.id].front ().level_m = junction_level;
      for (RiverReachId upstream : upstream_reaches[downstream.id])
        if (!reach_points[upstream].empty ())
          reach_points[upstream].back ().level_m = junction_level;
    }

    std::vector<float> running_score (count,
                                      std::numeric_limits<float>::infinity ());
    std::vector<float> running_level (count, 0.0f);
    {
      MOPPE_PROFILE_ZONE ("watercourse.paint_running_surface");
      const float margin = parameters.bank_margin.numerical_value_in (u::m);
      const float depth_limit =
        parameters.depth_limit.numerical_value_in (u::m);
      for (const RiverReach& reach : rivers.reaches) {
        const auto& points = reach_points[reach.id];
        for (std::size_t point = 0; point + 1 < points.size (); ++point) {
          const RunningPoint& start = points[point];
          const RunningPoint& end = points[point + 1];
          const float run_x = end.x_m - start.x_m;
          const float run_z = end.z_m - start.z_m;
          const float run2 = run_x * run_x + run_z * run_z;
          if (run2 < 1e-6f)
            continue;
          const float run = std::sqrt (run2);
          const float direction_x = run_x / run;
          const float direction_z = run_z / run;
          const bool falling = std::max (start.waterfall, end.waterfall) > 0.5f;
          const float radius =
            std::max (start.half_width_m + margin * start.source_profile,
                      end.half_width_m + margin * end.source_profile);
          if (radius <= 0.01f)
            continue;
          const int minimum_x = static_cast<int> (
            std::floor ((std::min (start.x_m, end.x_m) - radius) / spacing_x));
          const int maximum_x = static_cast<int> (
            std::ceil ((std::max (start.x_m, end.x_m) + radius) / spacing_x));
          const int minimum_z = static_cast<int> (
            std::floor ((std::min (start.z_m, end.z_m) - radius) / spacing_z));
          const int maximum_z = static_cast<int> (
            std::ceil ((std::max (start.z_m, end.z_m) + radius) / spacing_z));
          for (int unwrapped_z = minimum_z; unwrapped_z <= maximum_z;
               ++unwrapped_z)
            for (int unwrapped_x = minimum_x; unwrapped_x <= maximum_x;
                 ++unwrapped_x) {
              const float world_x = unwrapped_x * spacing_x;
              const float world_z = unwrapped_z * spacing_z;
              const float t = std::clamp (((world_x - start.x_m) * run_x +
                                           (world_z - start.z_m) * run_z) /
                                            run2,
                                          0.0f,
                                          1.0f);
              const float center_x = std::lerp (start.x_m, end.x_m, t);
              const float center_z = std::lerp (start.z_m, end.z_m, t);
              const float distance =
                std::hypot (world_x - center_x, world_z - center_z);
              const float half_width =
                std::lerp (start.half_width_m, end.half_width_m, t);
              const float profile =
                std::lerp (start.source_profile, end.source_profile, t);
              const float local_radius = half_width + margin * profile;
              if (distance >= local_radius || local_radius <= 1e-4f)
                continue;
              const int x = wrap_index (unwrapped_x, width);
              const int z = wrap_index (unwrapped_z, height);
              const std::size_t cell = static_cast<std::size_t> (z) * width + x;
              const float weight =
                std::pow (1.0f - distance / local_radius, 2.0f);
              const float speed =
                (parameters.base_speed +
                 parameters.rapid_speed *
                   std::lerp (start.rapid, end.rapid, t) +
                 parameters.waterfall_speed *
                   std::lerp (start.waterfall, end.waterfall, t))
                  .numerical_value_in (u::m / u::s);
              flow_x[cell] += weight * direction_x * speed;
              flow_z[cell] += weight * direction_z * speed;
              flow_weight[cell] += weight;

              const float standing =
                std::lerp (start.standing_water, end.standing_water, t);
              // A height field cannot own the vertical part of a waterfall.
              // Keep its current for the adjoining lip and plunge pool, but
              // leave the falling span to the explicit nickpoint curtain.
              if (falling || standing >= 0.99f)
                continue;
              const float ground = elevation_at (grid, elevations, x, z);
              const float candidate =
                std::min (std::lerp (start.level_m, end.level_m, t),
                          ground + depth_limit);
              const float score = distance / local_radius;
              if (candidate > ground + 0.005f && score < running_score[cell]) {
                running_score[cell] = score;
                running_level[cell] = candidate;
              }
            }
        }
      }
    }

    for (std::size_t cell = 0; cell < count; ++cell) {
      if (std::isfinite (running_score[cell])) {
        surface[cell] = std::max (surface[cell], running_level[cell]);
        amplitude[cell] = 0.0f;
      }
    }

    std::vector<float> flow (2 * count, 0.0f);
    for (std::size_t cell = 0; cell < count; ++cell)
      if (flow_weight[cell] > 0.0f &&
          surface[cell] - surface_elevation_value (elevations[cell]) > 0.005f) {
        flow[2 * cell] = flow_x[cell] / flow_weight[cell];
        flow[2 * cell + 1] = flow_z[cell] / flow_weight[cell];
      }

    // Sign the still-water shoreline. A dry cell beside water stores a
    // neighboring level just below its ground so bilinear consumers find a
    // true sub-cell zero crossing instead of a cell-edge staircase. It
    // stores the LOWEST wet neighbor: on an ordinary shore every neighbor
    // shares one body level, while on a sill between terraced bodies the
    // higher plate then ends at its own waterline instead of extending
    // across the sill and hanging in the air over the lower body.
    {
      constexpr float dry_margin = 1e-6f;
      std::vector<float> signed_surface = surface;
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const std::size_t cell = static_cast<std::size_t> (y) * width + x;
          const float ground = elevation_at (grid, elevations, x, y);
          if (surface[cell] - ground > dry_margin)
            continue;
          float lowest_wet = std::numeric_limits<float>::infinity ();
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              if (dx == 0 && dy == 0)
                continue;
              const int nx = wrap_index (x + dx, width);
              const int ny = wrap_index (y + dy, height);
              const std::size_t neighbor =
                static_cast<std::size_t> (ny) * width + nx;
              if (surface[neighbor] - elevation_at (grid, elevations, nx, ny) >
                  dry_margin)
                lowest_wet = std::min (lowest_wet, surface[neighbor]);
            }
          if (std::isfinite (lowest_wet))
            signed_surface[cell] = std::min (lowest_wet, ground - dry_margin);
        }
      surface = std::move (signed_surface);
    }

    WaterSheets sheets (grid);
    auto& sheet_elevations = spatial::get<surface_elevation> (sheets);
    auto& amplitudes = spatial::get<wave_amplitude> (sheets);
    auto& velocities = spatial::get<water_velocity> (sheets);
    for (std::size_t cell = 0; cell < count; ++cell) {
      sheet_elevations[cell] =
        SurfaceElevation (surface[cell] * surface_elevation[u::m]);
      amplitudes[cell] = amplitude[cell] * wave_amplitude[one];
      velocities[cell] = Vec3 (flow[2 * cell], 0.0f, flow[2 * cell + 1]) *
                         water_velocity[u::m / u::s];
    }
    return sheets;
  }
}
