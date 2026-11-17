#include <moppe/correct_math.hh>
#include <moppe/terrain/trail.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace moppe::terrain {
  namespace {
    using planning_score_t = quantity<planning_score[one], float>;

    // The plain number behind a dimensionless value, for std::lerp, the
    // correctly rounded functions, and lattice arithmetic.
    template <Quantity Q>
    float in_one (Q value) {
      return value.numerical_value_in (one);
    }

    // An elevation difference as a plain length, to weigh against distances.
    meters_t as_length (ElevationChange change) {
      return quantity_cast<isq::length> (change);
    }

    // The rise of a path over its run.
    grade_t grade_of (ElevationChange rise, meters_t run) {
      return quantity_cast<grade> (quantity_cast<isq::height> (rise) / run);
    }

    // The height a grade climbs over a run.
    ElevationChange rise_over (grade_t slope, meters_t run) {
      return meters_t (quantity_cast<isq::length> (slope * run));
    }

    ElevationChange elevation_change (float metres) {
      return metres * surface_elevation[u::m];
    }

    SurfaceElevation elevation_point (const TerrainDomain& domain,
                                      std::span<const SurfaceElevation> values,
                                      int column,
                                      int row) {
      return values[domain.offset (
        { .column = static_cast<std::size_t> (column),
          .row = static_cast<std::size_t> (row) })];
    }

    SurfaceElevation elevation_between (SurfaceElevation from,
                                        SurfaceElevation to,
                                        blend_weight_t t) {
      return surface_elevation_point (std::lerp (surface_elevation_value (from),
                                                 surface_elevation_value (to),
                                                 in_one (t)) *
                                      u::m);
    }

    blend_weight_t
    shoulder_ramp (meters_t distance, meters_t half_width, meters_t blend) {
      if (distance <= half_width)
        return 1.0f * blend_weight[one];
      if (blend <= 0.0f * u::m)
        return 0.0f * blend_weight[one];
      const magnitude_t ratio = (distance - half_width) / blend;
      const magnitude_t t = std::min (ratio, 1.0f * one);
      const magnitude_t smooth = t * t * (3.0f * one - 2.0f * t);
      return quantity_cast<blend_weight> (1.0f * one - smooth);
    }

    // How far apart two alignment points are, measured as cr::hypot.
    meters_t distance (const plan_position_t& a, const plan_position_t& b) {
      return moppe::hypot (a - b);
    }

    plan_extent_t period_of (const TerrainDomain& grid) {
      return plan_extent (grid.period_x (), grid.period_z ());
    }

    plan_position_t alignment_closure (const TrailAlignment& alignment,
                                       const TerrainDomain& grid) {
      if (alignment.points.empty ())
        return plan_position (0.0f, 0.0f);
      return nearest_image (
        alignment.points.front (), alignment.points.back (), period_of (grid));
    }

    struct AlignmentRaster {
      std::vector<meters_t> distance;
      std::vector<meters_t> signed_distance;
      std::vector<std::size_t> segment;
      std::vector<blend_weight_t> segment_t;
    };

    AlignmentRaster rasterize_alignment (const TerrainDomain& grid,
                                         const TrailAlignment& alignment,
                                         meters_t radius) {
      const int width = static_cast<int> (grid.width ());
      const int height = static_cast<int> (grid.height ());
      const std::size_t count = grid.width () * grid.height ();
      const std::size_t no_segment = alignment.points.size ();
      const meters_t spacing_x = grid.spacing_x ();
      const meters_t spacing_z = grid.spacing_z ();
      AlignmentRaster result {
        .distance = std::vector<meters_t> (
          count, std::numeric_limits<float>::infinity () * u::m),
        .signed_distance = std::vector<meters_t> (count, 0.0f * u::m),
        .segment = std::vector<std::size_t> (count, no_segment),
        .segment_t =
          std::vector<blend_weight_t> (count, 0.0f * blend_weight[one])
      };
      if (alignment.points.size () < 2)
        return result;

      const plan_position_t closure = alignment_closure (alignment, grid);
      for (std::size_t segment = 0; segment < alignment.points.size ();
           ++segment) {
        const plan_position_t a = alignment.points[segment];
        const plan_position_t b = segment + 1 < alignment.points.size ()
                                    ? alignment.points[segment + 1]
                                    : closure;
        const plan_displacement_t ab = b - a;
        const meters_t ab_x = x_of (ab);
        const meters_t ab_z = z_of (ab);
        const square_meters_t length2 = ab_x * ab_x + ab_z * ab_z;
        if (length2 <= 1e-8f * u::m * u::m)
          continue;
        const int min_x = static_cast<int> (std::floor (
          in_one ((std::min (x_of (a), x_of (b)) - radius) / spacing_x)));
        const int max_x = static_cast<int> (std::ceil (
          in_one ((std::max (x_of (a), x_of (b)) + radius) / spacing_x)));
        const int min_y = static_cast<int> (std::floor (
          in_one ((std::min (z_of (a), z_of (b)) - radius) / spacing_z)));
        const int max_y = static_cast<int> (std::ceil (
          in_one ((std::max (z_of (a), z_of (b)) + radius) / spacing_z)));
        for (int unwrapped_y = min_y; unwrapped_y <= max_y; ++unwrapped_y)
          for (int unwrapped_x = min_x; unwrapped_x <= max_x; ++unwrapped_x) {
            const plan_position_t point =
              plan_position (static_cast<float> (unwrapped_x) * spacing_x,
                             static_cast<float> (unwrapped_y) * spacing_z);
            const plan_displacement_t ap = point - a;
            const magnitude_t projection =
              (x_of (ap) * ab_x + z_of (ap) * ab_z) / length2;
            const blend_weight_t t = quantity_cast<blend_weight> (
              std::clamp (projection, 0.0f * one, 1.0f * one));
            const plan_position_t nearest = a + ab * in_one (t);
            const meters_t candidate = distance (point, nearest);
            if (candidate > radius)
              continue;
            const int x = wrap_index (unwrapped_x, width);
            const int y = wrap_index (unwrapped_y, height);
            const std::size_t cell = static_cast<std::size_t> (y) * width + x;
            if (candidate < result.distance[cell]) {
              result.distance[cell] = candidate;
              const per_metre_t inverse_length = 1.0f / sqrt (length2);
              const plan_displacement_t off = point - nearest;
              result.signed_distance[cell] =
                (x_of (off) * -ab_z + z_of (off) * ab_x) * inverse_length;
              result.segment[cell] = segment;
              result.segment_t[cell] = t;
            }
          }
      }
      return result;
    }

    // Bilinear in the lattice: x and y are positions counted in cells.
    SurfaceElevation
    sample_height (std::span<const SurfaceElevation> elevations,
                   const TerrainDomain& grid,
                   const plan_position_t& point) {
      const std::span<const float> heights =
        surface_elevation_values (elevations);
      const int width = static_cast<int> (grid.width ());
      const int height = static_cast<int> (grid.height ());
      float x = in_one (x_of (point) / grid.spacing_x ());
      float y = in_one (z_of (point) / grid.spacing_z ());
      x = wrap_coordinate (x, static_cast<float> (width));
      y = wrap_coordinate (y, static_cast<float> (height));
      const int x0 = static_cast<int> (std::floor (x));
      const int y0 = static_cast<int> (std::floor (y));
      const int x1 = wrap_index (x0 + 1, width);
      const int y1 = wrap_index (y0 + 1, height);
      const blend_weight_t tx = (x - x0) * blend_weight[one];
      const blend_weight_t ty = (y - y0) * blend_weight[one];
      const meters_t h0 =
        std::lerp (heights[static_cast<std::size_t> (y0) * width + x0],
                   heights[static_cast<std::size_t> (y0) * width + x1],
                   in_one (tx)) *
        u::m;
      const meters_t h1 =
        std::lerp (heights[static_cast<std::size_t> (y1) * width + x0],
                   heights[static_cast<std::size_t> (y1) * width + x1],
                   in_one (tx)) *
        u::m;
      return surface_elevation_point (
        std::lerp (meters_value (h0), meters_value (h1), in_one (ty)) * u::m);
    }

    struct PlanningGrid {
      struct EdgeProfile {
        grade_t mean_grade;
        grade_t maximum_grade;
        grade_t maximum_achievable_grade;
        SurfaceElevation maximum_elevation;
      };

      const TerrainDomain& domain;
      std::span<const SurfaceElevation> elevations;
      const DrainageGraph& drainage;
      const FloodField& flood;
      int source_width;
      int source_height;
      int width;
      int height;
      meters_t spacing_x;
      meters_t spacing_y;

      int wrap_x (int x) const {
        return wrap_index (x, width);
      }

      int wrap_y (int y) const {
        return wrap_index (y, height);
      }

      std::size_t node (int x, int y) const {
        return static_cast<std::size_t> (wrap_y (y)) * width + wrap_x (x);
      }

      int x (std::size_t node) const {
        return static_cast<int> (node % width);
      }

      int y (std::size_t node) const {
        return static_cast<int> (node / width);
      }

      int source_x (int x) const {
        return std::min (source_width - 1, x * source_width / width);
      }

      int source_y (int y) const {
        return std::min (source_height - 1, y * source_height / height);
      }

      std::size_t source_cell (std::size_t node) const {
        return static_cast<std::size_t> (source_y (y (node))) * source_width +
               source_x (x (node));
      }

      SurfaceElevation elevation (std::size_t node) const {
        return elevation_point (
          domain, elevations, source_x (x (node)), source_y (y (node)));
      }

      bool wet (std::size_t node) const {
        return flood.water_depth_m (source_cell (node)) > 1e-7f;
      }

      square_meters_t catchment (std::size_t node) const {
        return drainage.contributing_area_at (source_cell (node));
      }

      // Lattice offsets, in planning cells, the short way round.
      float delta_x (int from, int to) const {
        int delta = to - from;
        if (delta > width / 2)
          delta -= width;
        if (delta < -width / 2)
          delta += width;
        return static_cast<float> (delta);
      }

      float delta_y (int from, int to) const {
        int delta = to - from;
        if (delta > height / 2)
          delta -= height;
        if (delta < -height / 2)
          delta += height;
        return static_cast<float> (delta);
      }

      meters_t distance (std::size_t a, std::size_t b) const {
        return moppe::hypot (
          plan_displacement (delta_x (x (a), x (b)) * spacing_x,
                             delta_y (y (a), y (b)) * spacing_y));
      }

      // The ground's steepest fall to any of the eight neighbours.
      slope_t steepness (std::size_t node) const {
        slope_t maximum = 0.0f * terrain_slope[one];
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0)
              continue;
            const std::size_t neighbor =
              this->node (x (node) + dx, y (node) + dy);
            const meters_t run = distance (node, neighbor);
            if (run > 0.0f * u::m)
              maximum = std::max (
                maximum,
                quantity_cast<terrain_slope> (
                  abs (elevation (neighbor) - elevation (node)) / run));
          }
        return maximum;
      }

      EdgeProfile edge_profile (std::size_t from,
                                std::size_t to,
                                ElevationChange earthwork_budget) const {
        const int x0 = source_x (x (from));
        const int y0 = source_y (y (from));
        int dx = source_x (x (to)) - x0;
        int dy = source_y (y (to)) - y0;
        if (dx > source_width / 2)
          dx -= source_width;
        if (dx < -source_width / 2)
          dx += source_width;
        if (dy > source_height / 2)
          dy -= source_height;
        if (dy < -source_height / 2)
          dy += source_height;
        const int steps = std::max (std::abs (dx), std::abs (dy));
        ElevationChange accumulated_rise = elevation_change (0.0f);
        meters_t accumulated_run = 0.0f * u::m;
        grade_t maximum_grade = 0.0f * grade[one];
        grade_t maximum_achievable = 0.0f * grade[one];
        int previous_x = x0;
        int previous_y = y0;
        const auto source_x_at = [this] (int value) {
          return wrap_index (value, source_width);
        };
        const auto source_y_at = [this] (int value) {
          return wrap_index (value, source_height);
        };
        SurfaceElevation previous_elevation = elevation_point (
          domain, elevations, source_x_at (x0), source_y_at (y0));
        SurfaceElevation maximum_elevation = previous_elevation;
        for (int step = 1; step <= steps; ++step) {
          const blend_weight_t t =
            static_cast<float> (step) / steps * blend_weight[one];
          int current_x = x0 + static_cast<int> (std::round (dx * in_one (t)));
          int current_y = y0 + static_cast<int> (std::round (dy * in_one (t)));
          const SurfaceElevation current_elevation =
            elevation_point (domain,
                             elevations,
                             source_x_at (current_x),
                             source_y_at (current_y));
          maximum_elevation = std::max (maximum_elevation, current_elevation);
          const meters_t run = moppe::hypot (plan_displacement (
            static_cast<float> (current_x - previous_x) * domain.spacing_x (),
            static_cast<float> (current_y - previous_y) * domain.spacing_z ()));
          if (run > 0.0f * u::m) {
            const ElevationChange rise =
              abs (current_elevation - previous_elevation);
            accumulated_rise += rise;
            accumulated_run += run;
            maximum_grade = std::max (maximum_grade, grade_of (rise, run));
            maximum_achievable =
              std::max (maximum_achievable,
                        grade_of (std::max (elevation_change (0.0f),
                                            rise - earthwork_budget),
                                  run));
          }
          previous_x = current_x;
          previous_y = current_y;
          previous_elevation = current_elevation;
        }
        return { .mean_grade = accumulated_run > 0.0f * u::m
                                 ? grade_of (accumulated_rise, accumulated_run)
                                 : 0.0f * grade[one],
                 .maximum_grade = maximum_grade,
                 .maximum_achievable_grade = maximum_achievable,
                 .maximum_elevation = maximum_elevation };
      }
    };

    SurfaceElevation sea_elevation (const PlanningGrid& grid,
                                    const TrailFormation& parameters) {
      (void)grid;
      return parameters.sea_level;
    }

    SurfaceElevation
    preferred_route_elevation (const PlanningGrid& grid,
                               const TrailFormation& parameters) {
      return sea_elevation (grid, parameters) +
             parameters.highland_preference_height_above_sea;
    }

    SurfaceElevation
    alpine_avoidance_elevation (const PlanningGrid& grid,
                                const TrailFormation& parameters) {
      return sea_elevation (grid, parameters) +
             parameters.alpine_avoidance_height_above_sea;
    }

    // How far an elevation reaches into the band between the preferred and
    // the avoided heights: nothing below it, one at its top, more above.
    scale_factor_t highland_ratio (SurfaceElevation elevation,
                                   const PlanningGrid& grid,
                                   const TrailFormation& parameters) {
      const SurfaceElevation preferred =
        preferred_route_elevation (grid, parameters);
      const SurfaceElevation avoidance =
        alpine_avoidance_elevation (grid, parameters);
      return quantity_cast<scale_factor> (
        std::max (elevation_change (0.0f), elevation - preferred) /
        std::max (avoidance - preferred, elevation_change (1.0f)));
    }

    PlanningGrid planning_grid (const TerrainDomain& domain,
                                std::span<const SurfaceElevation> elevations,
                                const DrainageGraph& drainage,
                                const FloodField& flood) {
      const TerrainDomain& source = domain;
      const int source_width = static_cast<int> (source.width ());
      const int source_height = static_cast<int> (source.height ());
      constexpr meters_t planning_spacing = 16.0f * u::m;
      const int width = std::clamp (static_cast<int> (std::round (in_one (
                                      static_cast<float> (source_width) *
                                      source.spacing_x () / planning_spacing))),
                                    std::min (8, source_width),
                                    source_width);
      const int height =
        std::clamp (static_cast<int> (std::round (
                      in_one (static_cast<float> (source_height) *
                              source.spacing_z () / planning_spacing))),
                    std::min (8, source_height),
                    source_height);
      return { .domain = domain,
               .elevations = elevations,
               .drainage = drainage,
               .flood = flood,
               .source_width = source_width,
               .source_height = source_height,
               .width = width,
               .height = height,
               .spacing_x = static_cast<float> (source_width) *
                            source.spacing_x () / static_cast<float> (width),
               .spacing_y = static_cast<float> (source_height) *
                            source.spacing_z () / static_cast<float> (height) };
    }

    TrailAlignment
    make_alignment (const PlanningGrid& planner,
                    const std::vector<std::size_t>& coarse_circuit) {
      const TerrainDomain& source = planner.domain;
      const meters_t source_spacing_x = source.spacing_x ();
      const meters_t source_spacing_z = source.spacing_z ();
      const plan_extent_t period = period_of (source);
      std::vector<plan_position_t> raw;
      raw.reserve (coarse_circuit.size ());
      for (const std::size_t node : coarse_circuit) {
        plan_position_t point = plan_position (
          static_cast<float> (planner.source_x (planner.x (node))) *
            source_spacing_x,
          static_cast<float> (planner.source_y (planner.y (node))) *
            source_spacing_z);
        if (!raw.empty ())
          point = nearest_image (point, raw.back (), period);
        if (raw.empty () || distance (raw.back (), point) > 1e-4f * u::m)
          raw.push_back (point);
      }
      if (raw.size () < 4)
        throw std::runtime_error (
          "trail circuit is too short for a smooth alignment");

      // A damped periodic cubic Hermite curve passes through the A* route
      // while rounding its eight-way corners. Short handles keep the curve
      // close to the searched corridor instead of producing Catmull-Rom-style
      // loops or large overshoots at alternating turns.
      constexpr scale_factor_t tangent_scale = 0.34f * scale_factor[one];
      const meters_t sample_spacing =
        std::clamp (0.4f * std::min (source_spacing_x, source_spacing_z),
                    0.75f * u::m,
                    2.0f * u::m);
      const plan_position_t closure =
        nearest_image (raw.front (), raw.back (), period);
      TrailAlignment alignment;
      for (std::size_t segment = 0; segment < raw.size (); ++segment) {
        const plan_position_t a = raw[segment];
        const plan_position_t b =
          segment + 1 < raw.size () ? raw[segment + 1] : closure;
        const plan_position_t before =
          segment > 0 ? raw[segment - 1]
                      : nearest_image (raw.back (), a, period);
        const plan_position_t after =
          segment + 2 < raw.size ()   ? raw[segment + 2]
          : segment + 1 < raw.size () ? closure
                                      : nearest_image (raw[1], b, period);
        const plan_displacement_t tangent_a =
          (b - before) * in_one (tangent_scale);
        const plan_displacement_t tangent_b =
          (after - a) * in_one (tangent_scale);
        const int subdivisions =
          std::max (1,
                    static_cast<int> (
                      std::ceil (in_one (distance (a, b) / sample_spacing))));
        for (int step = 0; step < subdivisions; ++step) {
          // The Hermite basis: weights that may fall below zero.
          const float t = static_cast<float> (step) / subdivisions;
          const float t2 = t * t;
          const float t3 = t2 * t;
          const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
          const float h10 = t3 - 2.0f * t2 + t;
          const float h01 = -2.0f * t3 + 3.0f * t2;
          const float h11 = t3 - t2;
          const plan_position_t point =
            plan_position (plan_value (a) * h00 + plan_value (tangent_a) * h10 +
                           plan_value (b) * h01 + plan_value (tangent_b) * h11);
          if (alignment.points.empty () ||
              distance (alignment.points.back (), point) > 1e-4f * u::m)
            alignment.points.push_back (point);
        }
      }
      if (alignment.points.size () < 4)
        throw std::runtime_error (
          "trail alignment sampling produced too few points");
      meters_f64_t length = 0.0 * u::m;
      for (std::size_t i = 1; i < alignment.points.size (); ++i)
        length += distance (alignment.points[i - 1], alignment.points[i]);
      length += distance (alignment.points.back (),
                          alignment_closure (alignment, source));
      alignment.length = length;
      return alignment;
    }

    meters_t nearest_water (const PlanningGrid& grid,
                            std::size_t node,
                            meters_t search_distance) {
      const int reach = std::max (
        1,
        static_cast<int> (std::ceil (in_one (
          search_distance / std::min (grid.spacing_x, grid.spacing_y)))));
      meters_t nearest = std::numeric_limits<float>::infinity () * u::m;
      for (int dy = -reach; dy <= reach; ++dy)
        for (int dx = -reach; dx <= reach; ++dx) {
          const std::size_t candidate =
            grid.node (grid.x (node) + dx, grid.y (node) + dy);
          if (grid.wet (candidate))
            nearest = std::min (nearest, grid.distance (node, candidate));
        }
      return nearest;
    }

    struct HomeBaseSite {
      std::size_t node;
      planning_score_t score;
    };

    std::vector<HomeBaseSite>
    choose_home_bases (const PlanningGrid& grid,
                       const TrailFormation& parameters) {
      const SurfaceElevation sea = sea_elevation (grid, parameters);
      const meters_t target_water = parameters.home_base_water_distance;
      const int shelter_reach = std::clamp (
        static_cast<int> (std::round (
          in_one (120.0f * u::m / std::min (grid.spacing_x, grid.spacing_y)))),
        2,
        8);
      std::vector<HomeBaseSite> candidates;
      for (std::size_t node = 0;
           node < static_cast<std::size_t> (grid.width) * grid.height;
           ++node) {
        if (grid.wet (node))
          continue;
        // Keep the visible circuit away from the arbitrary cut in the
        // canonical periodic chart.
        if (grid.x (node) < grid.width * 0.12f ||
            grid.x (node) > grid.width * 0.88f ||
            grid.y (node) < grid.height * 0.12f ||
            grid.y (node) > grid.height * 0.88f)
          continue;
        const SurfaceElevation elevation = grid.elevation (node);
        const ElevationChange above_sea = elevation - sea;
        const slope_t slope = grid.steepness (node);
        if (above_sea < parameters.minimum_height_above_sea ||
            slope > 0.75f * terrain_slope[one])
          continue;
        const meters_t water = nearest_water (grid, node, target_water * 2.5f);
        if (!std::isfinite (meters_value (water)))
          continue;

        ElevationChange shelter = elevation_change (0.0f);
        ElevationChange distant_relief = elevation_change (0.0f);
        constexpr int directions = 12;
        for (int direction = 0; direction < directions; ++direction) {
          const radians_t angle =
            2.0f * std::numbers::pi_v<float> * direction / directions * u::rad;
          const int dx = static_cast<int> (
            std::round (cr::cos (radians_value (angle)) * shelter_reach));
          const int dy = static_cast<int> (
            std::round (cr::sin (radians_value (angle)) * shelter_reach));
          const ElevationChange rise =
            grid.elevation (
              grid.node (grid.x (node) + dx, grid.y (node) + dy)) -
            elevation;
          shelter = std::max (shelter, rise);
          const int far_dx = dx * 4;
          const int far_dy = dy * 4;
          distant_relief =
            std::max (distant_relief,
                      grid.elevation (grid.node (grid.x (node) + far_dx,
                                                 grid.y (node) + far_dy)) -
                        elevation);
        }
        const planning_score_t water_score =
          (36.0f - 0.20f * meters_value (abs (water - target_water))) *
          planning_score[one];
        const planning_score_t flat_score =
          (30.0f - 110.0f * in_one (slope)) * planning_score[one];
        const meters_t sheltered = as_length (std::clamp (
          shelter, elevation_change (0.0f), elevation_change (40.0f)));
        const planning_score_t shelter_score =
          (12.0f - 0.35f * meters_value (abs (sheltered - 16.0f * u::m))) *
          planning_score[one];
        const planning_score_t view_score =
          meters_value (as_length (std::clamp (distant_relief,
                                               elevation_change (0.0f),
                                               elevation_change (180.0f)))) *
          0.08f * planning_score[one];
        const scale_factor_t alpine =
          highland_ratio (elevation, grid, parameters);
        const planning_score_t alpine_penalty =
          45.0f * in_one (alpine) * in_one (alpine) * planning_score[one];
        const planning_score_t score = water_score + flat_score +
                                       shelter_score + view_score -
                                       alpine_penalty;
        candidates.push_back ({ node, score });
      }
      if (candidates.empty ())
        throw std::runtime_error ("no dry home-base site near water");
      std::ranges::sort (
        candidates, [] (const HomeBaseSite& a, const HomeBaseSite& b) {
          return a.score != b.score ? a.score > b.score : a.node < b.node;
        });
      std::vector<HomeBaseSite> sites;
      const meters_t separation =
        std::max (120.0f * u::m, 0.22f * parameters.desired_circuit_radius);
      for (const HomeBaseSite candidate : candidates) {
        const bool distinct =
          std::ranges::all_of (sites, [&] (const HomeBaseSite site) {
            return grid.distance (candidate.node, site.node) >= separation;
          });
        if (distinct)
          sites.push_back (candidate);
        if (sites.size () == 8)
          break;
      }
      return sites;
    }

    struct ScenicCandidate {
      std::size_t node;
      planning_score_t score;
    };

    meters_t desired_planning_radius (const PlanningGrid& grid,
                                      const TrailFormation& parameters) {
      const meters_t world_span =
        std::min (static_cast<float> (grid.width) * grid.spacing_x,
                  static_cast<float> (grid.height) * grid.spacing_y);
      return std::clamp (parameters.desired_circuit_radius,
                         0.12f * world_span,
                         0.36f * world_span);
    }

    std::vector<std::size_t>
    choose_scenic_focuses (const PlanningGrid& grid,
                           std::size_t base,
                           const TrailFormation& parameters) {
      const meters_t desired = desired_planning_radius (grid, parameters);
      const int relief_reach = std::clamp (
        static_cast<int> (std::round (in_one (
          desired * 0.28f / std::min (grid.spacing_x, grid.spacing_y)))),
        2,
        12);
      std::vector<ScenicCandidate> candidates;
      for (std::size_t node = 0;
           node < static_cast<std::size_t> (grid.width) * grid.height;
           ++node) {
        if (grid.x (node) < grid.width * 0.07f ||
            grid.x (node) > grid.width * 0.93f ||
            grid.y (node) < grid.height * 0.07f ||
            grid.y (node) > grid.height * 0.93f)
          continue;
        const meters_t distance = grid.distance (base, node);
        if (distance < 0.52f * desired || distance > 1.48f * desired)
          continue;
        const SurfaceElevation elevation = grid.elevation (node);
        SurfaceElevation low = elevation;
        SurfaceElevation high = low;
        for (int dy : { -relief_reach, 0, relief_reach })
          for (int dx : { -relief_reach, 0, relief_reach }) {
            const SurfaceElevation height = grid.elevation (
              grid.node (grid.x (node) + dx, grid.y (node) + dy));
            low = std::min (low, height);
            high = std::max (high, height);
          }
        const planning_score_t distance_score =
          30.0f * (1.0f - in_one (abs (distance - desired) / desired)) *
          planning_score[one];
        const planning_score_t feature_score =
          ((grid.wet (node) ? 38.0f : 0.0f) +
           meters_value (as_length (std::clamp (
             high - low, elevation_change (0.0f), elevation_change (160.0f)))) *
             0.22f) *
          planning_score[one];
        // Relief is scenery, not an instruction to put the trail on its
        // summit. Prefer a low viewpoint beside the feature and progressively
        // distrust alpine focuses as they approach the avoidance height.
        const scale_factor_t alpine =
          highland_ratio (elevation, grid, parameters);
        const planning_score_t score =
          distance_score + feature_score -
          55.0f * in_one (alpine) * in_one (alpine) * planning_score[one];
        candidates.push_back ({ node, score });
      }
      if (candidates.empty ())
        throw std::runtime_error ("no scenic focus for trail circuit");
      // One expedition gets a few genuinely different circuits to try:
      // routing around the best focus can fail on a single blocked pass
      // that a focus in another quarter of the annulus never meets.
      std::ranges::sort (
        candidates, [] (const ScenicCandidate& a, const ScenicCandidate& b) {
          return a.score != b.score ? a.score > b.score : a.node < b.node;
        });
      const meters_t separation = 0.4f * desired;
      std::vector<std::size_t> focuses;
      for (const ScenicCandidate candidate : candidates) {
        const bool distinct =
          std::ranges::all_of (focuses, [&] (const std::size_t focus) {
            return grid.distance (candidate.node, focus) >= separation;
          });
        if (distinct)
          focuses.push_back (candidate.node);
        if (focuses.size () == 4)
          break;
      }
      return focuses;
    }

    // Feasibility leaves a small slack above the authored maximum grade so
    // discretization noise cannot sever an otherwise buildable corridor. The
    // desperate slack is the last resort for closing a loop on heavily
    // dissected terrain: eroded valley networks are dendritic, and when no
    // second disjoint arm exists at the leisurely limit, a short technical
    // pitch over a spur ridge beats abandoning the circuit.
    constexpr grade_t feasible_grade_slack = 0.04f * grade[one];
    constexpr grade_t desperate_grade_slack = 0.12f * grade[one];

    std::vector<std::uint8_t> routable_land (const PlanningGrid& grid,
                                             std::size_t start,
                                             const TrailFormation& parameters) {
      const std::size_t count =
        static_cast<std::size_t> (grid.width) * grid.height;
      std::vector<std::uint8_t> reachable (count, 0);
      if (grid.wet (start))
        return reachable;
      const grade_t maximum_grade = parameters.maximum_grade;
      const ElevationChange earthwork_budget =
        parameters.maximum_cut + parameters.maximum_fill;
      std::queue<std::size_t> frontier;
      reachable[start] = 1;
      frontier.push (start);
      while (!frontier.empty ()) {
        const std::size_t current = frontier.front ();
        frontier.pop ();
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0)
              continue;
            const std::size_t next =
              grid.node (grid.x (current) + dx, grid.y (current) + dy);
            if (reachable[next] || grid.wet (next))
              continue;
            const meters_t run = grid.distance (current, next);
            if (run <= 0.0f * u::m)
              continue;
            const PlanningGrid::EdgeProfile profile =
              grid.edge_profile (current, next, earthwork_budget);
            if (profile.maximum_achievable_grade >
                maximum_grade + feasible_grade_slack)
              continue;
            reachable[next] = 1;
            frontier.push (next);
          }
      }
      return reachable;
    }

    // ideal_x and ideal_y are a place on the planning lattice, in cells.
    std::size_t
    nearest_buildable_site (const PlanningGrid& grid,
                            float ideal_x,
                            float ideal_y,
                            int search_radius,
                            const std::vector<std::uint8_t>& reachable,
                            const TrailFormation& parameters) {
      planning_score_t best_score =
        std::numeric_limits<float>::infinity () * planning_score[one];
      const std::size_t count =
        static_cast<std::size_t> (grid.width) * grid.height;
      std::size_t best = count;
      for (int dy = -search_radius; dy <= search_radius; ++dy)
        for (int dx = -search_radius; dx <= search_radius; ++dx) {
          const std::size_t node =
            grid.node (static_cast<int> (std::round (ideal_x)) + dx,
                       static_cast<int> (std::round (ideal_y)) + dy);
          if (!reachable[node])
            continue;
          const scale_factor_t alpine =
            highland_ratio (grid.elevation (node), grid, parameters);
          const planning_score_t score =
            (cr::hypot (static_cast<float> (dx), static_cast<float> (dy)) +
             12.0f * in_one (grid.steepness (node)) +
             18.0f * in_one (alpine) * in_one (alpine)) *
            planning_score[one];
          if (score < best_score) {
            best_score = score;
            best = node;
          }
        }
      if (best != count)
        return best;

      const std::size_t ideal =
        grid.node (static_cast<int> (std::round (ideal_x)),
                   static_cast<int> (std::round (ideal_y)));
      for (std::size_t node = 0; node < count; ++node) {
        if (!reachable[node])
          continue;
        const scale_factor_t alpine =
          highland_ratio (grid.elevation (node), grid, parameters);
        const planning_score_t score =
          (in_one (grid.distance (ideal, node) /
                   std::min (grid.spacing_x, grid.spacing_y)) +
           12.0f * in_one (grid.steepness (node)) +
           18.0f * in_one (alpine) * in_one (alpine)) *
          planning_score[one];
        if (score < best_score) {
          best_score = score;
          best = node;
        }
      }
      if (best == count)
        throw std::runtime_error (
          "no buildable trail control site on home-base land");
      return best;
    }

    // A* over (node, heading) states. A ride's cost is its length weighted
    // by how hard each metre of it is to build and to ride.
    std::vector<std::size_t>
    shortest_ride (const PlanningGrid& grid,
                   std::size_t start,
                   std::size_t goal,
                   const TrailFormation& parameters,
                   const std::vector<std::uint8_t>& avoid = {},
                   grade_t grade_slack = feasible_grade_slack) {
      constexpr int headings = 8;
      constexpr int no_heading = headings;
      constexpr int states_per_node = headings + 1;
      constexpr int heading_x[headings] = { -1, 0, 1, 1, 1, 0, -1, -1 };
      constexpr int heading_y[headings] = { -1, -1, -1, 0, 1, 1, 1, 0 };
      struct Candidate {
        meters_t estimate;
        std::size_t state;
        bool operator> (const Candidate& other) const {
          return estimate != other.estimate ? estimate > other.estimate
                                            : state > other.state;
        }
      };
      const std::size_t node_count =
        static_cast<std::size_t> (grid.width) * grid.height;
      const std::size_t state_count = node_count * states_per_node;
      const meters_t infinity = std::numeric_limits<float>::infinity () * u::m;
      std::vector<meters_t> cost (state_count, infinity);
      std::vector<std::size_t> previous (state_count, state_count);
      std::priority_queue<Candidate,
                          std::vector<Candidate>,
                          std::greater<Candidate>>
        frontier;
      const auto state = [] (std::size_t node, int heading) {
        return node * states_per_node + static_cast<std::size_t> (heading);
      };
      const auto state_node = [] (std::size_t state) {
        return state / states_per_node;
      };
      const auto state_heading = [] (std::size_t state) {
        return static_cast<int> (state % states_per_node);
      };
      const std::size_t start_state = state (start, no_heading);
      cost[start_state] = 0.0f * u::m;
      frontier.push ({ grid.distance (start, goal), start_state });
      const grade_t maximum_grade = parameters.maximum_grade;
      const grade_t designed_grade = parameters.designed_grade;
      const ElevationChange earthwork_budget =
        parameters.maximum_cut + parameters.maximum_fill;
      // Construction capacity is a feasibility limit, not a discount. A
      // larger cut/fill allowance must not make a steep edge more attractive
      // than a contour-following edge that needs less work.
      constexpr ElevationChange preferred_earthwork =
        4.0f * surface_elevation[u::m];
      const square_meters_t target_catchment = sqrt (
        parameters.minimum_catchment_area * parameters.maximum_catchment_area);
      const meters_t direct_distance =
        std::max (grid.distance (start, goal), grid.spacing_x);
      std::size_t goal_state = state_count;
      while (!frontier.empty ()) {
        const Candidate current = frontier.top ();
        frontier.pop ();
        const std::size_t current_node = state_node (current.state);
        const int previous_heading = state_heading (current.state);
        if (current_node == goal) {
          goal_state = current.state;
          break;
        }
        if (current.estimate > cost[current.state] +
                                 grid.distance (current_node, goal) +
                                 1e-4f * u::m)
          continue;
        for (int next_heading = 0; next_heading < headings; ++next_heading) {
          const int dx = heading_x[next_heading];
          const int dy = heading_y[next_heading];
          const std::size_t next =
            grid.node (grid.x (current_node) + dx, grid.y (current_node) + dy);
          if (grid.wet (next) && next != goal)
            continue;
          if (!avoid.empty () && avoid[next] && next != goal)
            continue;
          const meters_t run = grid.distance (current_node, next);
          if (run <= 0.0f * u::m)
            continue;
          const PlanningGrid::EdgeProfile profile =
            grid.edge_profile (current_node, next, earthwork_budget);
          const ElevationChange rise =
            abs (grid.elevation (next) - grid.elevation (current_node));
          const grade_t route_grade =
            std::max (profile.mean_grade, 0.55f * profile.maximum_grade);
          const grade_t achievable_grade = profile.maximum_achievable_grade;
          if (achievable_grade > maximum_grade + grade_slack)
            continue;
          const scale_factor_t maximum_excess = quantity_cast<scale_factor> (
            std::max (0.0f * grade[one], achievable_grade - maximum_grade) /
            std::max (maximum_grade, 0.01f * grade[one]));
          const grade_t grade_scale =
            std::max (designed_grade, 0.01f * grade[one]);
          const scale_factor_t grade_ratio =
            quantity_cast<scale_factor> (route_grade / grade_scale);
          const ElevationChange earthwork = std::max (
            elevation_change (0.0f), rise - rise_over (designed_grade, run));
          const scale_factor_t earthwork_ratio =
            quantity_cast<scale_factor> (earthwork / preferred_earthwork);
          const scale_factor_t detour_ratio = quantity_cast<scale_factor> (
            (grid.distance (start, next) + grid.distance (next, goal)) /
            direct_distance);
          const scale_factor_t corridor = std::max (
            0.0f * scale_factor[one], detour_ratio - 1.12f * scale_factor[one]);
          scale_factor_t turn = 0.0f * scale_factor[one];
          if (previous_heading != no_heading) {
            // The cosine between two lattice headings.
            const float dot =
              (heading_x[previous_heading] * heading_x[next_heading] +
               heading_y[previous_heading] * heading_y[next_heading]) /
              std::sqrt (static_cast<float> (
                (heading_x[previous_heading] * heading_x[previous_heading] +
                 heading_y[previous_heading] * heading_y[previous_heading]) *
                (heading_x[next_heading] * heading_x[next_heading] +
                 heading_y[next_heading] * heading_y[next_heading])));
            turn = (1.0f - dot) * scale_factor[one];
          }
          const scale_factor_t valley =
            std::fabs (
              cr::log (in_one (std::max (grid.catchment (next),
                                         square_meters_t (1.0f * u::m * u::m)) /
                               target_catchment))) *
            scale_factor[one];
          const scale_factor_t alpine =
            highland_ratio (profile.maximum_elevation, grid, parameters);
          const int chart_edge_distance =
            std::min ({ grid.x (next),
                        grid.width - 1 - grid.x (next),
                        grid.y (next),
                        grid.height - 1 - grid.y (next) });
          const scale_factor_t chart_edge =
            (chart_edge_distance < 4
               ? static_cast<float> (4 - chart_edge_distance) * 8.0f
               : 0.0f) *
            scale_factor[one];
          const meters_t edge =
            run *
            (1.0f + 0.70f * in_one (grade_ratio) * in_one (grade_ratio) +
             12.0f * in_one (earthwork_ratio) * in_one (earthwork_ratio) +
             30.0f * in_one (maximum_excess) * in_one (maximum_excess) +
             2.5f * in_one (turn) * in_one (turn) +
             20.0f * in_one (corridor) * in_one (corridor) +
             0.10f * in_one (valley) +
             8.0f * in_one (alpine) * in_one (alpine) + in_one (chart_edge));
          const meters_t next_cost = cost[current.state] + edge;
          const std::size_t next_state = state (next, next_heading);
          if (next_cost < cost[next_state]) {
            cost[next_state] = next_cost;
            previous[next_state] = current.state;
            frontier.push (
              { next_cost + grid.distance (next, goal), next_state });
          }
        }
      }
      if (goal_state == state_count)
        return {};
      std::vector<std::size_t> path;
      for (std::size_t cursor = goal_state;; cursor = previous[cursor]) {
        path.push_back (state_node (cursor));
        if (cursor == start_state)
          break;
      }
      std::reverse (path.begin (), path.end ());
      return path;
    }

    void append_path (std::vector<std::size_t>& target,
                      const std::vector<std::size_t>& path) {
      if (path.empty ())
        return;
      const std::size_t first =
        !target.empty () && target.back () == path[0] ? 1 : 0;
      target.insert (target.end (), path.begin () + first, path.end ());
    }

    void avoid_path (const PlanningGrid& grid,
                     const std::vector<std::size_t>& path,
                     std::size_t open_start,
                     std::size_t open_end,
                     int padding,
                     std::vector<std::uint8_t>& avoid) {
      const meters_t open_radius =
        1.75f * std::max (grid.spacing_x, grid.spacing_y);
      for (const std::size_t node : path) {
        if (grid.distance (node, open_start) < open_radius ||
            grid.distance (node, open_end) < open_radius)
          continue;
        for (int dy = -padding; dy <= padding; ++dy)
          for (int dx = -padding; dx <= padding; ++dx)
            avoid[grid.node (grid.x (node) + dx, grid.y (node) + dy)] = 1;
      }
    }

    struct CoarseCircuitPlan {
      std::size_t home_base;
      std::size_t scenic_focus;
      std::size_t left;
      std::size_t far;
      std::size_t right;
      std::vector<std::size_t> circuit;
      planning_score_t score;
      // A home base in a small routable pocket snaps every anchor inward
      // and closes a courtyard loop whose accumulated cost undercuts any
      // real excursion. Collapse is judged against the desired radius so
      // such a loop only ships when no expedition managed better.
      bool collapsed;
    };

    // Route the two arms of one circuit: the outbound home-left-far ride is
    // free, while the inbound home-right-far ride must stay off it so the
    // loop encloses the focus instead of retracing itself.
    std::optional<std::vector<std::size_t>>
    route_circuit_arms (const PlanningGrid& planner,
                        std::size_t home_base,
                        std::size_t left,
                        std::size_t far,
                        std::size_t right,
                        const TrailFormation& parameters,
                        std::string& failure) {
      const std::size_t planner_cells =
        static_cast<std::size_t> (planner.width) * planner.height;
      const std::vector<std::size_t> home_to_left =
        shortest_ride (planner, home_base, left, parameters);
      if (home_to_left.empty ()) {
        failure = "home base could not reach the left control site";
        return std::nullopt;
      }
      // Prefer a full coarse cell of space between arms. Narrow passes may
      // not have 48 metres to spare, so retry while forbidding only actual
      // centerline reuse; the last attempt also accepts short technical
      // pitches so a spur ridge can carry the second arm.
      struct Attempt {
        int padding;
        grade_t grade_slack;
      };
      constexpr Attempt attempts[] = {
        { .padding = 1, .grade_slack = feasible_grade_slack },
        { .padding = 0, .grade_slack = feasible_grade_slack },
        { .padding = 0, .grade_slack = desperate_grade_slack },
      };
      for (const Attempt attempt : attempts) {
        std::vector<std::size_t> outbound = home_to_left;
        std::vector<std::uint8_t> outbound_avoid (planner_cells, 0);
        avoid_path (
          planner, outbound, left, far, attempt.padding, outbound_avoid);
        const std::vector<std::size_t> left_to_far = shortest_ride (
          planner, left, far, parameters, outbound_avoid, attempt.grade_slack);
        if (left_to_far.empty ()) {
          failure = "left control site could not reach the far control site";
          continue;
        }
        append_path (outbound, left_to_far);
        if (outbound.front () != home_base || outbound.back () != far) {
          failure = "outbound route did not join its control sites";
          continue;
        }

        std::vector<std::uint8_t> inbound_avoid (planner_cells, 0);
        avoid_path (
          planner, outbound, home_base, far, attempt.padding, inbound_avoid);
        std::vector<std::size_t> inbound = shortest_ride (planner,
                                                          home_base,
                                                          right,
                                                          parameters,
                                                          inbound_avoid,
                                                          attempt.grade_slack);
        if (inbound.empty ()) {
          failure = "home base could not reach the right control site";
          continue;
        }
        avoid_path (
          planner, inbound, right, far, attempt.padding, inbound_avoid);
        const std::vector<std::size_t> right_to_far = shortest_ride (
          planner, right, far, parameters, inbound_avoid, attempt.grade_slack);
        if (right_to_far.empty ()) {
          failure = "right control site could not reach the far control site";
          continue;
        }
        append_path (inbound, right_to_far);
        if (inbound.front () != home_base || inbound.back () != far) {
          failure = "inbound route did not join its control sites";
          continue;
        }

        // The avoidance openings around the shared endpoints let the arms
        // approach on any bearing, but a node used by both arms would later
        // splice the scenic head out of the fine-grid circuit. Treat that
        // as a failed attempt rather than a routing success.
        std::vector<std::uint8_t> outbound_nodes (planner_cells, 0);
        for (const std::size_t node : outbound)
          outbound_nodes[node] = 1;
        bool arms_share_ground = false;
        for (std::size_t i = 1; i + 1 < inbound.size (); ++i)
          arms_share_ground |= outbound_nodes[inbound[i]] != 0;
        if (arms_share_ground) {
          failure = "circuit arms could not enclose the focus apart";
          continue;
        }

        std::vector<std::size_t> circuit = std::move (outbound);
        if (inbound.size () > 2)
          for (std::size_t i = inbound.size () - 1; i-- > 1;)
            circuit.push_back (inbound[i]);
        return circuit;
      }
      return std::nullopt;
    }

    std::optional<CoarseCircuitPlan>
    explore_circuit (const PlanningGrid& planner,
                     HomeBaseSite site,
                     const TrailFormation& parameters,
                     std::string& failure) {
      const std::size_t home_base = site.node;
      const std::vector<std::uint8_t> reachable =
        routable_land (planner, home_base, parameters);
      const std::vector<std::size_t> focuses =
        choose_scenic_focuses (planner, home_base, parameters);
      // The anchors' frame is laid out on the planning lattice, in cells.
      const float flank = std::clamp (
        0.12f * std::min (planner.width, planner.height), 2.0f, 18.0f);
      const int anchor_search = std::max (2, static_cast<int> (flank * 0.55f));
      const meters_t desired = desired_planning_radius (planner, parameters);
      std::optional<CoarseCircuitPlan> pocket_loop;
      for (const std::size_t focus : focuses) {
        float forward_x =
          planner.delta_x (planner.x (home_base), planner.x (focus));
        float forward_y =
          planner.delta_y (planner.y (home_base), planner.y (focus));
        const float forward_length = cr::hypot (forward_x, forward_y);
        forward_x /= std::max (forward_length, 1.0f);
        forward_y /= std::max (forward_length, 1.0f);
        const float focus_x = static_cast<float> (planner.x (focus));
        const float focus_y = static_cast<float> (planner.y (focus));
        const std::size_t left =
          nearest_buildable_site (planner,
                                  focus_x - forward_y * flank,
                                  focus_y + forward_x * flank,
                                  anchor_search,
                                  reachable,
                                  parameters);
        const std::size_t far =
          nearest_buildable_site (planner,
                                  focus_x + forward_x * flank,
                                  focus_y + forward_y * flank,
                                  anchor_search,
                                  reachable,
                                  parameters);
        const std::size_t right =
          nearest_buildable_site (planner,
                                  focus_x + forward_y * flank,
                                  focus_y - forward_x * flank,
                                  anchor_search,
                                  reachable,
                                  parameters);
        std::optional<std::vector<std::size_t>> circuit = route_circuit_arms (
          planner, home_base, left, far, right, parameters, failure);
        if (!circuit)
          continue;

        // Effort-weighted metres, as shortest_ride weighs them.
        meters_t route_cost = 0.0f * u::m;
        meters_t length = 0.0f * u::m;
        for (std::size_t i = 0; i < circuit->size (); ++i) {
          const std::size_t from = (*circuit)[i];
          const std::size_t to = (*circuit)[(i + 1) % circuit->size ()];
          const meters_t run = planner.distance (from, to);
          const grade_t step_grade =
            run > 0.0f * u::m
              ? grade_of (
                  abs (planner.elevation (to) - planner.elevation (from)), run)
              : 0.0f * grade[one];
          const scale_factor_t alpine =
            highland_ratio (planner.elevation (to), planner, parameters);
          length += run;
          route_cost +=
            run * (1.0f + 8.0f * in_one (step_grade) * in_one (step_grade) +
                   4.0f * in_one (alpine) * in_one (alpine));
        }
        // A metre of effort costs one point of the home base's score.
        CoarseCircuitPlan plan { .home_base = home_base,
                                 .scenic_focus = focus,
                                 .left = left,
                                 .far = far,
                                 .right = right,
                                 .circuit = std::move (*circuit),
                                 .score = (meters_value (route_cost) -
                                           24.0f * in_one (site.score)) *
                                          planning_score[one],
                                 .collapsed = length < 2.0f * desired };
        if (!plan.collapsed)
          return plan;
        // A pocketed home base collapses every focus the same way; keep the
        // first courtyard loop as a last resort and keep looking.
        if (!pocket_loop)
          pocket_loop = std::move (plan);
        failure = "circuit collapsed far short of the desired radius";
      }
      return pocket_loop;
    }

    std::vector<CellIndex>
    expand_circuit (const PlanningGrid& grid,
                    const std::vector<std::size_t>& coarse_circuit) {
      std::vector<CellIndex> circuit;
      const std::size_t no_position =
        static_cast<std::size_t> (grid.source_width) * grid.source_height;
      std::vector<std::size_t> position (no_position, no_position);
      const auto append_cell =
        [&circuit, &position, &grid, no_position] (int x, int y) {
          x = wrap_index (x, grid.source_width);
          y = wrap_index (y, grid.source_height);
          const std::size_t cell =
            static_cast<std::size_t> (y) * grid.source_width + x;
          if (!circuit.empty () && circuit.back ().value == cell)
            return;
          if (position[cell] != no_position) {
            const std::size_t retained = position[cell] + 1;
            for (std::size_t i = retained; i < circuit.size (); ++i)
              position[circuit[i].value] = no_position;
            circuit.resize (retained);
            return;
          }
          position[cell] = circuit.size ();
          circuit.emplace_back (static_cast<std::uint32_t> (cell));
        };
      for (std::size_t segment = 0; segment < coarse_circuit.size ();
           ++segment) {
        const std::size_t from = coarse_circuit[segment];
        const std::size_t to =
          coarse_circuit[(segment + 1) % coarse_circuit.size ()];
        const int x0 = grid.source_x (grid.x (from));
        const int y0 = grid.source_y (grid.y (from));
        int dx = grid.source_x (grid.x (to)) - x0;
        int dy = grid.source_y (grid.y (to)) - y0;
        if (dx > grid.source_width / 2)
          dx -= grid.source_width;
        if (dx < -grid.source_width / 2)
          dx += grid.source_width;
        if (dy > grid.source_height / 2)
          dy -= grid.source_height;
        if (dy < -grid.source_height / 2)
          dy += grid.source_height;
        const int steps = std::max (std::abs (dx), std::abs (dy));
        for (int step = 0; step < steps; ++step) {
          const blend_weight_t t =
            (steps ? static_cast<float> (step) / steps : 0.0f) *
            blend_weight[one];
          append_cell (x0 + static_cast<int> (std::round (dx * in_one (t))),
                       y0 + static_cast<int> (std::round (dy * in_one (t))));
        }
      }
      return circuit;
    }

    bool circuit_is_contiguous (const TerrainDomain& grid,
                                const std::vector<CellIndex>& circuit) {
      if (circuit.size () < 2)
        return false;
      const int width = static_cast<int> (grid.width ());
      const int height = static_cast<int> (grid.height ());
      for (std::size_t i = 0; i < circuit.size (); ++i) {
        const CellIndex from = circuit[i];
        const CellIndex to = circuit[(i + 1) % circuit.size ()];
        int dx = std::abs (static_cast<int> (from.value % width) -
                           static_cast<int> (to.value % width));
        int dy = std::abs (static_cast<int> (from.value / width) -
                           static_cast<int> (to.value / width));
        dx = std::min (dx, width - dx);
        dy = std::min (dy, height - dy);
        if ((dx == 0 && dy == 0) || dx > 1 || dy > 1)
          return false;
      }
      return true;
    }
  }

  void TrailFormation::validate () const {
    if (!std::isfinite (surface_elevation_value (sea_level)) ||
        !std::isfinite ((minimum_catchment_area)
                          .numerical_value_in (moppe::u::m * moppe::u::m)) ||
        !std::isfinite ((maximum_catchment_area)
                          .numerical_value_in (moppe::u::m * moppe::u::m)) ||
        minimum_catchment_area <=
          0.0f * mp_units::si::metre * mp_units::si::metre ||
        maximum_catchment_area < minimum_catchment_area ||
        !std::isfinite (
          (minimum_height_above_sea).numerical_value_in (moppe::u::m)) ||
        minimum_height_above_sea < 0.0f * mp_units::si::metre ||
        !std::isfinite ((width).numerical_value_in (moppe::u::m)) ||
        width <= 0.0f * mp_units::si::metre ||
        !std::isfinite ((shoulder_blend).numerical_value_in (moppe::u::m)) ||
        shoulder_blend < 0.0f * mp_units::si::metre ||
        !std::isfinite ((maximum_cut).numerical_value_in (moppe::u::m)) ||
        maximum_cut < 0.0f * mp_units::si::metre ||
        !std::isfinite ((maximum_fill).numerical_value_in (moppe::u::m)) ||
        maximum_fill < 0.0f * mp_units::si::metre ||
        !std::isfinite (maximum_grade.numerical_value_in (mp_units::one)) ||
        maximum_grade < 0.0f * grade[mp_units::one] ||
        !std::isfinite (designed_grade.numerical_value_in (mp_units::one)) ||
        designed_grade < 0.0f * grade[mp_units::one] ||
        designed_grade > maximum_grade ||
        !std::isfinite (crossfall.numerical_value_in (mp_units::one)) ||
        crossfall < 0.0f * grade[mp_units::one] ||
        crossfall > 0.1f * grade[mp_units::one] || grading_iterations < 0 ||
        !std::isfinite (
          (home_base_water_distance).numerical_value_in (moppe::u::m)) ||
        home_base_water_distance <= 0.0f * mp_units::si::metre ||
        !std::isfinite (
          (home_base_pad_radius).numerical_value_in (moppe::u::m)) ||
        home_base_pad_radius <= 0.0f * mp_units::si::metre ||
        !std::isfinite (
          (desired_circuit_radius).numerical_value_in (moppe::u::m)) ||
        desired_circuit_radius <= 0.0f * mp_units::si::metre ||
        !std::isfinite ((highland_preference_height_above_sea)
                          .numerical_value_in (moppe::u::m)) ||
        highland_preference_height_above_sea <= 0.0f * mp_units::si::metre ||
        !std::isfinite ((alpine_avoidance_height_above_sea)
                          .numerical_value_in (moppe::u::m)) ||
        alpine_avoidance_height_above_sea <
          highland_preference_height_above_sea)
      throw std::invalid_argument ("trail formation parameters are invalid");
  }

  TrailNetwork
  detail::analyze_trail_network (const TerrainDomain& grid,
                                 std::span<const SurfaceElevation> elevations,
                                 const TrailFormation& parameters,
                                 const DrainageGraph& drainage,
                                 const FloodField& flood) {
    MOPPE_PROFILE_ZONE ("analyze_trail_network");
    parameters.validate ();
    const int width = static_cast<int> (grid.width ());
    const int height = static_cast<int> (grid.height ());
    const std::size_t count = grid.width () * grid.height ();
    if (drainage.width () != static_cast<std::size_t> (width) ||
        drainage.height () != static_cast<std::size_t> (height) ||
        flood.width () != static_cast<std::size_t> (width) ||
        flood.height () != static_cast<std::size_t> (height))
      throw std::invalid_argument (
        "trail network readings do not match terrain");

    const PlanningGrid planner =
      planning_grid (grid, elevations, drainage, flood);
    std::optional<CoarseCircuitPlan> chosen;
    std::vector<CellIndex> chosen_cells;
    const std::vector<HomeBaseSite> sites =
      choose_home_bases (planner, parameters);
    std::vector<std::string> failures;
    for (const HomeBaseSite site : sites) {
      try {
        std::string failure;
        std::optional<CoarseCircuitPlan> expedition =
          explore_circuit (planner, site, parameters, failure);
        if (!expedition) {
          failures.push_back (std::move (failure));
          continue;
        }
        if (chosen) {
          const bool better = expedition->collapsed == chosen->collapsed
                                ? expedition->score < chosen->score
                                : chosen->collapsed;
          if (!better)
            continue;
        }
        std::vector<CellIndex> cells =
          expand_circuit (planner, expedition->circuit);
        if (cells.size () >= 4 && circuit_is_contiguous (grid, cells)) {
          chosen = std::move (expedition);
          chosen_cells = std::move (cells);
        } else {
          failures.emplace_back (
            "coarse route did not expand to a contiguous fine-grid circuit");
        }
      } catch (const std::runtime_error& error) {
        // An expedition can fail to find a focus while another base succeeds.
        failures.emplace_back (error.what ());
      }
    }
    if (!chosen) {
      std::ostringstream message;
      message << "no home-base expedition found a complete trail circuit";
      bool first = true;
      for (std::size_t i = 0; i < failures.size (); ++i) {
        if (std::find (failures.begin (), failures.begin () + i, failures[i]) !=
            failures.begin () + i)
          continue;
        const std::size_t repetitions = static_cast<std::size_t> (
          std::count (failures.begin (), failures.end (), failures[i]));
        message << (first ? ": " : "; ") << repetitions << "x " << failures[i];
        first = false;
      }
      throw std::runtime_error (message.str ());
    }
    const std::size_t home_base = chosen->home_base;
    const std::size_t focus = chosen->scenic_focus;
    const std::size_t left = chosen->left;
    const std::size_t far = chosen->far;
    const std::size_t right = chosen->right;
    TrailAlignment alignment = make_alignment (planner, chosen->circuit);

    const CellIndex home_base_cell { static_cast<std::uint32_t> (
      planner.source_cell (home_base)) };
    const CellIndex focus_cell { static_cast<std::uint32_t> (
      planner.source_cell (focus)) };
    const auto source_cell = [&planner] (std::size_t node) {
      return CellIndex { static_cast<std::uint32_t> (
        planner.source_cell (node)) };
    };
    TrailPlan plan { .home_base = home_base_cell,
                     .scenic_focus = focus_cell,
                     .control_sites = { home_base_cell,
                                        source_cell (left),
                                        source_cell (far),
                                        source_cell (right) },
                     .circuit = std::move (chosen_cells) };

    std::vector<TrailInfluence> influence (count, 0.0f * trail_influence[one]);
    std::vector<HomeBaseInfluence> home_base_influence (
      count, 0.0f * terrain::home_base_influence[one]);
    // Unlike the constructed height stamp, the material footprint is a
    // distance to the continuous alignment: one on the centreline, falling
    // linearly to nothing at the formed width plus its shoulder. Each
    // consumer then cuts its own width from the same field -- a narrow dirt
    // tread, a grass verge, a wider forest corridor -- and bilinear sampling
    // of a distance keeps even a tread narrower than a grid cell legible.
    const meters_t half_width = 0.5f * parameters.width;
    const meters_t blend = parameters.shoulder_blend;
    const meters_t radius = half_width + blend;
    const AlignmentRaster material_raster =
      rasterize_alignment (grid, alignment, radius);
    for (std::size_t cell = 0; cell < count; ++cell)
      if (flood.water_depth_m (cell) <= 1e-7f &&
          material_raster.distance[cell] < radius)
        influence[cell] =
          (1.0f - in_one (material_raster.distance[cell] / radius)) *
          trail_influence[one];

    const meters_t base_radius = parameters.home_base_pad_radius;
    const meters_t base_blend = std::max (6.0f * u::m, 0.45f * base_radius);
    const int base_reach_x =
      static_cast<int> (
        std::ceil (in_one ((base_radius + base_blend) / grid.spacing_x ()))) +
      1;
    const int base_reach_y =
      static_cast<int> (
        std::ceil (in_one ((base_radius + base_blend) / grid.spacing_z ()))) +
      1;
    const int base_x = static_cast<int> (home_base_cell.value % width);
    const int base_y = static_cast<int> (home_base_cell.value / width);
    for (int dy = -base_reach_y; dy <= base_reach_y; ++dy)
      for (int dx = -base_reach_x; dx <= base_reach_x; ++dx) {
        const int x = wrap_index (base_x + dx, width);
        const int y = wrap_index (base_y + dy, height);
        const meters_t distance = moppe::hypot (
          plan_displacement (static_cast<float> (dx) * grid.spacing_x (),
                             static_cast<float> (dy) * grid.spacing_z ()));
        const std::size_t cell = static_cast<std::size_t> (y) * width + x;
        if (flood.water_depth_m (cell) <= 1e-7f)
          home_base_influence[cell] =
            in_one (shoulder_ramp (distance, base_radius, base_blend)) *
            terrain::home_base_influence[one];
      }

    return { .domain = grid,
             .plan = std::move (plan),
             .alignment = std::move (alignment),
             .formed_width = parameters.width,
             .earthwork_delta =
               std::vector<ElevationChange> (count, elevation_change (0.0f)),
             .use = TrailUseMap (
               grid, std::move (influence), std::move (home_base_influence)) };
  }

  TrailNetwork
  detail::analyze_trail_network (const TerrainDomain& domain,
                                 std::span<const SurfaceElevation> elevations,
                                 const TrailFormation& parameters) {
    const DrainageGraph drainage = analyze_drainage (domain, elevations);
    const FloodField flood =
      analyze_standing_water (domain, elevations, parameters.sea_level);
    return analyze_trail_network (
      domain, elevations, parameters, drainage, flood);
  }

  TrailFormationResult
  detail::form_trails (const TerrainDomain& grid,
                       std::span<const SurfaceElevation> elevations,
                       const TrailFormation& parameters) {
    MOPPE_PROFILE_ZONE ("form_trails");
    parameters.validate ();
    const int width = static_cast<int> (grid.width ());
    const int height = static_cast<int> (grid.height ());
    const std::size_t count = grid.width () * grid.height ();
    const square_meters_t cell_area = grid.cell_area ();

    std::vector<SurfaceElevation> original (count);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        original[static_cast<std::size_t> (y) * width + x] =
          elevations[grid.offset ({ .column = static_cast<std::size_t> (x),
                                    .row = static_cast<std::size_t> (y) })];

    const DrainageGraph drainage = analyze_drainage (grid, elevations);
    const FloodField flood =
      analyze_standing_water (grid, elevations, parameters.sea_level);
    TrailNetwork network =
      analyze_trail_network (grid, elevations, parameters, drainage, flood);

    // Solve the vertical profile along arc-length samples of the continuous
    // alignment. Horizontal intent and longitudinal grade are deliberately
    // separate: the former says where the trail goes, while this pass says
    // what the bulldozer can build there within bounded cut and fill.
    const std::size_t alignment_count = network.alignment.points.size ();
    std::vector<SurfaceElevation> original_profile (alignment_count);
    for (std::size_t point = 0; point < alignment_count; ++point)
      original_profile[point] =
        sample_height (original, grid, network.alignment.points[point]);
    std::vector<SurfaceElevation> profile = original_profile;
    const grade_t maximum_grade = parameters.maximum_grade;
    const meters_t maximum_cut = parameters.maximum_cut;
    const meters_t maximum_fill = parameters.maximum_fill;
    for (int iteration = 0;
         iteration < count_value (parameters.grading_iterations);
         ++iteration) {
      for (std::size_t point = 0; point < alignment_count; ++point) {
        const std::size_t next = (point + 1) % alignment_count;
        const plan_position_t next_position =
          next ? network.alignment.points[next]
               : alignment_closure (network.alignment, grid);
        const ElevationChange allowed =
          rise_over (maximum_grade,
                     distance (network.alignment.points[point], next_position));
        const ElevationChange difference = profile[point] - profile[next];
        if (abs (difference) <= allowed)
          continue;
        const ElevationChange excess = abs (difference) - allowed;
        // Which way the excess runs: down from this point, or up to it.
        const float direction =
          difference > elevation_change (0.0f) ? 1.0f : -1.0f;
        profile[point] -= direction * 0.5f * excess;
        profile[next] += direction * 0.5f * excess;
        profile[point] = std::clamp (profile[point],
                                     original_profile[point] - maximum_cut,
                                     original_profile[point] + maximum_fill);
        profile[next] = std::clamp (profile[next],
                                    original_profile[next] - maximum_cut,
                                    original_profile[next] + maximum_fill);
      }
    }

    // Raster construction is derived from the nearest point on that same
    // alignment. The geometric core still grows to one source cell at coarse
    // resolutions so collision remains a continuous formed surface; the
    // independently generated material footprint retains the authored width.
    const meters_t authored_half_width = 0.5f * parameters.width;
    const meters_t half_width =
      std::max (authored_half_width,
                1.01f * std::max (grid.spacing_x (), grid.spacing_z ()));
    const meters_t blend = parameters.shoulder_blend;
    const meters_t radius = half_width + blend;
    const AlignmentRaster formation_raster =
      rasterize_alignment (grid, network.alignment, radius);

    // Pick the naturally lower side along the alignment, falling back to the
    // loop exterior where the ground is effectively level. The crossfall is
    // clamped to the authored tread width; a coarse preview may widen the
    // construction stamp for continuity, but must not magnify a
    // three-centimetres-per-metre drainage slope into a large terrace.
    const grade_t crossfall = parameters.crossfall;
    const meters_t probe_distance =
      std::max (authored_half_width,
                0.5f * std::min (grid.spacing_x (), grid.spacing_z ()));
    // Which side of the centreline each segment's tread falls toward: +1
    // along its left-hand normal, -1 against it, blended in between.
    std::vector<float> downhill_side (alignment_count, 1.0f);
    quantity<u::m * u::m, double> signed_area = 0.0 * u::m * u::m;
    for (std::size_t segment = 0; segment < alignment_count; ++segment) {
      const plan_position_t a = network.alignment.points[segment];
      const plan_position_t b = segment + 1 < alignment_count
                                  ? network.alignment.points[segment + 1]
                                  : alignment_closure (network.alignment, grid);
      signed_area += value_cast<double> (x_of (a)) * z_of (b) -
                     value_cast<double> (x_of (b)) * z_of (a);
    }
    const float exterior_side = signed_area >= 0.0 * u::m * u::m ? -1.0f : 1.0f;
    for (std::size_t segment = 0; segment < alignment_count; ++segment) {
      const plan_position_t a = network.alignment.points[segment];
      const plan_position_t b = segment + 1 < alignment_count
                                  ? network.alignment.points[segment + 1]
                                  : alignment_closure (network.alignment, grid);
      const plan_displacement_t along_segment = b - a;
      const meters_t segment_length = distance (a, b);
      if (segment_length <= 1e-5f * u::m)
        continue;
      const plan_position_t midpoint = a + along_segment * 0.5f;
      const plan_direction_t normal = plan_unit_direction (
        Vec2 (in_one (-z_of (along_segment) / segment_length),
              in_one (x_of (along_segment) / segment_length)));
      const SurfaceElevation positive_height = sample_height (
        original, grid, midpoint + along (normal, probe_distance));
      const SurfaceElevation negative_height = sample_height (
        original, grid, midpoint + along (normal, -probe_distance));
      const ElevationChange natural_difference =
        negative_height - positive_height;
      const float natural_side =
        natural_difference >= elevation_change (0.0f) ? 1.0f : -1.0f;
      const magnitude_t steepness =
        (abs (natural_difference) - elevation_change (0.05f)) /
        elevation_change (0.20f);
      const blend_weight_t transition = quantity_cast<blend_weight> (
        std::clamp (steepness, 0.0f * one, 1.0f * one));
      const blend_weight_t smooth_transition =
        in_one (transition) * in_one (transition) *
        (3.0f - 2.0f * in_one (transition)) * blend_weight[one];
      // On an unambiguously sloped sidehill, drain with gravity. Around a
      // ridge, saddle, or nearly level patch, ease toward the loop exterior
      // instead of letting tiny height noise flip the crossfall every couple
      // of metres.
      downhill_side[segment] =
        std::lerp (exterior_side, natural_side, in_one (smooth_transition));
    }

    std::vector<SurfaceElevation> shaped = original;
    std::vector<ElevationChange> earthwork_delta (count,
                                                  elevation_change (0.0f));
    TrailFormationReport report;
    report.centerline_cells = cell_count (network.plan.circuit.size ());
    report.circuit_length = network.alignment.length;
    meters_f64_t absolute_change = 0.0 * u::m;
    meters_f64_t maximum_change = 0.0 * u::m;
    cubic_meters_f64_t cut_volume = 0.0 * u::m * u::m * u::m;
    cubic_meters_f64_t fill_volume = 0.0 * u::m * u::m * u::m;
    for (std::size_t cell = 0; cell < count; ++cell) {
      if (formation_raster.segment[cell] >= alignment_count ||
          flood.water_depth_m (cell) > 1e-7f)
        continue;
      const SurfaceElevation natural = original[cell];
      const std::size_t segment = formation_raster.segment[cell];
      const std::size_t next = (segment + 1) % alignment_count;
      const SurfaceElevation centerline = elevation_between (
        profile[segment], profile[next], formation_raster.segment_t[cell]);
      const meters_t across =
        std::clamp (formation_raster.signed_distance[cell],
                    -authored_half_width,
                    authored_half_width);
      const SurfaceElevation target =
        centerline - rise_over (crossfall * downhill_side[segment], across);
      const blend_weight_t influence =
        shoulder_ramp (formation_raster.distance[cell], half_width, blend);
      const SurfaceElevation formed =
        std::clamp (natural + in_one (influence) * (target - natural),
                    natural - maximum_cut,
                    natural + maximum_fill);
      const ElevationChange difference = formed - natural;
      const meters_f64_t change = as_length (difference);
      if (abs (change) <= 1e-6 * u::m)
        continue;
      shaped[cell] = formed;
      earthwork_delta[cell] = difference;
      ++report.shaped_cells;
      absolute_change += abs (change);
      maximum_change = std::max (maximum_change, abs (change));
      if (change < 0.0 * u::m)
        cut_volume += -change * cell_area;
      else
        fill_volume += change * cell_area;
    }
    report.cut_volume = cut_volume;
    report.fill_volume = fill_volume;
    report.maximum_absolute_change = maximum_change;
    if (report.shaped_cells)
      report.mean_absolute_change =
        absolute_change /
        static_cast<double> (count_value (report.shaped_cells));
    meters_f64_t grade_distance = 0.0 * u::m;
    meters_f64_t accumulated_rise = 0.0 * u::m;
    meters_f64_t maximum_height_above_sea = 0.0 * u::m;
    const meters_f64_t sea =
      static_cast<double> (surface_elevation_value (parameters.sea_level)) *
      u::m;
    for (std::size_t point = 0; point < alignment_count; ++point) {
      const std::size_t next = (point + 1) % alignment_count;
      const plan_position_t next_position =
        next ? network.alignment.points[next]
             : alignment_closure (network.alignment, grid);
      const meters_f64_t run =
        distance (network.alignment.points[point], next_position);
      const meters_f64_t elevation =
        static_cast<double> (surface_elevation_value (
          sample_height (shaped, grid, network.alignment.points[point]))) *
        u::m;
      maximum_height_above_sea =
        std::max (maximum_height_above_sea, elevation - sea);
      if (run <= 0.0 * u::m)
        continue;
      const meters_f64_t rise = as_length (
        abs (sample_height (shaped, grid, network.alignment.points[point]) -
             sample_height (shaped, grid, next_position)));
      const grade_f64_t segment_grade = quantity_cast<grade> (rise / run);
      grade_distance += run;
      accumulated_rise += rise;
      report.maximum_centerline_step =
        std::max (report.maximum_centerline_step, run);
      report.maximum_centerline_grade =
        std::max (report.maximum_centerline_grade, segment_grade);
      if (segment_grade > maximum_grade + 1e-5f * grade[one])
        ++report.grade_exceptions;
    }
    if (grade_distance > 0.0 * u::m)
      report.mean_centerline_grade =
        quantity_cast<grade> (accumulated_rise / grade_distance);
    report.maximum_centerline_height_above_sea = maximum_height_above_sea;
    network.earthwork_delta = std::move (earthwork_delta);
    return { .heights = std::move (shaped),
             .network = std::move (network),
             .report = report };
  }
}
