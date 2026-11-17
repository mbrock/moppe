#include <moppe/game/relief_map.hh>

#include <moppe/color.hh>
#include <moppe/parallel.hh>

#include <algorithm>
#include <cmath>
#include <span>

namespace moppe::game {
  namespace {
    using terrain::ElevationChange;
    using terrain::SurfaceElevation;

    // The number behind a ratio of two like quantities.
    template <Quantity Q>
    float ratio (Q value) {
      return value.numerical_value_in (one);
    }

    float saturate (float value) {
      return std::clamp (value, 0.0f, 1.0f);
    }

    // How much of a pixel a stroke covers, from how far its edge reaches
    // past the pixel's centre.
    float coverage (pixels_t reach) {
      return saturate (pixels_value (reach));
    }

    // How many of the sheet's pixels a ratio of like lengths comes to.
    template <Quantity Q>
    pixels_t pixels (Q value) {
      return ratio (value) * u::px;
    }

    blend_weight_t weight (float value) {
      return value * blend_weight[one];
    }

    constexpr proportion_t share (float value) {
      return value * proportion[one];
    }

    SurfaceElevation elevation (float metres) {
      return terrain::surface_elevation_point (metres * u::m);
    }

    DisplayColor mix (DisplayColor a, DisplayColor b, blend_weight_t t) {
      return mix_display (a, b, t);
    }

    // How far `value` has come from `low` to `high`, eased at both ends.
    template <typename Low, typename High, typename Value>
    blend_weight_t smooth (Low low, High high, Value value) {
      const float t = saturate (ratio ((value - low) / (high - low)));
      return weight (t * t * (3.0f - 2.0f * t));
    }

    // The sheet's inks.
    constexpr DisplayColor lowland { 0.955f, 0.930f, 0.800f };
    constexpr DisplayColor upland { 0.925f, 0.870f, 0.730f };
    constexpr DisplayColor fell { 0.900f, 0.860f, 0.790f };
    constexpr DisplayColor summit { 0.965f, 0.960f, 0.945f };
    constexpr DisplayColor woodland { 0.730f, 0.835f, 0.610f };
    constexpr DisplayColor high_woodland { 0.790f, 0.850f, 0.660f };
    constexpr DisplayColor shallows { 0.730f, 0.865f, 0.940f };
    constexpr DisplayColor deeps { 0.520f, 0.720f, 0.880f };
    constexpr DisplayColor fresh_water { 0.640f, 0.815f, 0.945f };
    constexpr DisplayColor shore_ink { 0.180f, 0.400f, 0.660f };
    constexpr DisplayColor river_ink { 0.215f, 0.470f, 0.760f };
    constexpr DisplayColor contour_ink { 0.560f, 0.360f, 0.200f };
    constexpr DisplayColor sounding_ink { 0.330f, 0.540f, 0.760f };
    constexpr DisplayColor grid_ink { 0.150f, 0.200f, 0.300f };
    constexpr DisplayColor trail_ink { 0.800f, 0.130f, 0.110f };
    constexpr DisplayColor road_casing_ink { 0.300f, 0.200f, 0.120f };
    constexpr DisplayColor road_ink { 0.985f, 0.800f, 0.380f };
    constexpr DisplayColor footpath_ink { 0.330f, 0.220f, 0.140f };
    // Ways no longer used: grey-violet, apart from the brown contours.
    constexpr DisplayColor old_way_ink { 0.380f, 0.320f, 0.460f };
    constexpr DisplayColor sanctuary_ink { 0.560f, 0.120f, 0.160f };
    constexpr DisplayColor sightline_ink { 0.850f, 0.420f, 0.150f };
    constexpr DisplayColor shadow_ink { 0.360f, 0.340f, 0.430f };
    constexpr DisplayColor light_ink { 1.000f, 0.985f, 0.940f };

    // One column of a bundle as plain numbers over the periodic lattice,
    // read between its samples: the raster's lanes, addressed in cells.
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

    meters_t wrap_delta (meters_t delta, meters_t period) {
      if (delta > 0.5f * period)
        delta -= period;
      if (delta < -0.5f * period)
        delta += period;
      return delta;
    }

    plan_displacement_t wrap_delta (const plan_displacement_t& delta,
                                    const plan_extent_t& period) {
      const Vec2& lap = plan_value (period);
      return plan_displacement (wrap_delta (x_of (delta), lap.x () * u::m),
                                wrap_delta (z_of (delta), lap.z () * u::m));
    }

    // A layer of line work: how much of each pixel its strokes cover. Its
    // strokes are placed and sized in the sheet's own pixels.
    struct Ink {
      int size = 0;
      std::vector<std::uint8_t> cover;

      explicit Ink (int size)
          : size (size), cover (static_cast<std::size_t> (size) * size) {}

      // How far the ink is laid over the pixel's colour.
      blend_weight_t at (int x, int y) const {
        return weight (
          static_cast<float> (cover[static_cast<std::size_t> (y) * size + x]) /
          255.0f);
      }

      // A stroke from a to b in pixels, tapering between two half widths.
      // With a dash length, `along` is how far down the line a lies, and
      // the stroke shows only on the dashes.
      void stroke (pixels_t ax,
                   pixels_t ay,
                   pixels_t bx,
                   pixels_t by,
                   pixels_t half_a,
                   pixels_t half_b,
                   pixels_t along = 0.0f * u::px,
                   pixels_t dash = 0.0f * u::px,
                   pixels_t gap = 0.0f * u::px) {
        constexpr pixels_t none = 0.0f * u::px, half_pixel = 0.5f * u::px;
        const pixels_t reach = std::max (half_a, half_b) + 1.0f * u::px;
        // The pixels' indices are the raster's own integers.
        const auto index = [] (pixels_t at, float (*round) (float)) {
          return static_cast<int> (round (pixels_value (at)));
        };
        const int x0 = index (std::min (ax, bx) - reach, std::floor);
        const int x1 = index (std::max (ax, bx) + reach, std::ceil);
        const int y0 = index (std::min (ay, by) - reach, std::floor);
        const int y1 = index (std::max (ay, by) + reach, std::ceil);
        const pixels_t dx = bx - ax;
        const pixels_t dy = by - ay;
        const auto length2 = dx * dx + dy * dy;
        const pixels_t length = sqrt (length2);
        for (int y = y0; y <= y1; ++y)
          for (int x = x0; x <= x1; ++x) {
            const pixels_t px =
              static_cast<float> (x) * u::px + half_pixel - ax;
            const pixels_t py =
              static_cast<float> (y) * u::px + half_pixel - ay;
            const float t = length2 > 0.0f * (u::px * u::px)
                              ? saturate (ratio ((px * dx + py * dy) / length2))
                              : 0.0f;
            const pixels_t ox = px - t * dx;
            const pixels_t oy = py - t * dy;
            const pixels_t half = half_a + (half_b - half_a) * t;
            float covered =
              coverage (half + half_pixel - sqrt (ox * ox + oy * oy));
            if (dash > none) {
              const pixels_t phase = fmod (along + t * length, dash + gap);
              covered *= coverage (std::min (phase, dash - phase) + half_pixel);
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
                  SurfaceElevation sea) {
      const meters_t spacing_x = domain.spacing_x ();
      const meters_t spacing_z = domain.spacing_z ();
      const meters_t period_x = domain.period_x ();
      const meters_t period_z = domain.period_z ();
      const plan_extent_t period = plan_extent (period_x, period_z);
      const std::size_t block =
        std::max<std::size_t> (1,
                               static_cast<std::size_t> (std::lround (
                                 ratio (200.0f * u::m / spacing_x))));
      const std::size_t across =
        std::max<std::size_t> (1, ground.width / block);
      const std::size_t down = std::max<std::size_t> (1, ground.height / block);
      struct Top {
        SurfaceElevation height = elevation (-1e9f);
        std::size_t column = 0, row = 0;
      };
      std::vector<Top> tops (across * down);
      for (std::size_t row = 0; row < down * block && row < ground.height;
           ++row)
        for (std::size_t column = 0;
             column < across * block && column < ground.width;
             ++column) {
          Top& top = tops[(row / block) * across + column / block];
          const SurfaceElevation height =
            elevation (ground.values[row * ground.width + column]);
          if (height > top.height)
            top = { height, column, row };
        }

      std::vector<ReliefMap::Summit> found;
      const long reach = 2;
      for (std::size_t z = 0; z < down; ++z)
        for (std::size_t x = 0; x < across; ++x) {
          const Top& top = tops[z * across + x];
          if (top.height - sea < 30.0f * u::m)
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
            found.push_back (
              { plan_position (static_cast<float> (top.column) * spacing_x,
                               static_cast<float> (top.row) * spacing_z),
                top.height - sea });
        }
      std::sort (found.begin (),
                 found.end (),
                 [] (const ReliefMap::Summit& a, const ReliefMap::Summit& b) {
                   return a.above_sea != b.above_sea ? a.above_sea > b.above_sea
                          : z_of (a.position) != z_of (b.position)
                            ? z_of (a.position) < z_of (b.position)
                            : x_of (a.position) < x_of (b.position);
                 });
      // About a dozen to the ordinary world, and as thick on a wider one.
      const square_meters_t ordinary_area = 25.0e6f * u::m2;
      const std::size_t limit = static_cast<std::size_t> (
        std::max (3.0f, ratio (12.0f * period_x * period_z / ordinary_area)));
      std::vector<ReliefMap::Summit> kept;
      for (const ReliefMap::Summit& candidate : found) {
        if (kept.size () >= limit)
          break;
        const bool crowded = std::any_of (
          kept.begin (), kept.end (), [&] (const ReliefMap::Summit& other) {
            const plan_displacement_t apart =
              wrap_delta (candidate.position - other.position, period);
            return hypot (x_of (apart), z_of (apart)) < 650.0f * u::m;
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
                              SurfaceElevation sea_level,
                              int size) {
    const terrain::TerrainDomain& domain = surface.domain ();
    const std::size_t width = domain.width ();
    const std::size_t height = domain.height ();
    const meters_t spacing_x = domain.spacing_x ();
    const meters_t spacing_z = domain.spacing_z ();
    const meters_t period_x = domain.period_x ();
    const meters_t period_z = domain.period_z ();
    const plan_extent_t period = plan_extent (period_x, period_z);

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
    sheet.period = period;
    sheet.rgba.resize (static_cast<std::size_t> (size) * size * 4);
    SurfaceElevation highest = sea_level;
    for (const float value : ground.values)
      highest = std::max (highest, elevation (value));
    sheet.highest_above_sea = highest - sea_level;
    sheet.summits = find_summits (ground, domain, sea_level);

    // Metres to a pixel, and the pen's unit: lines keep their weight on
    // the screen whatever the sheet's size. Line work is drawn in the
    // sheet's pixels.
    const meters_t metres_x = period_x / static_cast<float> (size);
    const meters_t metres_z = period_z / static_cast<float> (size);
    const meters_t metres = 0.5f * (metres_x + metres_z);
    const pixels_t pen = static_cast<float> (size) / 2048.0f * u::px;
    const auto across = [&] (meters_t x) { return pixels (x / metres_x); };
    const auto down = [&] (meters_t z) { return pixels (z / metres_z); };

    // Rivers, broader the more country they drain.
    Ink river_layer (size);
    const square_meters_t least_area =
      std::max (square_meters_t (1.0f * u::m2), rivers.minimum_area);
    const auto river_half = [&] (square_meters_t area) {
      return pen *
             std::clamp (0.45f + 0.24f * std::log2 (std::max (
                                           1.0f, ratio (area / least_area))),
                         0.45f,
                         2.4f);
    };
    for (const terrain::RiverReach& reach : rivers.reaches) {
      const auto& points = reach.alignment.points;
      for (std::size_t i = 0; i + 1 < points.size (); ++i) {
        const plan_position_t a = plan_position (points[i].x_m, points[i].z_m);
        const plan_position_t b =
          a +
          wrap_delta (plan_position (points[i + 1].x_m, points[i + 1].z_m) - a,
                      period);
        river_layer.stroke (
          across (x_of (a)),
          down (z_of (a)),
          across (x_of (b)),
          down (z_of (b)),
          river_half (points[i].contributing_area_m2 * u::m2),
          river_half (points[i + 1].contributing_area_m2 * u::m2));
      }
    }

    // The trail's circuit, closing on itself, in dashes.
    Ink trail_layer (size);
    {
      const auto& points = trails.alignment.points;
      pixels_t along = 0.0f * u::px;
      for (std::size_t i = 0; i < points.size () && points.size () > 1; ++i) {
        const plan_position_t& a = points[i];
        const plan_position_t& b = points[(i + 1) % points.size ()];
        const plan_displacement_t a_to_b = wrap_delta (b - a, period);
        const pixels_t ax = across (x_of (a));
        const pixels_t az = down (z_of (a));
        const pixels_t bx = ax + across (x_of (a_to_b));
        const pixels_t bz = az + down (z_of (a_to_b));
        trail_layer.stroke (ax,
                            az,
                            bx,
                            bz,
                            1.6f * pen,
                            1.6f * pen,
                            along,
                            9.0f * pen,
                            5.0f * pen);
        along += hypot (bx - ax, bz - az);
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
      pixels_t along = 0.0f * u::px;
      for (std::size_t i = 0; i + 1 < road.points.size (); ++i) {
        const pixels_t ax = across (x_of (road.points[i]));
        const pixels_t az = down (z_of (road.points[i]));
        const pixels_t bx = across (x_of (road.points[i + 1]));
        const pixels_t bz = down (z_of (road.points[i + 1]));
        // A way no longer used is drawn in broken line, as maps show
        // an old road gone back to the country.
        const bool abandoned = road.abandoned > age_t {};
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
        along += hypot (bx - ax, bz - az);
      }
    }
    for (const auto& [a, b] : roads.sightlines) {
      const terrain::RoadPlace& one = roads.places[a];
      const terrain::RoadPlace& two = roads.places[b];
      const plan_displacement_t one_to_two =
        wrap_delta (two.at - one.at, period);
      const pixels_t ax = across (x_of (one.at));
      const pixels_t az = down (z_of (one.at));
      const pixels_t bx = ax + across (x_of (one_to_two));
      const pixels_t bz = az + down (z_of (one_to_two));
      sightline_layer.stroke (ax,
                              az,
                              bx,
                              bz,
                              1.0f * pen,
                              1.0f * pen,
                              0.0f * u::px,
                              10.0f * pen,
                              8.0f * pen);
    }
    for (const terrain::RoadPlace& place : roads.places) {
      if (place.kind != terrain::RoadPlace::Kind::Sanctuary)
        continue;
      const pixels_t cx = across (x_of (place.at));
      const pixels_t cz = down (z_of (place.at));
      const pixels_t r = 12.0f * pen;
      const pixels_t top_x = cx, top_z = cz - r;
      const pixels_t left_x = cx - 0.87f * r, left_z = cz + 0.5f * r;
      const pixels_t right_x = cx + 0.87f * r, right_z = cz + 0.5f * r;
      const pixels_t w = 2.0f * pen;
      sanctuary_layer.stroke (top_x, top_z, left_x, left_z, w, w);
      sanctuary_layer.stroke (left_x, left_z, right_x, right_z, w, w);
      sanctuary_layer.stroke (right_x, right_z, top_x, top_z, w, w);
      sanctuary_layer.stroke (cx, cz, cx, cz, 2.6f * pen, 2.6f * pen);
    }

    // Light from the north-west, as map readers expect, a little steeper
    // than the country so that gentle ground still models.
    const float light_length = std::sqrt (0.62f * 0.62f * 2.0f + 0.48f * 0.48f);
    const direction_t light = unit_direction (Vec3 (
      -0.62f / light_length, 0.48f / light_length, -0.62f / light_length));
    const Vec3& sun = direction_value (light);
    const scale_factor_t exaggeration = 1.25f * scale_factor[one];
    const ElevationChange interval = sheet.contour_interval;
    const ElevationChange index_interval = 5.0f * interval;
    const ElevationChange relief =
      std::max (ElevationChange (60.0f * u::m), sheet.highest_above_sea);

    parallel_for (
      static_cast<std::size_t> (size),
      16,
      [&] (std::size_t begin, std::size_t end) {
        for (std::size_t py = begin; py < end; ++py)
          for (int px = 0; px < size; ++px) {
            const meters_t x_m = (static_cast<float> (px) + 0.5f) * metres_x;
            const meters_t z_m = (static_cast<float> (py) + 0.5f) * metres_z;
            const float gx = ratio (x_m / spacing_x);
            const float gz = ratio (z_m / spacing_z);
            // Slopes over a pixel or a sample, whichever is the coarser.
            const meters_t step_m = 0.75f * std::max (metres, spacing_x);
            const float sx = ratio (step_m / spacing_x);
            const float sz = ratio (step_m / spacing_z);
            const SurfaceElevation h = elevation (ground.sample (gx, gz));
            const SurfaceElevation h_east =
              elevation (ground.sample (gx + sx, gz));
            const SurfaceElevation h_west =
              elevation (ground.sample (gx - sx, gz));
            const SurfaceElevation h_south =
              elevation (ground.sample (gx, gz + sz));
            const SurfaceElevation h_north =
              elevation (ground.sample (gx, gz - sz));
            const grade_t dhdx =
              ratio ((h_east - h_west) / (2.0f * step_m)) * grade[one];
            const grade_t dhdz =
              ratio ((h_south - h_north) / (2.0f * step_m)) * grade[one];
            const terrain::slope_t slope =
              std::sqrt (ratio (dhdx * dhdx + dhdz * dhdz)) *
              terrain::terrain_slope[one];
            const ElevationChange above = h - sea_level;

            // The ground's tint: by height, and by what grows on it.
            const proportion_t high = share (saturate (ratio (above / relief)));
            DisplayColor open = mix (
              lowland, upland, smooth (share (0.05f), share (0.45f), high));
            open =
              mix (open, fell, smooth (share (0.40f), share (0.75f), high));
            open =
              mix (open, summit, smooth (share (0.72f), share (1.0f), high));
            const map::ForestCover cover =
              forest.sample (gx, gz) * map::forest_cover[one];
            const blend_weight_t wooded =
              smooth (share (0.22f), share (0.58f), cover);
            DisplayColor color =
              mix (open,
                   mix (woodland,
                        high_woodland,
                        smooth (share (0.3f), share (0.8f), high)),
                   0.92f * wooded);

            // Shaded relief: the cosine of the light on the steepened
            // ground, less its cosine on the level.
            const grade_t nx = -dhdx * exaggeration.numerical_value_in (one);
            const grade_t nz = -dhdz * exaggeration.numerical_value_in (one);
            const float n_length =
              std::sqrt (ratio (nx * nx) + 1.0f + ratio (nz * nz));
            const float lit =
              (ratio (nx) * sun[0] + sun[1] + ratio (nz) * sun[2]) / n_length -
              sun[1];
            color =
              lit < 0.0f
                ? mix (color, shadow_ink, weight (saturate (-0.46f * lit)))
                : mix (color, light_ink, weight (saturate (0.60f * lit)));

            // A line where a field crosses a multiple of an interval: how
            // far the pixel lies from it across the slope, in pixels.
            const auto line = [&] (ElevationChange value,
                                   ElevationChange every,
                                   terrain::slope_t gradient,
                                   pixels_t half_width) {
              const float t = ratio (value / every);
              const ElevationChange off =
                std::fabs (t - std::round (t)) * every;
              return coverage (
                half_width + 0.5f * u::px -
                pixels (off / std::max<ElevationChange> (gradient * metres,
                                                         1e-4f * u::m)));
            };
            // Lines that would run together are let go, the fine ones
            // first. How far apart they run, in pixels:
            const terrain::slope_t steepness =
              std::max (slope, 0.004f * terrain::terrain_slope[one]);
            const pixels_t between = pixels (interval / (steepness * metres));

            const SurfaceElevation water_level =
              elevation (level.sample (gx, gz));
            const ElevationChange depth = water_level - h;
            const ElevationChange d_east =
              elevation (level.sample (gx + sx, gz)) - h_east;
            const ElevationChange d_west =
              elevation (level.sample (gx - sx, gz)) - h_west;
            const ElevationChange d_south =
              elevation (level.sample (gx, gz + sz)) - h_south;
            const ElevationChange d_north =
              elevation (level.sample (gx, gz - sz)) - h_north;
            const terrain::slope_t depth_slope =
              std::max (ratio (hypot (d_east - d_west, d_south - d_north) /
                               (2.0f * step_m)) *
                          terrain::terrain_slope[one],
                        0.02f * terrain::terrain_slope[one]);
            // Dry ground carries the sheet at its own height, so water is
            // what stands a hand's breadth over it. How far the pixel is
            // into the water from the shore, in pixels:
            const pixels_t shore =
              pixels ((depth - 0.06f * u::m) / (depth_slope * metres));
            const blend_weight_t wet = weight (coverage (0.5f * u::px + shore));

            if (wet < 1.0f * blend_weight[one] && above > 0.5f * interval) {
              const blend_weight_t fine =
                0.50f * line (above, interval, steepness, 0.42f * pen) *
                smooth (2.2f * pen, 4.5f * pen, between);
              const blend_weight_t heavy =
                0.80f * line (above, index_interval, steepness, 0.85f * pen) *
                smooth (0.45f * pen, 1.0f * pen, between);
              color = mix (color, contour_ink, std::max (fine, heavy));
            }

            if (wet > 0.0f * blend_weight[one]) {
              const bool sea = water_level < sea_level + 0.35f * u::m;
              DisplayColor wash =
                sea ? mix (shallows,
                           deeps,
                           smooth (0.0f * u::m, 45.0f * u::m, depth))
                    : fresh_water;
              if (sea && depth > 0.5f * interval)
                wash =
                  mix (wash,
                       sounding_ink,
                       0.30f *
                         line (depth, 2.0f * interval, steepness, 0.35f * pen) *
                         smooth (3.0f * pen, 6.0f * pen, 2.0f * between));
              color = mix (color, wash, wet);
              color =
                mix (color,
                     shore_ink,
                     weight (0.85f * coverage (0.55f * pen + 0.5f * u::px -
                                               abs (shore))));
            }

            color = mix (color,
                         river_ink,
                         0.92f * river_layer.at (px, static_cast<int> (py)));

            // The kilometre grid.
            const auto grid = [&] (meters_t along, meters_t metres_per_pixel) {
              const float t = ratio (along / (1000.0f * u::m));
              return weight (
                coverage (0.25f * pen + 0.5f * u::px -
                          pixels (std::fabs (t - std::round (t)) * 1000.0f *
                                  u::m / metres_per_pixel)));
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
              std::lround (saturate (color.red) * 255.0f));
            out[1] = static_cast<std::uint8_t> (
              std::lround (saturate (color.green) * 255.0f));
            out[2] = static_cast<std::uint8_t> (
              std::lround (saturate (color.blue) * 255.0f));
            out[3] = 255;
          }
      });
    return sheet;
  }
}
