#include <moppe/game/relief_map.hh>

#include <moppe/parallel.hh>

#include <algorithm>
#include <cmath>
#include <span>

namespace moppe::game {
  namespace {
    struct Rgb {
      float r = 0.0f, g = 0.0f, b = 0.0f;
    };

    Rgb mix (Rgb a, Rgb b, float t) {
      return { a.r + (b.r - a.r) * t,
               a.g + (b.g - a.g) * t,
               a.b + (b.b - a.b) * t };
    }

    float saturate (float value) {
      return std::clamp (value, 0.0f, 1.0f);
    }

    float smooth (float low, float high, float value) {
      const float t = saturate ((value - low) / (high - low));
      return t * t * (3.0f - 2.0f * t);
    }

    // The sheet's inks.
    constexpr Rgb lowland { 0.955f, 0.930f, 0.800f };
    constexpr Rgb upland { 0.925f, 0.870f, 0.730f };
    constexpr Rgb fell { 0.900f, 0.860f, 0.790f };
    constexpr Rgb summit { 0.965f, 0.960f, 0.945f };
    constexpr Rgb woodland { 0.730f, 0.835f, 0.610f };
    constexpr Rgb high_woodland { 0.790f, 0.850f, 0.660f };
    constexpr Rgb shallows { 0.730f, 0.865f, 0.940f };
    constexpr Rgb deeps { 0.520f, 0.720f, 0.880f };
    constexpr Rgb fresh_water { 0.640f, 0.815f, 0.945f };
    constexpr Rgb shore_ink { 0.180f, 0.400f, 0.660f };
    constexpr Rgb river_ink { 0.215f, 0.470f, 0.760f };
    constexpr Rgb contour_ink { 0.560f, 0.360f, 0.200f };
    constexpr Rgb sounding_ink { 0.330f, 0.540f, 0.760f };
    constexpr Rgb grid_ink { 0.150f, 0.200f, 0.300f };
    constexpr Rgb trail_ink { 0.800f, 0.130f, 0.110f };
    constexpr Rgb road_casing_ink { 0.300f, 0.200f, 0.120f };
    constexpr Rgb road_ink { 0.985f, 0.800f, 0.380f };
    constexpr Rgb footpath_ink { 0.330f, 0.220f, 0.140f };
    // Ways no longer used: grey-violet, apart from the brown contours.
    constexpr Rgb old_way_ink { 0.380f, 0.320f, 0.460f };
    constexpr Rgb sanctuary_ink { 0.560f, 0.120f, 0.160f };
    constexpr Rgb sightline_ink { 0.850f, 0.420f, 0.150f };
    constexpr Rgb shadow_ink { 0.360f, 0.340f, 0.430f };
    constexpr Rgb light_ink { 1.000f, 0.985f, 0.940f };

    // One column of a bundle as plain numbers over the periodic lattice,
    // read between its samples.
    struct Lattice {
      std::span<const float> values;
      std::size_t width = 0;
      std::size_t height = 0;

      float at (long column, long row) const {
        const long w = static_cast<long> (width);
        const long h = static_cast<long> (height);
        column %= w;
        row %= h;
        if (column < 0)
          column += w;
        if (row < 0)
          row += h;
        return values[static_cast<std::size_t> (row) * width +
                      static_cast<std::size_t> (column)];
      }

      float sample (float x, float z) const {
        const float fx = std::floor (x);
        const float fz = std::floor (z);
        const long column = static_cast<long> (fx);
        const long row = static_cast<long> (fz);
        const float tx = x - fx;
        const float tz = z - fz;
        const float north =
          at (column, row) + (at (column + 1, row) - at (column, row)) * tx;
        const float south =
          at (column, row + 1) +
          (at (column + 1, row + 1) - at (column, row + 1)) * tx;
        return north + (south - north) * tz;
      }
    };

    template <typename Quantity>
    std::span<const float> numbers (const std::vector<Quantity>& column) {
      static_assert (sizeof (Quantity) == sizeof (float) &&
                     alignof (Quantity) == alignof (float));
      return { reinterpret_cast<const float*> (column.data ()),
               column.size () };
    }

    float wrap_delta (float delta, float period) {
      if (delta > 0.5f * period)
        delta -= period;
      if (delta < -0.5f * period)
        delta += period;
      return delta;
    }

    // A layer of line work: how much of each pixel its strokes cover.
    struct Ink {
      int size = 0;
      std::vector<std::uint8_t> cover;

      explicit Ink (int size)
          : size (size), cover (static_cast<std::size_t> (size) * size) {}

      float at (int x, int y) const {
        return static_cast<float> (
                 cover[static_cast<std::size_t> (y) * size + x]) /
               255.0f;
      }

      // A stroke from a to b in pixels, tapering between two half widths.
      // With a dash length, `along` is how far down the line a lies, and
      // the stroke shows only on the dashes.
      void stroke (float ax,
                   float ay,
                   float bx,
                   float by,
                   float half_a,
                   float half_b,
                   float along = 0.0f,
                   float dash = 0.0f,
                   float gap = 0.0f) {
        const float reach = std::max (half_a, half_b) + 1.0f;
        const int x0 =
          static_cast<int> (std::floor (std::min (ax, bx) - reach));
        const int x1 = static_cast<int> (std::ceil (std::max (ax, bx) + reach));
        const int y0 =
          static_cast<int> (std::floor (std::min (ay, by) - reach));
        const int y1 = static_cast<int> (std::ceil (std::max (ay, by) + reach));
        const float dx = bx - ax;
        const float dy = by - ay;
        const float length2 = dx * dx + dy * dy;
        const float length = std::sqrt (length2);
        for (int y = y0; y <= y1; ++y)
          for (int x = x0; x <= x1; ++x) {
            const float px = static_cast<float> (x) + 0.5f - ax;
            const float py = static_cast<float> (y) + 0.5f - ay;
            const float t =
              length2 > 0.0f ? saturate ((px * dx + py * dy) / length2) : 0.0f;
            const float ox = px - t * dx;
            const float oy = py - t * dy;
            const float half = half_a + (half_b - half_a) * t;
            float covered =
              saturate (half + 0.5f - std::sqrt (ox * ox + oy * oy));
            if (dash > 0.0f) {
              const float phase = std::fmod (along + t * length, dash + gap);
              covered *= saturate (std::min (phase, dash - phase) + 0.5f);
            }
            if (covered <= 0.0f)
              continue;
            const int wx = ((x % size) + size) % size;
            const int wy = ((y % size) + size) % size;
            std::uint8_t& pixel =
              cover[static_cast<std::size_t> (wy) * size + wx];
            pixel = std::max (
              pixel,
              static_cast<std::uint8_t> (std::lround (covered * 255.0f)));
          }
      }
    };

    // The tops that stand over everything within a few hundred metres,
    // highest first, none crowding another.
    std::vector<ReliefMap::Summit>
    find_summits (const Lattice& ground,
                  const terrain::TerrainDomain& domain,
                  float sea_m) {
      const float spacing_x = domain.spacing_x ().numerical_value_in (u::m);
      const float spacing_z = domain.spacing_z ().numerical_value_in (u::m);
      const float period_x = domain.period_x ().numerical_value_in (u::m);
      const float period_z = domain.period_z ().numerical_value_in (u::m);
      const std::size_t block = std::max<std::size_t> (
        1, static_cast<std::size_t> (std::lround (200.0f / spacing_x)));
      const std::size_t across =
        std::max<std::size_t> (1, ground.width / block);
      const std::size_t down = std::max<std::size_t> (1, ground.height / block);
      struct Top {
        float height = -1e9f;
        std::size_t column = 0, row = 0;
      };
      std::vector<Top> tops (across * down);
      for (std::size_t row = 0; row < down * block && row < ground.height;
           ++row)
        for (std::size_t column = 0;
             column < across * block && column < ground.width;
             ++column) {
          Top& top = tops[(row / block) * across + column / block];
          const float height = ground.values[row * ground.width + column];
          if (height > top.height)
            top = { height, column, row };
        }

      std::vector<ReliefMap::Summit> found;
      const long reach = 2;
      for (std::size_t z = 0; z < down; ++z)
        for (std::size_t x = 0; x < across; ++x) {
          const Top& top = tops[z * across + x];
          if (top.height - sea_m < 30.0f)
            continue;
          bool highest = true;
          for (long dz = -reach; dz <= reach && highest; ++dz)
            for (long dx = -reach; dx <= reach; ++dx) {
              const std::size_t nx = static_cast<std::size_t> (
                (static_cast<long> (x) + dx + static_cast<long> (across)) %
                static_cast<long> (across));
              const std::size_t nz = static_cast<std::size_t> (
                (static_cast<long> (z) + dz + static_cast<long> (down)) %
                static_cast<long> (down));
              if (tops[nz * across + nx].height > top.height) {
                highest = false;
                break;
              }
            }
          if (highest)
            found.push_back ({ static_cast<float> (top.column) * spacing_x,
                               static_cast<float> (top.row) * spacing_z,
                               top.height - sea_m });
        }
      std::sort (found.begin (),
                 found.end (),
                 [] (const ReliefMap::Summit& a, const ReliefMap::Summit& b) {
                   return a.height_m != b.height_m ? a.height_m > b.height_m
                          : a.z_m != b.z_m         ? a.z_m < b.z_m
                                                   : a.x_m < b.x_m;
                 });
      // About a dozen to the ordinary world, and as thick on a wider one.
      const std::size_t limit = static_cast<std::size_t> (
        std::max (3.0f, 12.0f * period_x * period_z / 25.0e6f));
      std::vector<ReliefMap::Summit> kept;
      for (const ReliefMap::Summit& candidate : found) {
        if (kept.size () >= limit)
          break;
        const bool crowded = std::any_of (
          kept.begin (), kept.end (), [&] (const ReliefMap::Summit& other) {
            return std::hypot (
                     wrap_delta (candidate.x_m - other.x_m, period_x),
                     wrap_delta (candidate.z_m - other.z_m, period_z)) < 650.0f;
          });
        if (!crowded)
          kept.push_back (candidate);
      }
      return kept;
    }
  }

  ReliefMap paint_relief_map (const map::SurfaceGeometry& surface,
                              const map::SurfaceReadings& readings,
                              const terrain::WaterSheets& water,
                              const terrain::RiverNetwork& rivers,
                              const terrain::TrailNetwork& trails,
                              const terrain::RoadNetwork& roads,
                              meters_t sea_level,
                              int size) {
    const terrain::TerrainDomain& domain = surface.domain ();
    const std::size_t width = domain.width ();
    const std::size_t height = domain.height ();
    const float spacing_x = domain.spacing_x ().numerical_value_in (u::m);
    const float spacing_z = domain.spacing_z ().numerical_value_in (u::m);
    const float period_x = domain.period_x ().numerical_value_in (u::m);
    const float period_z = domain.period_z ().numerical_value_in (u::m);
    const float sea_m = sea_level.numerical_value_in (u::m);

    const Lattice ground { numbers (spatial::get<terrain::surface_elevation> (
                             surface)),
                           width,
                           height };
    const Lattice level {
      numbers (spatial::get<terrain::surface_elevation> (water)), width, height
    };
    const Lattice forest { numbers (spatial::get<map::forest_cover> (readings)),
                           width,
                           height };

    ReliefMap sheet;
    sheet.size = size;
    sheet.period_x_m = period_x;
    sheet.period_z_m = period_z;
    sheet.rgba.resize (static_cast<std::size_t> (size) * size * 4);
    float highest = sea_m;
    for (const float value : ground.values)
      highest = std::max (highest, value);
    sheet.highest_m = highest - sea_m;
    sheet.summits = find_summits (ground, domain, sea_m);

    // Metres to a pixel, and the pen's unit: lines keep their weight on
    // the screen whatever the sheet's size.
    const float metres_x = period_x / static_cast<float> (size);
    const float metres_z = period_z / static_cast<float> (size);
    const float metres = 0.5f * (metres_x + metres_z);
    const float pen = static_cast<float> (size) / 2048.0f;

    // Rivers, broader the more country they drain.
    Ink river_layer (size);
    const float least_area =
      std::max (1.0f, rivers.minimum_area.numerical_value_in (u::m * u::m));
    const auto river_half = [&] (float area_m2) {
      return pen * std::clamp (0.45f + 0.24f * std::log2 (std::max (
                                                 1.0f, area_m2 / least_area)),
                               0.45f,
                               2.4f);
    };
    for (const terrain::RiverReach& reach : rivers.reaches) {
      const auto& points = reach.alignment.points;
      for (std::size_t i = 0; i + 1 < points.size (); ++i) {
        const float ax = points[i].x_m;
        const float az = points[i].z_m;
        const float bx = ax + wrap_delta (points[i + 1].x_m - ax, period_x);
        const float bz = az + wrap_delta (points[i + 1].z_m - az, period_z);
        river_layer.stroke (ax / metres_x,
                            az / metres_z,
                            bx / metres_x,
                            bz / metres_z,
                            river_half (points[i].contributing_area_m2),
                            river_half (points[i + 1].contributing_area_m2));
      }
    }

    // The trail's circuit, closing on itself, in dashes.
    Ink trail_layer (size);
    {
      const auto& points = trails.alignment.points;
      float along = 0.0f;
      for (std::size_t i = 0; i < points.size () && points.size () > 1; ++i) {
        const terrain::TrailAlignmentPoint& a = points[i];
        const terrain::TrailAlignmentPoint& b =
          points[(i + 1) % points.size ()];
        const float ax = a.x_m / metres_x;
        const float az = a.z_m / metres_z;
        const float bx = ax + wrap_delta (b.x_m - a.x_m, period_x) / metres_x;
        const float bz = az + wrap_delta (b.z_m - a.z_m, period_z) / metres_z;
        trail_layer.stroke (ax,
                            az,
                            bx,
                            bz,
                            1.6f * pen,
                            1.6f * pen,
                            along,
                            9.0f * pen,
                            5.0f * pen);
        along += std::hypot (bx - ax, bz - az);
      }
    }

    // The planned roads: each a pale band in a dark casing; footpaths in
    // dots; the sanctuaries as triangles, with a faint dashed line to each
    // other one whose fire they would see.
    Ink casing_layer (size);
    Ink road_layer (size);
    Ink footpath_layer (size);
    Ink old_road_layer (size);
    Ink old_footpath_layer (size);
    Ink sanctuary_layer (size);
    Ink sightline_layer (size);
    for (const terrain::Road& road : roads.roads) {
      float along = 0.0f;
      for (std::size_t i = 0; i + 1 < road.points.size (); ++i) {
        const float ax = road.points[i].x_m / metres_x;
        const float az = road.points[i].z_m / metres_z;
        const float bx = road.points[i + 1].x_m / metres_x;
        const float bz = road.points[i + 1].z_m / metres_z;
        // A way no longer used is drawn in broken line, as maps show
        // an old road gone back to the country.
        const bool abandoned = road.abandoned_years_ago > 0.0f;
        if (road.kind == terrain::Road::Kind::Road && abandoned) {
          old_road_layer.stroke (ax,
                                 az,
                                 bx,
                                 bz,
                                 1.6f * pen,
                                 1.6f * pen,
                                 along,
                                 8.0f * pen,
                                 5.0f * pen);
        } else if (road.kind == terrain::Road::Kind::Road) {
          casing_layer.stroke (ax, az, bx, bz, 4.2f * pen, 4.2f * pen);
          road_layer.stroke (ax, az, bx, bz, 2.6f * pen, 2.6f * pen);
        } else if (abandoned) {
          old_footpath_layer.stroke (ax,
                                     az,
                                     bx,
                                     bz,
                                     1.3f * pen,
                                     1.3f * pen,
                                     along,
                                     6.0f * pen,
                                     5.0f * pen);
        } else {
          footpath_layer.stroke (ax,
                                 az,
                                 bx,
                                 bz,
                                 1.5f * pen,
                                 1.5f * pen,
                                 along,
                                 3.5f * pen,
                                 4.5f * pen);
        }
        along += std::hypot (bx - ax, bz - az);
      }
    }
    for (const auto& [a, b] : roads.sightlines) {
      const terrain::RoadPlace& one = roads.places[a];
      const terrain::RoadPlace& two = roads.places[b];
      const float ax = one.x_m / metres_x;
      const float az = one.z_m / metres_z;
      const float bx = ax + wrap_delta (two.x_m - one.x_m, period_x) / metres_x;
      const float bz = az + wrap_delta (two.z_m - one.z_m, period_z) / metres_z;
      sightline_layer.stroke (
        ax, az, bx, bz, 1.0f * pen, 1.0f * pen, 0.0f, 10.0f * pen, 8.0f * pen);
    }
    for (const terrain::RoadPlace& place : roads.places) {
      if (place.kind != terrain::RoadPlace::Kind::Sanctuary)
        continue;
      const float cx = place.x_m / metres_x;
      const float cz = place.z_m / metres_z;
      const float r = 12.0f * pen;
      const float top_x = cx, top_z = cz - r;
      const float left_x = cx - 0.87f * r, left_z = cz + 0.5f * r;
      const float right_x = cx + 0.87f * r, right_z = cz + 0.5f * r;
      const float w = 2.0f * pen;
      sanctuary_layer.stroke (top_x, top_z, left_x, left_z, w, w);
      sanctuary_layer.stroke (left_x, left_z, right_x, right_z, w, w);
      sanctuary_layer.stroke (right_x, right_z, top_x, top_z, w, w);
      sanctuary_layer.stroke (cx, cz, cx, cz, 2.6f * pen, 2.6f * pen);
    }

    // Light from the north-west, as map readers expect, a little steeper
    // than the country so that gentle ground still models.
    const float light_length = std::sqrt (0.62f * 0.62f * 2.0f + 0.48f * 0.48f);
    const float light_x = -0.62f / light_length;
    const float light_y = 0.48f / light_length;
    const float light_z = -0.62f / light_length;
    const float exaggeration = 1.25f;
    const float interval = sheet.contour_interval_m;
    const float index_interval = 5.0f * interval;
    const float relief = std::max (60.0f, sheet.highest_m);

    parallel_for (
      static_cast<std::size_t> (size),
      16,
      [&] (std::size_t begin, std::size_t end) {
        for (std::size_t py = begin; py < end; ++py)
          for (int px = 0; px < size; ++px) {
            const float x_m = (static_cast<float> (px) + 0.5f) * metres_x;
            const float z_m = (static_cast<float> (py) + 0.5f) * metres_z;
            const float gx = x_m / spacing_x;
            const float gz = z_m / spacing_z;
            // Slopes over a pixel or a sample, whichever is the coarser.
            const float step_m = 0.75f * std::max (metres, spacing_x);
            const float sx = step_m / spacing_x;
            const float sz = step_m / spacing_z;
            const float h = ground.sample (gx, gz);
            const float h_east = ground.sample (gx + sx, gz);
            const float h_west = ground.sample (gx - sx, gz);
            const float h_south = ground.sample (gx, gz + sz);
            const float h_north = ground.sample (gx, gz - sz);
            const float dhdx = (h_east - h_west) / (2.0f * step_m);
            const float dhdz = (h_south - h_north) / (2.0f * step_m);
            const float slope = std::sqrt (dhdx * dhdx + dhdz * dhdz);
            const float above = h - sea_m;

            // The ground's tint: by height, and by what grows on it.
            const float high = saturate (above / relief);
            Rgb open = mix (lowland, upland, smooth (0.05f, 0.45f, high));
            open = mix (open, fell, smooth (0.40f, 0.75f, high));
            open = mix (open, summit, smooth (0.72f, 1.0f, high));
            const float wooded = smooth (0.22f, 0.58f, forest.sample (gx, gz));
            Rgb color =
              mix (open,
                   mix (woodland, high_woodland, smooth (0.3f, 0.8f, high)),
                   0.92f * wooded);

            // Shaded relief.
            const float nx = -dhdx * exaggeration;
            const float nz = -dhdz * exaggeration;
            const float n_length = std::sqrt (nx * nx + 1.0f + nz * nz);
            const float lit =
              (nx * light_x + light_y + nz * light_z) / n_length - light_y;
            color = lit < 0.0f
                      ? mix (color, shadow_ink, saturate (-0.46f * lit))
                      : mix (color, light_ink, saturate (0.60f * lit));

            // A line where a field crosses a multiple of an interval: how
            // far the pixel lies from it across the slope, in pixels.
            const auto line =
              [&] (float value, float every, float gradient, float half_width) {
                const float t = value / every;
                const float off = std::fabs (t - std::round (t)) * every;
                return saturate (half_width + 0.5f -
                                 off / std::max (gradient * metres, 1e-4f));
              };
            // Lines that would run together are let go, the fine ones
            // first.
            const float steepness = std::max (slope, 0.004f);
            const float between = interval / (steepness * metres);

            const float water_m = level.sample (gx, gz);
            const float depth = water_m - h;
            const float d_east = level.sample (gx + sx, gz) - h_east;
            const float d_west = level.sample (gx - sx, gz) - h_west;
            const float d_south = level.sample (gx, gz + sz) - h_south;
            const float d_north = level.sample (gx, gz - sz) - h_north;
            const float depth_slope = std::max (
              std::hypot (d_east - d_west, d_south - d_north) / (2.0f * step_m),
              0.02f);
            // Dry ground carries the sheet at its own height, so water is
            // what stands a hand's breadth over it.
            const float shore_px = (depth - 0.06f) / (depth_slope * metres);
            const float wet = saturate (0.5f + shore_px);

            if (wet < 1.0f && above > 0.5f * interval) {
              const float fine =
                0.50f * line (above, interval, steepness, 0.42f * pen) *
                smooth (2.2f * pen, 4.5f * pen, between);
              const float heavy =
                0.80f * line (above, index_interval, steepness, 0.85f * pen) *
                smooth (0.45f * pen, 1.0f * pen, between);
              color = mix (color, contour_ink, std::max (fine, heavy));
            }

            if (wet > 0.0f) {
              const bool sea = water_m < sea_m + 0.35f;
              Rgb wash = sea
                           ? mix (shallows, deeps, smooth (0.0f, 45.0f, depth))
                           : fresh_water;
              if (sea && depth > 0.5f * interval)
                wash =
                  mix (wash,
                       sounding_ink,
                       0.30f *
                         line (depth, 2.0f * interval, steepness, 0.35f * pen) *
                         smooth (3.0f * pen, 6.0f * pen, 2.0f * between));
              color = mix (color, wash, wet);
              color = mix (
                color,
                shore_ink,
                0.85f * saturate (0.55f * pen + 0.5f - std::fabs (shore_px)));
            }

            color = mix (color,
                         river_ink,
                         0.92f * river_layer.at (px, static_cast<int> (py)));

            // The kilometre grid.
            const auto grid = [&] (float along_m, float metres_per_pixel) {
              const float t = along_m / 1000.0f;
              return saturate (0.25f * pen + 0.5f -
                               std::fabs (t - std::round (t)) * 1000.0f /
                                 metres_per_pixel);
            };
            color = mix (
              color,
              grid_ink,
              0.26f * std::max (grid (x_m, metres_x), grid (z_m, metres_z)));

            const int row = static_cast<int> (py);
            color =
              mix (color, sightline_ink, 0.7f * sightline_layer.at (px, row));
            color = mix (color, footpath_ink, footpath_layer.at (px, row));
            color =
              mix (color, old_way_ink, 0.85f * old_footpath_layer.at (px, row));
            color = mix (color, old_way_ink, old_road_layer.at (px, row));
            color = mix (color, road_casing_ink, casing_layer.at (px, row));
            color = mix (color, road_ink, road_layer.at (px, row));
            color = mix (color, trail_ink, trail_layer.at (px, row));
            color = mix (color, sanctuary_ink, sanctuary_layer.at (px, row));

            std::uint8_t* out =
              &sheet.rgba[(py * static_cast<std::size_t> (size) +
                           static_cast<std::size_t> (px)) *
                          4];
            out[0] = static_cast<std::uint8_t> (
              std::lround (saturate (color.r) * 255.0f));
            out[1] = static_cast<std::uint8_t> (
              std::lround (saturate (color.g) * 255.0f));
            out[2] = static_cast<std::uint8_t> (
              std::lround (saturate (color.b) * 255.0f));
            out[3] = 255;
          }
      });
    return sheet;
  }
}
