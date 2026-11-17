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
#include <moppe/terrain/torus.hh>
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
    const meters_t across =
      (argc > 4 ? std::stof (argv[4]) * 1000.0f : 5000.0f) * u::m;
    const std::string variant = argc > 5 ? argv[5] : "beds";
    if (variant != "beds" && variant != "bare")
      throw std::invalid_argument ("the variant is beds or bare");
    const WorldRecipe recipe = make_world_recipe (
      spatial_extent_in_metres (
        Vec3 (meters_value (across), 320.0f, meters_value (across))),
      resolution,
      Seed { seed },
      50.0f * u::m,
      TerrainGenerationProfile::Play);
    const TerrainDomain domain (static_cast<std::size_t> (resolution),
                                static_cast<std::size_t> (resolution),
                                recipe.extent ());
    map::SurfaceGeometry surface (domain);
    const std::string world =
      std::to_string (resolution) + "-" + std::to_string (seed) + "-" +
      std::to_string (int (meters_value (across) / 1000.0f));
    std::filesystem::create_directories (work);
    const std::filesystem::path evolved =
      work / ("evolved-" + world + ".arrows");
    if (!game::try_load_land (surface, evolved.string ())) {
      const auto uplift = map::initialize_terrain (
        surface, recipe.seed (), recipe.water_datum ());
      map::evolve_terrain (surface, uplift, recipe.evolution ());
      game::save_land (surface, evolved.string ());
    }
    std::vector<ElevationChange> change;
    if (variant == "beds")
      change = map::form_river_channels (surface, recipe.water_datum ()).change;
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
    const meters_t spacing = domain.spacing_x ();
    // A river point's place and how far down its reach it lies.
    const auto place_of = [] (const RiverAlignmentPoint& point) {
      return point.place;
    };
    const auto distance_of = [] (const RiverAlignmentPoint& point) {
      return point.distance;
    };
    const auto cell_at = [&] (plan_position_t at) {
      const int x = wrap_index (
        int (std::lround (static_cast<float> (x_of (at) / spacing))), width);
      const int z = wrap_index (
        int (std::lround (static_cast<float> (z_of (at) / spacing))), width);
      return std::size_t (z) * width + x;
    };
    // A height at a place, bilinear between the samples AT (x, z) gives in
    // metres (the sheets' raster lanes).
    const auto bilinear = [&] (plan_position_t place, const auto& at) {
      const float gx = wrap_coordinate (
        static_cast<float> (x_of (place) / spacing), float (width));
      const float gz = wrap_coordinate (
        static_cast<float> (z_of (place) / spacing), float (width));
      const int x0 = int (std::floor (gx));
      const int z0 = int (std::floor (gz));
      const int x1 = wrap_index (x0 + 1, width);
      const int z1 = wrap_index (z0 + 1, width);
      const float tx = gx - x0;
      const float tz = gz - z0;
      return std::lerp (std::lerp (at (x0, z0), at (x1, z0), tx),
                        std::lerp (at (x0, z1), at (x1, z1), tx),
                        tz) *
             u::m;
    };
    // The water over the ground at a place, bilinear, as the renderer clips
    // it.
    const auto depth_at = [&] (plan_position_t place) -> ElevationChange {
      return bilinear (place, [&] (int x, int z) {
        const std::size_t cell = std::size_t (z) * width + x;
        return surface_elevation_value (water[cell]) -
               surface_elevation_value (ground[cell]);
      });
    };
    // The unit way from BEFORE to AFTER, the chord at least MINIMUM long,
    // measured by std::hypot as the census always has.
    const auto way_between =
      [] (plan_position_t before, plan_position_t after, meters_t minimum) {
        const Vec2 chord = plan_value (after - before);
        const float run = std::max (std::hypot (chord.x (), chord.z ()),
                                    meters_value (minimum));
        return plan_unit_direction (Vec2 (chord.x () / run, chord.z () / run));
      };
    // OUT to the right of WAY (x east, z south): (-z, x) times OUT.
    const auto aside = [] (plan_direction_t way, meters_t out) {
      const Vec2& t = plan_value (way);
      const float metres = meters_value (out);
      return plan_displacement (Vec2 (-t.z () * metres, t.x () * metres));
    };
    // The deepest water within two metres across the river at a point.
    const auto deepest_across =
      [&] (const std::vector<RiverAlignmentPoint>& points, std::size_t point) {
        const auto& here = points[point];
        const auto& before = points[point ? point - 1 : point];
        const auto& after = points[std::min (point + 1, points.size () - 1)];
        const plan_direction_t way =
          way_between (place_of (before), place_of (after), 1e-4f * u::m);
        ElevationChange deepest =
          -std::numeric_limits<float>::infinity () * u::m;
        for (meters_t across = -2.0f * u::m; across <= 2.0f * u::m;
             across += 0.5f * u::m)
          deepest = std::max (deepest,
                              depth_at (place_of (here) + aside (way, across)));
        return deepest;
      };

    // Every reach: its length, its dry gaps, and how deep the rest runs. A
    // fall is its curtain's, and a spring's first metres fade in, so
    // neither counts.
    meters_f64_t total = 0.0 * u::m;
    meters_f64_t dry = 0.0 * u::m;
    std::vector<meters_t> gaps;
    constexpr std::array<ElevationChange, 6> depth_edges {
      0.0f * u::m,  0.02f * u::m, 0.05f * u::m,
      0.10f * u::m, 0.20f * u::m, 0.50f * u::m
    };
    std::array<meters_f64_t, depth_edges.size () + 1> depth_bins {};
    std::vector<std::vector<std::uint8_t>> reach_wet (rivers.reaches.size ());
    for (const RiverReach& reach : rivers.reaches) {
      const auto& points = reach.alignment.points;
      auto& wet = reach_wet[reach.id];
      wet.assign (points.size (), 1);
      const meters_t none = 0.0f * u::m;
      meters_t gap = none;
      for (std::size_t point = 0; point < points.size (); ++point) {
        const meters_t step =
          point + 1 < points.size ()
            ? distance_of (points[point + 1]) - distance_of (points[point])
            : none;
        total += step;
        if (points[point].waterfall >= 0.5f * proportion[one] ||
            distance_of (points[point]) <= 20.0f * u::m)
          continue;
        const ElevationChange deepest = deepest_across (points, point);
        std::size_t bin = 0;
        while (bin < depth_edges.size () && deepest > depth_edges[bin])
          ++bin;
        depth_bins[bin] += step;
        wet[point] = deepest > ElevationChange::zero ();
        if (!wet[point]) {
          dry += step;
          gap += step;
        } else if (gap > none) {
          gaps.push_back (std::exchange (gap, none));
        }
      }
      if (gap > none)
        gaps.push_back (gap);
    }
    std::ranges::sort (gaps, std::greater<> ());
    const auto gaps_over = [&] (meters_t length) {
      return std::ranges::count_if (gaps,
                                    [&] (meters_t g) { return g > length; });
    };
    // Kilometres for the report, as metres over a thousand.
    const auto km = [] (meters_f64_t length) {
      return length.numerical_value_in (u::m) / 1000.0;
    };
    std::printf ("%s: %zu reaches, %.1f km of river, %.2f km dry (%.1f%%)\n",
                 variant.c_str (),
                 rivers.reaches.size (),
                 km (total),
                 km (dry),
                 100.0 * dry.numerical_value_in (u::m) /
                   total.numerical_value_in (u::m));
    std::printf ("dry gaps: %zu; over 5 m %td, 10 m %td, 25 m %td; longest",
                 gaps.size (),
                 gaps_over (5.0f * u::m),
                 gaps_over (10.0f * u::m),
                 gaps_over (25.0f * u::m));
    for (std::size_t gap = 0; gap < std::min<std::size_t> (gaps.size (), 8);
         ++gap)
      std::printf (" %.0f", meters_value (gaps[gap]));
    std::printf (" m\n");
    std::printf ("deepest across, km: dry %.2f, <2 cm %.2f, <5 cm %.2f, "
                 "<10 cm %.2f, <20 cm %.2f, <50 cm %.2f, deeper %.2f\n",
                 km (depth_bins[0]),
                 km (depth_bins[1]),
                 km (depth_bins[2]),
                 km (depth_bins[3]),
                 km (depth_bins[4]),
                 km (depth_bins[5]),
                 km (depth_bins[6]));

    // The banks: from the water's edge on either side, how steeply the
    // ground rises over the next three metres, by how steeply the river
    // falls. A bank under one in eight is a strand to walk along; over one
    // in three, a cut bank.
    {
      const auto ground_at = [&] (plan_position_t place) {
        return surface_elevation_point (bilinear (place, [&] (int x, int z) {
          return surface_elevation_value (ground[std::size_t (z) * width + x]);
        }));
      };
      // Gentle, middling, and steep banks along rivers falling under one
      // in a hundred, one to three, and more.
      std::array<std::array<meters_f64_t, 3>, 3> banks {};
      const auto grade_of = [] (ElevationChange rise, meters_t run) {
        return quantity_cast<grade> (quantity_cast<isq::height> (rise) / run);
      };
      for (const RiverReach& reach : rivers.reaches) {
        const auto& points = reach.alignment.points;
        for (std::size_t point = 8; point + 8 < points.size (); point += 2) {
          const auto& here = points[point];
          if (here.waterfall >= 0.5f * proportion[one] ||
              here.standing_water > 0.0f * proportion[one] ||
              here.pooled > 0.0f * proportion[one] ||
              !reach_wet[reach.id][point])
            continue;
          const auto& above = points[point - 8];
          const auto& below = points[point + 8];
          const meters_t run = distance_of (below) - distance_of (above);
          const grade_t fall = grade_of (ground_at (place_of (above)) -
                                           ground_at (place_of (below)),
                                         std::max (run, 1.0f * u::m));
          const std::size_t kind = fall < 0.01f * grade[one]   ? 0
                                   : fall < 0.03f * grade[one] ? 1
                                                               : 2;
          const plan_direction_t way =
            way_between (place_of (points[point - 2]),
                         place_of (points[point + 2]),
                         1e-3f * u::m);
          const meters_t step =
            distance_of (points[point + 2]) - distance_of (here);
          for (const float side : { -1.0f, 1.0f }) {
            // Out from the bank on this side, toward the water's edge.
            const auto out = [&] (meters_t distance) {
              return place_of (here) + aside (way, side * distance);
            };
            meters_t edge = 0.0f * u::m;
            while (edge < 40.0f * u::m &&
                   depth_at (out (edge)) > ElevationChange::zero ())
              edge += 0.5f * u::m;
            const grade_t slope = grade_of (
              ground_at (out (edge + 3.0f * u::m)) - ground_at (out (edge)),
              3.0f * u::m);
            banks[kind][slope < 0.125f * grade[one]        ? 0
                        : slope < 1.0f / 3.0f * grade[one] ? 1
                                                           : 2] += 0.5 * step;
          }
        }
      }
      const char* names[] = { "under 1%", "1-3%", "over 3%" };
      for (std::size_t kind = 0; kind < 3; ++kind) {
        const double all = (banks[kind][0] + banks[kind][1] + banks[kind][2])
                             .numerical_value_in (u::m);
        const auto share = [&] (std::size_t bank) {
          return 100.0 * banks[kind][bank].numerical_value_in (u::m) /
                 std::max (all, 1.0);
        };
        std::printf ("banks where rivers fall %s: %.1f km, %.0f%% strand, "
                     "%.0f%% middling, %.0f%% cut\n",
                     names[kind],
                     all / 1000.0,
                     share (0),
                     share (1),
                     share (2));
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
      meters_f64_t along = 0.0 * u::m;
      meters_f64_t dry_along = 0.0 * u::m;
      meters_f64_t gap_start = 0.0 * u::m;
      bool was_wet = true;
      for (const RiverReachId id : path) {
        const auto& points = rivers.reaches[id].alignment.points;
        for (std::size_t point = 0; point + 1 < points.size (); ++point) {
          const bool wet = reach_wet[id][point];
          if (!wet && was_wet)
            gap_start = along;
          if (wet && !was_wet)
            std::printf ("  dry from %.0f m to %.0f m, at %.0f %.0f\n",
                         gap_start.numerical_value_in (u::m),
                         along.numerical_value_in (u::m),
                         meters_value (x_of (place_of (points[point]))),
                         meters_value (z_of (place_of (points[point]))));
          was_wet = wet;
          const meters_t step =
            distance_of (points[point + 1]) - distance_of (points[point]);
          along += step;
          if (!wet)
            dry_along += step;
        }
      }
      std::printf ("greatest river: %zu reaches, %.0f m, %.0f m dry\n",
                   path.size (),
                   along.numerical_value_in (u::m),
                   dry_along.numerical_value_in (u::m));
    }

    // Views down the greatest river, on the bank at eye height and from a
    // little height: at its source, at a confluence half way, at its mouth.
    {
      const auto ground_on = [&] (plan_position_t at) {
        return ground[cell_at (at)];
      };
      std::ofstream views (work / (variant + "-" + world + "-views.txt"));
      views << "world seed " << seed << " resolution " << resolution
            << " profile play\n";
      const auto view = [&] (const std::string& name,
                             const std::vector<RiverAlignmentPoint>& points,
                             std::size_t at) {
        const plan_position_t here = place_of (points[at]);
        const auto& before = points[at >= 4 ? at - 4 : 0];
        const auto& after = points[std::min (at + 4, points.size () - 1)];
        const plan_direction_t way =
          way_between (place_of (before), place_of (after), 1e-3f * u::m);
        const Vec2& t = plan_value (way);
        // Downstream, clockwise from north, in the shot list's degrees.
        const quantity<heading[si::degree], float> heading =
          std::atan2 (t.x (), t.z ()) * 57.29578f * moppe::heading[si::degree];
        const plan_position_t bank =
          here - along (way, 12.0f * u::m) + aside (way, 8.0f * u::m);
        const plan_position_t high = here - along (way, 45.0f * u::m);
        const SurfaceElevation river = ground_on (here);
        const auto eye_height = [&] (plan_position_t at, meters_t over) {
          return surface_elevation_value (std::max (ground_on (at), river) +
                                          over);
        };
        char line[320];
        std::snprintf (
          line,
          sizeof line,
          "shot %s-bank eye %.2f %.2f %.2f heading %.1f pitch -10 fov 60 "
          "hold 3\nshot %s-high eye %.2f %.2f %.2f heading %.1f pitch -20 "
          "fov 60 hold 3\n",
          name.c_str (),
          meters_value (x_of (bank)),
          eye_height (bank, 1.7f * u::m),
          meters_value (z_of (bank)),
          (heading + 25.0f * moppe::heading[si::degree])
            .numerical_value_in (si::degree),
          name.c_str (),
          meters_value (x_of (high)),
          eye_height (high, 22.0f * u::m),
          meters_value (z_of (high)),
          heading.numerical_value_in (si::degree));
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
          const plan_position_t place =
            plan_position (std::stof (spot.substr (0, comma)),
                           std::stof (spot.substr (comma + 1)));
          const RiverReach* nearest = nullptr;
          std::size_t at = 0;
          meters_t best = std::numeric_limits<float>::infinity () * u::m;
          for (const RiverReach& reach : rivers.reaches)
            for (std::size_t point = 0; point < reach.alignment.points.size ();
                 ++point) {
              const Vec2 off =
                plan_value (place_of (reach.alignment.points[point]) - place);
              const meters_t d = std::hypot (off.x (), off.z ()) * u::m;
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
        const SurfaceElevation h = ground[cell];
        const SurfaceElevation east =
          ground[std::size_t (z) * width + wrap_index (x + 1, width)];
        const SurfaceElevation south =
          ground[std::size_t (wrap_index (z + 1, width)) * width + x];
        const ElevationChange relief = (h - east) + (h - south);
        // Brightness, lit from the south-east, and grey by height: the
        // picture's bytes.
        const float shade = std::clamp (
          0.6f + 0.25f * relief.numerical_value_in (u::m), 0.1f, 1.0f);
        const float grey =
          std::clamp (
            60.0f + surface_elevation_value (h) * 0.5f, 0.0f, 255.0f) *
          shade;
        const bool wet = water[cell] > h;
        std::uint8_t* rgb = &image[cell * 3];
        rgb[0] = wet ? 40 : std::uint8_t (grey);
        rgb[1] = wet ? 90 : std::uint8_t (grey);
        rgb[2] = wet ? 220 : std::uint8_t (grey);
      }
    for (const RiverReach& reach : rivers.reaches) {
      const auto& points = reach.alignment.points;
      for (std::size_t point = 0; point < points.size (); ++point)
        if (!reach_wet[reach.id][point]) {
          std::uint8_t* rgb = &image[cell_at (place_of (points[point])) * 3];
          rgb[0] = 255;
          rgb[1] = 30;
          rgb[2] = 30;
        }
    }
    // What the beds moved: cut red, filled blue, a metre to full colour.
    if (!change.empty ()) {
      std::vector<std::uint8_t> moved (change.size () * 3);
      for (std::size_t cell = 0; cell < change.size (); ++cell) {
        const float c =
          std::clamp (change[cell].numerical_value_in (u::m), -1.0f, 1.0f);
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
