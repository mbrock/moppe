// Walks every river of a world and says where its water stands and where it
// does not: how much of the length runs dry, in how many gaps, how deep the
// rest is, how the reaches begin and end, and the gaps along the greatest
// river from its source to the sea. The water is read as the renderer reads
// it, bilinearly between the sheet's samples, so a stream that pinches shut
// between two cells counts as dry there.
//
//   river-census WORK_DIRECTORY [RESOLUTION [SEED [KM [beds|bare]]]]
//   (default 2048 123 5 beds)
//
// The evolved land, before its rivers' beds or trails, is kept in the work
// directory, so a second run skips the geology. "bare" lays the trails
// without giving the rivers their beds, for comparison. The work directory
// also gets a map (VARIANT-...ppm: water blue, dry river red), a map of what
// the beds moved (...-moved.ppm: cut red, filled blue), and an opening
// shot list (VARIANT-KM-views.txt) that looks down the greatest river at its
// source, a confluence half way, and its mouth, and at the river nearest
// each place RC_SPOTS names ("x,z;x,z"), for MOPPE_OPENING.

#include <moppe/game/generated_world.hh>
#include <moppe/game/land.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/watercourse.hh>
#include <moppe/terrain/world_recipe.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    if (argc < 2)
      throw std::invalid_argument ("usage: river-census WORK_DIRECTORY "
                                   "[RESOLUTION [SEED [KM [beds|bare]]]]");
    const std::filesystem::path work = argv[1];
    const int resolution = argc > 2 ? std::stoi (argv[2]) : 2048;
    const std::uint32_t seed =
      argc > 3 ? static_cast<std::uint32_t> (std::stoul (argv[3])) : 123;
    const float across_m = argc > 4 ? std::stof (argv[4]) * 1000.0f : 5000.0f;
    const std::string variant = argc > 5 ? argv[5] : "beds";
    if (variant != "beds" && variant != "bare")
      throw std::invalid_argument ("the variant is beds or bare");
    const WorldRecipe recipe = make_world_recipe (
      spatial_extent_in_metres (Vec3 (across_m, 320.0f, across_m)),
      resolution,
      Seed { seed },
      50.0f * u::m,
      TerrainGenerationProfile::Play);
    const TerrainDomain domain (static_cast<std::size_t> (resolution),
                                static_cast<std::size_t> (resolution),
                                recipe.extent ());
    map::SurfaceGeometry surface (domain);
    const std::string world = std::to_string (resolution) + "-" +
                              std::to_string (seed) + "-" +
                              std::to_string (int (across_m / 1000.0f));
    std::filesystem::create_directories (work);
    const std::filesystem::path evolved =
      work / ("evolved-" + world + ".arrows");
    if (!game::try_load_land (surface, evolved.string ())) {
      const auto uplift = map::initialize_terrain (
        surface, recipe.seed (), recipe.water_datum ());
      map::evolve_terrain (surface, uplift, recipe.evolution ());
      game::save_land (surface, evolved.string ());
    }
    std::vector<float> change;
    if (variant == "beds")
      change =
        map::form_river_channels (surface, recipe.water_datum ()).change_m;
    (void)map::form_terrain_trails (surface, recipe.trail_formation ());
    game::quantize_land (surface);
    map::rebuild_geometry (surface);
    const game::Hydrology hydrology = game::analyze_hydrology (surface, recipe);
    const auto& [flood, lakes, drainage, rivers] = hydrology;
    const WaterSheets sheets =
      paint_watercourses (surface, flood, lakes, drainage, rivers);

    const auto ground = elevations (surface);
    const auto water = spatial::get<surface_elevation> (sheets);
    const int width = resolution;
    const float spacing = domain.spacing_x ().numerical_value_in (u::m);
    const auto cell_at = [&] (float x_m, float z_m) {
      const int x = wrap_index (int (std::lround (x_m / spacing)), width);
      const int z = wrap_index (int (std::lround (z_m / spacing)), width);
      return std::size_t (z) * width + x;
    };
    // The water over the ground at a place, bilinear, as the renderer clips
    // it.
    const auto depth_at = [&] (float x_m, float z_m) {
      const float gx = wrap_coordinate (x_m / spacing, float (width));
      const float gz = wrap_coordinate (z_m / spacing, float (width));
      const int x0 = int (std::floor (gx));
      const int z0 = int (std::floor (gz));
      const int x1 = wrap_index (x0 + 1, width);
      const int z1 = wrap_index (z0 + 1, width);
      const auto at = [&] (int x, int z) {
        const std::size_t cell = std::size_t (z) * width + x;
        return surface_elevation_value (water[cell]) -
               surface_elevation_value (ground[cell]);
      };
      const float tx = gx - x0;
      const float tz = gz - z0;
      return std::lerp (std::lerp (at (x0, z0), at (x1, z0), tx),
                        std::lerp (at (x0, z1), at (x1, z1), tx),
                        tz);
    };
    // The deepest water within two metres across the river at a point.
    const auto deepest_across =
      [&] (const std::vector<RiverAlignmentPoint>& points, std::size_t point) {
        const auto& here = points[point];
        const auto& before = points[point ? point - 1 : point];
        const auto& after = points[std::min (point + 1, points.size () - 1)];
        float tx = after.x_m - before.x_m;
        float tz = after.z_m - before.z_m;
        const float run = std::max (std::hypot (tx, tz), 1e-4f);
        tx /= run;
        tz /= run;
        float deepest = -std::numeric_limits<float>::infinity ();
        for (float across = -2.0f; across <= 2.0f; across += 0.5f)
          deepest = std::max (
            deepest, depth_at (here.x_m - tz * across, here.z_m + tx * across));
        return deepest;
      };

    // Every reach: its length, its dry gaps, and how deep the rest runs. A
    // fall is its curtain's, and a spring's first metres fade in, so
    // neither counts.
    double total = 0.0;
    double dry = 0.0;
    std::vector<float> gaps;
    constexpr std::array<float, 6> depth_edges { 0.0f,  0.02f, 0.05f,
                                                 0.10f, 0.20f, 0.50f };
    std::array<double, depth_edges.size () + 1> depth_bins {};
    std::vector<std::vector<std::uint8_t>> reach_wet (rivers.reaches.size ());
    for (const RiverReach& reach : rivers.reaches) {
      const auto& points = reach.alignment.points;
      auto& wet = reach_wet[reach.id];
      wet.assign (points.size (), 1);
      float gap = 0.0f;
      for (std::size_t point = 0; point < points.size (); ++point) {
        const float step =
          point + 1 < points.size ()
            ? points[point + 1].distance_m - points[point].distance_m
            : 0.0f;
        total += step;
        if (points[point].waterfall >= 0.5f ||
            points[point].distance_m <= 20.0f)
          continue;
        const float deepest = deepest_across (points, point);
        std::size_t bin = 0;
        while (bin < depth_edges.size () && deepest > depth_edges[bin])
          ++bin;
        depth_bins[bin] += step;
        wet[point] = deepest > 0.0f;
        if (!wet[point]) {
          dry += step;
          gap += step;
        } else if (gap > 0.0f) {
          gaps.push_back (std::exchange (gap, 0.0f));
        }
      }
      if (gap > 0.0f)
        gaps.push_back (gap);
    }
    std::ranges::sort (gaps, std::greater<> ());
    const auto gaps_over = [&] (float length) {
      return std::ranges::count_if (gaps, [&] (float g) { return g > length; });
    };
    std::printf ("%s: %zu reaches, %.1f km of river, %.2f km dry (%.1f%%)\n",
                 variant.c_str (),
                 rivers.reaches.size (),
                 total / 1000.0,
                 dry / 1000.0,
                 100.0 * dry / total);
    std::printf ("dry gaps: %zu; over 5 m %td, 10 m %td, 25 m %td; longest",
                 gaps.size (),
                 gaps_over (5.0f),
                 gaps_over (10.0f),
                 gaps_over (25.0f));
    for (std::size_t gap = 0; gap < std::min<std::size_t> (gaps.size (), 8);
         ++gap)
      std::printf (" %.0f", gaps[gap]);
    std::printf (" m\n");
    std::printf ("deepest across, km: dry %.2f, <2 cm %.2f, <5 cm %.2f, "
                 "<10 cm %.2f, <20 cm %.2f, <50 cm %.2f, deeper %.2f\n",
                 depth_bins[0] / 1000,
                 depth_bins[1] / 1000,
                 depth_bins[2] / 1000,
                 depth_bins[3] / 1000,
                 depth_bins[4] / 1000,
                 depth_bins[5] / 1000,
                 depth_bins[6] / 1000);

    // The banks: from the water's edge on either side, how steeply the
    // ground rises over the next three metres, by how steeply the river
    // falls. A bank under one in eight is a strand to walk along; over one
    // in three, a cut bank.
    {
      const auto ground_at = [&] (float x_m, float z_m) {
        const float gx = wrap_coordinate (x_m / spacing, float (width));
        const float gz = wrap_coordinate (z_m / spacing, float (width));
        const int x0 = int (std::floor (gx));
        const int z0 = int (std::floor (gz));
        const int x1 = wrap_index (x0 + 1, width);
        const int z1 = wrap_index (z0 + 1, width);
        const auto at = [&] (int x, int z) {
          return surface_elevation_value (ground[std::size_t (z) * width + x]);
        };
        const float tx = gx - x0;
        const float tz = gz - z0;
        return std::lerp (std::lerp (at (x0, z0), at (x1, z0), tx),
                          std::lerp (at (x0, z1), at (x1, z1), tx),
                          tz);
      };
      // Gentle, middling, and steep banks along rivers falling under one
      // in a hundred, one to three, and more.
      std::array<std::array<double, 3>, 3> banks {};
      for (const RiverReach& reach : rivers.reaches) {
        const auto& points = reach.alignment.points;
        for (std::size_t point = 8; point + 8 < points.size (); point += 2) {
          const auto& here = points[point];
          if (here.waterfall >= 0.5f || here.standing_water > 0.0f ||
              here.pooled > 0.0f || !reach_wet[reach.id][point])
            continue;
          const auto& above = points[point - 8];
          const auto& below = points[point + 8];
          const float run = below.distance_m - above.distance_m;
          const float fall = (ground_at (above.x_m, above.z_m) -
                              ground_at (below.x_m, below.z_m)) /
                             std::max (run, 1.0f);
          const std::size_t grade = fall < 0.01f ? 0 : fall < 0.03f ? 1 : 2;
          float tx = points[point + 2].x_m - points[point - 2].x_m;
          float tz = points[point + 2].z_m - points[point - 2].z_m;
          const float length = std::max (std::hypot (tx, tz), 1e-3f);
          tx /= length;
          tz /= length;
          const float step =
            points[point + 2].distance_m - points[point].distance_m;
          for (const float side : { -1.0f, 1.0f }) {
            float edge = 0.0f;
            while (edge < 40.0f &&
                   depth_at (here.x_m - side * tz * edge,
                             here.z_m + side * tx * edge) > 0.0f)
              edge += 0.5f;
            const auto rise = [&] (float out) {
              return ground_at (here.x_m - side * tz * out,
                                here.z_m + side * tx * out);
            };
            const float slope = (rise (edge + 3.0f) - rise (edge)) / 3.0f;
            banks[grade][slope < 0.125f        ? 0
                         : slope < 1.0f / 3.0f ? 1
                                               : 2] += 0.5 * step;
          }
        }
      }
      const char* names[] = { "under 1%", "1-3%", "over 3%" };
      for (std::size_t grade = 0; grade < 3; ++grade) {
        const double all = banks[grade][0] + banks[grade][1] + banks[grade][2];
        std::printf ("banks where rivers fall %s: %.1f km, %.0f%% strand, "
                     "%.0f%% middling, %.0f%% cut\n",
                     names[grade],
                     all / 1000.0,
                     100.0 * banks[grade][0] / std::max (all, 1.0),
                     100.0 * banks[grade][1] / std::max (all, 1.0),
                     100.0 * banks[grade][2] / std::max (all, 1.0));
      }
    }

    // How the reaches begin and end.
    const auto into_sea = [&] (const RiverReach& reach) {
      return reach.downstream_ocean ||
             (reach.downstream_body != no_water_body &&
              lakes.water_body (reach.downstream_body).classification ==
                WaterBodyClass::Sea);
    };
    {
      std::array<std::size_t, 4> ends {};
      std::vector<std::uint8_t> fed (rivers.reaches.size (), 0);
      for (const RiverReach& reach : rivers.reaches)
        if (reach.downstream_reach != RiverReach::no_id) {
          ++ends[0];
          fed[reach.downstream_reach] = 1;
        } else
          ++ends[into_sea (reach)                         ? 1
                 : reach.downstream_body != no_water_body ? 2
                                                          : 3];
      std::array<std::size_t, 3> starts {};
      for (const RiverReach& reach : rivers.reaches)
        ++starts[fed[reach.id]                          ? 0
                 : reach.upstream_body != no_water_body ? 1
                                                        : 2];
      std::printf ("reaches end: %zu in another, %zu in the sea, %zu in a "
                   "lake, %zu nowhere\n",
                   ends[0],
                   ends[1],
                   ends[2],
                   ends[3]);
      std::printf ("reaches begin: %zu fed, %zu at a lake, %zu at a source\n",
                   starts[0],
                   starts[1],
                   starts[2]);
    }

    // The greatest river, from its farthest source to its mouth, through
    // the lakes on its way.
    std::vector<std::vector<RiverReachId>> feeders (rivers.reaches.size ());
    std::vector<RiverReachId> outlet (lakes.domain ().size (),
                                      RiverReach::no_id);
    for (const RiverReach& reach : rivers.reaches)
      if (reach.upstream_body != no_water_body)
        outlet[lakes.domain ().offset (reach.upstream_body)] = reach.id;
    for (const RiverReach& reach : rivers.reaches)
      if (reach.downstream_reach != RiverReach::no_id)
        feeders[reach.downstream_reach].push_back (reach.id);
      else if (!into_sea (reach) && reach.downstream_body != no_water_body)
        if (const RiverReachId out =
              outlet[lakes.domain ().offset (reach.downstream_body)];
            out != RiverReach::no_id && out != reach.id)
          feeders[out].push_back (reach.id);
    RiverReachId mouth = RiverReach::no_id;
    for (const RiverReach& reach : rivers.reaches)
      if (into_sea (reach) &&
          (mouth == RiverReach::no_id ||
           reach.downstream_area > rivers.reaches[mouth].downstream_area))
        mouth = reach.id;
    if (mouth == RiverReach::no_id)
      throw std::runtime_error ("no river reaches the sea");
    std::vector<RiverReachId> path { mouth };
    std::vector<std::uint8_t> seen (rivers.reaches.size (), 0);
    while (!feeders[path.back ()].empty () &&
           !std::exchange (seen[path.back ()], 1))
      path.push_back (*std::ranges::max_element (
        feeders[path.back ()], {}, [&] (RiverReachId id) {
          return rivers.reaches[id].downstream_area.numerical_value_in (u::m *
                                                                        u::m);
        }));
    std::ranges::reverse (path);
    {
      double along = 0.0;
      double dry_along = 0.0;
      double gap_start = 0.0;
      bool was_wet = true;
      for (const RiverReachId id : path) {
        const auto& points = rivers.reaches[id].alignment.points;
        for (std::size_t point = 0; point + 1 < points.size (); ++point) {
          const bool wet = reach_wet[id][point];
          if (!wet && was_wet)
            gap_start = along;
          if (wet && !was_wet)
            std::printf ("  dry from %.0f m to %.0f m, at %.0f %.0f\n",
                         gap_start,
                         along,
                         points[point].x_m,
                         points[point].z_m);
          was_wet = wet;
          const float step =
            points[point + 1].distance_m - points[point].distance_m;
          along += step;
          if (!wet)
            dry_along += step;
        }
      }
      std::printf ("greatest river: %zu reaches, %.0f m, %.0f m dry\n",
                   path.size (),
                   along,
                   dry_along);
    }

    // Views down the greatest river, on the bank at eye height and from a
    // little height: at its source, at a confluence half way, at its mouth.
    {
      const auto ground_m = [&] (float x_m, float z_m) {
        return surface_elevation_value (ground[cell_at (x_m, z_m)]);
      };
      std::ofstream views (work / (variant + "-" + world + "-views.txt"));
      views << "world seed " << seed << " resolution " << resolution
            << " profile play\n";
      const auto view = [&] (const std::string& name,
                             const std::vector<RiverAlignmentPoint>& points,
                             std::size_t at) {
        const auto& here = points[at];
        const auto& before = points[at >= 4 ? at - 4 : 0];
        const auto& after = points[std::min (at + 4, points.size () - 1)];
        float tx = after.x_m - before.x_m;
        float tz = after.z_m - before.z_m;
        const float run = std::max (std::hypot (tx, tz), 1e-3f);
        tx /= run;
        tz /= run;
        const float heading = std::atan2 (tx, tz) * 57.29578f;
        const float bank_x = here.x_m - tx * 12.0f - tz * 8.0f;
        const float bank_z = here.z_m - tz * 12.0f + tx * 8.0f;
        const float high_x = here.x_m - tx * 45.0f;
        const float high_z = here.z_m - tz * 45.0f;
        const float river = ground_m (here.x_m, here.z_m);
        char line[320];
        std::snprintf (
          line,
          sizeof line,
          "shot %s-bank eye %.2f %.2f %.2f heading %.1f pitch -10 fov 60 "
          "hold 3\nshot %s-high eye %.2f %.2f %.2f heading %.1f pitch -20 "
          "fov 60 hold 3\n",
          name.c_str (),
          bank_x,
          std::max (ground_m (bank_x, bank_z), river) + 1.7f,
          bank_z,
          heading + 25.0f,
          name.c_str (),
          high_x,
          std::max (ground_m (high_x, high_z), river) + 22.0f,
          high_z,
          heading);
        views << line;
      };
      const auto& source = rivers.reaches[path.front ()].alignment.points;
      view ("source", source, std::min<std::size_t> (source.size () - 1, 40));
      RiverReachId middle = path[path.size () / 2];
      for (std::size_t i = path.size () / 2; i < path.size (); ++i)
        if (feeders[path[i]].size () >= 2) {
          middle = path[i];
          break;
        }
      view ("confluence", rivers.reaches[middle].alignment.points, 0);
      const auto& last = rivers.reaches[path.back ()].alignment.points;
      view ("mouth", last, last.size () > 30 ? last.size () - 30 : 0);
      // And the river nearest each place in RC_SPOTS ("x,z;x,z").
      if (const char* spots = std::getenv ("RC_SPOTS")) {
        const std::string list = spots;
        int index = 0;
        for (std::size_t from = 0; from < list.size ();) {
          const std::size_t to = std::min (list.find (';', from), list.size ());
          const std::string spot = list.substr (from, to - from);
          const std::size_t comma = spot.find (',');
          const float x = std::stof (spot.substr (0, comma));
          const float z = std::stof (spot.substr (comma + 1));
          const RiverReach* nearest = nullptr;
          std::size_t at = 0;
          float best = std::numeric_limits<float>::infinity ();
          for (const RiverReach& reach : rivers.reaches)
            for (std::size_t point = 0; point < reach.alignment.points.size ();
                 ++point) {
              const auto& here = reach.alignment.points[point];
              const float d = std::hypot (here.x_m - x, here.z_m - z);
              if (d < best) {
                best = d;
                nearest = &reach;
                at = point;
              }
            }
          if (nearest)
            view (
              "spot" + std::to_string (index++), nearest->alignment.points, at);
          from = to + 1;
        }
      }
    }

    // The map: shaded ground, water blue, and the rivers red where dry.
    std::vector<std::uint8_t> image (std::size_t (width) * width * 3);
    for (int z = 0; z < width; ++z)
      for (int x = 0; x < width; ++x) {
        const std::size_t cell = std::size_t (z) * width + x;
        const float h = surface_elevation_value (ground[cell]);
        const float east = surface_elevation_value (
          ground[std::size_t (z) * width + wrap_index (x + 1, width)]);
        const float south = surface_elevation_value (
          ground[std::size_t (wrap_index (z + 1, width)) * width + x]);
        const float shade =
          std::clamp (0.6f + 0.25f * ((h - east) + (h - south)), 0.1f, 1.0f);
        const float grey = std::clamp (60.0f + h * 0.5f, 0.0f, 255.0f) * shade;
        const bool wet = surface_elevation_value (water[cell]) > h;
        std::uint8_t* rgb = &image[cell * 3];
        rgb[0] = wet ? 40 : std::uint8_t (grey);
        rgb[1] = wet ? 90 : std::uint8_t (grey);
        rgb[2] = wet ? 220 : std::uint8_t (grey);
      }
    for (const RiverReach& reach : rivers.reaches) {
      const auto& points = reach.alignment.points;
      for (std::size_t point = 0; point < points.size (); ++point)
        if (!reach_wet[reach.id][point]) {
          std::uint8_t* rgb =
            &image[cell_at (points[point].x_m, points[point].z_m) * 3];
          rgb[0] = 255;
          rgb[1] = 30;
          rgb[2] = 30;
        }
    }
    // What the beds moved: cut red, filled blue, a metre to full colour.
    if (!change.empty ()) {
      std::vector<std::uint8_t> moved (change.size () * 3);
      for (std::size_t cell = 0; cell < change.size (); ++cell) {
        const float c = std::clamp (change[cell], -1.0f, 1.0f);
        moved[cell * 3] = std::uint8_t (255.0f * (1.0f - std::max (c, 0.0f)));
        moved[cell * 3 + 1] = std::uint8_t (255.0f * (1.0f - std::abs (c)));
        moved[cell * 3 + 2] =
          std::uint8_t (255.0f * (1.0f + std::min (c, 0.0f)));
      }
      std::ofstream out (work / (variant + "-" + world + "-moved.ppm"),
                         std::ios::binary);
      out << "P6\n" << width << ' ' << width << "\n255\n";
      out.write (reinterpret_cast<const char*> (moved.data ()),
                 std::streamsize (moved.size ()));
    }
    const std::filesystem::path map = work / (variant + "-" + world + ".ppm");
    std::ofstream out (map, std::ios::binary);
    out << "P6\n" << width << ' ' << width << "\n255\n";
    out.write (reinterpret_cast<const char*> (image.data ()),
               std::streamsize (image.size ()));
    std::cerr << "river-census: " << map.string () << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "river-census: " << error.what () << '\n';
    return -1;
  }
}
