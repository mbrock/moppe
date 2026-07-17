#include <moppe/terrain/trail.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::terrain {
  namespace {
    constexpr float maximum_traversable_grade = 0.85f;

    std::string format_trail_transform_float (float value, int precision) {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision (precision) << value;
      return stream.str ();
    }

    std::string format_trail_transform_count (int value) {
      std::string text = std::to_string (value);
      for (int i = static_cast<int> (text.size ()) - 3; i > 0; i -= 3)
        text.insert (static_cast<std::size_t> (i), ",");
      return text;
    }

    [[noreturn]] void invalid_trail_transform_property_index () {
      throw std::out_of_range ("terrain transform property index is invalid");
    }

    float shoulder_ramp (float distance, float half_width, float blend) {
      if (distance <= half_width)
        return 1.0f;
      if (blend <= 0.0f)
        return 0.0f;
      const float t = std::min ((distance - half_width) / blend, 1.0f);
      const float smooth = t * t * (3.0f - 2.0f * t);
      return 1.0f - smooth;
    }

    float receiver_distance_m (std::size_t cell,
                               std::size_t receiver,
                               const TerrainGrid& grid) {
      const int width = static_cast<int> (grid.unique_width ());
      const int height = static_cast<int> (grid.unique_height ());
      int dx =
        static_cast<int> (receiver % width) - static_cast<int> (cell % width);
      int dy =
        static_cast<int> (receiver / width) - static_cast<int> (cell / width);
      if (grid.topology == Topology::Torus) {
        if (dx > width / 2)
          dx -= width;
        if (dx < -width / 2)
          dx += width;
        if (dy > height / 2)
          dy -= height;
        if (dy < -height / 2)
          dy += height;
      }
      return std::hypot (dx * grid.spacing_x_m (), dy * grid.spacing_y_m ());
    }

    TrailAlignmentPoint operator+ (TrailAlignmentPoint point,
                                   TrailAlignmentPoint offset) {
      return { point.x_m + offset.x_m, point.z_m + offset.z_m };
    }

    TrailAlignmentPoint operator- (TrailAlignmentPoint a,
                                   TrailAlignmentPoint b) {
      return { a.x_m - b.x_m, a.z_m - b.z_m };
    }

    TrailAlignmentPoint operator* (TrailAlignmentPoint point, float scale) {
      return { point.x_m * scale, point.z_m * scale };
    }

    float distance (TrailAlignmentPoint a, TrailAlignmentPoint b) {
      return std::hypot (a.x_m - b.x_m, a.z_m - b.z_m);
    }

    float wrapped_delta (float delta, float period) {
      if (period <= 0.0f)
        return delta;
      return std::remainder (delta, period);
    }

    TrailAlignmentPoint nearest_image (TrailAlignmentPoint point,
                                       TrailAlignmentPoint reference,
                                       const TerrainGrid& grid) {
      if (grid.topology != Topology::Torus)
        return point;
      const float period_x = grid.unique_width () * grid.spacing_x_m ();
      const float period_z = grid.unique_height () * grid.spacing_y_m ();
      point.x_m =
        reference.x_m + wrapped_delta (point.x_m - reference.x_m, period_x);
      point.z_m =
        reference.z_m + wrapped_delta (point.z_m - reference.z_m, period_z);
      return point;
    }

    TrailAlignmentPoint alignment_closure (const TrailAlignment& alignment,
                                           const TerrainGrid& grid) {
      if (alignment.points.empty ())
        return {};
      return nearest_image (
        alignment.points.front (), alignment.points.back (), grid);
    }

    struct AlignmentRaster {
      std::vector<float> distance_m;
      std::vector<float> signed_distance_m;
      std::vector<std::size_t> segment;
      std::vector<float> segment_t;
    };

    AlignmentRaster rasterize_alignment (const TerrainGrid& grid,
                                         const TrailAlignment& alignment,
                                         float radius) {
      const int width = static_cast<int> (grid.unique_width ());
      const int height = static_cast<int> (grid.unique_height ());
      const std::size_t count = grid.unique_size ();
      const std::size_t no_segment = alignment.points.size ();
      AlignmentRaster result {
        .distance_m =
          std::vector<float> (count, std::numeric_limits<float>::infinity ()),
        .signed_distance_m = std::vector<float> (count, 0.0f),
        .segment = std::vector<std::size_t> (count, no_segment),
        .segment_t = std::vector<float> (count, 0.0f)
      };
      if (alignment.points.size () < 2)
        return result;

      const bool periodic = grid.topology == Topology::Torus;
      const TrailAlignmentPoint closure = alignment_closure (alignment, grid);
      for (std::size_t segment = 0; segment < alignment.points.size ();
           ++segment) {
        const TrailAlignmentPoint a = alignment.points[segment];
        const TrailAlignmentPoint b = segment + 1 < alignment.points.size ()
                                        ? alignment.points[segment + 1]
                                        : closure;
        const TrailAlignmentPoint ab = b - a;
        const float length2 = ab.x_m * ab.x_m + ab.z_m * ab.z_m;
        if (length2 <= 1e-8f)
          continue;
        const int min_x = static_cast<int> (std::floor (
          (std::min (a.x_m, b.x_m) - radius) / grid.spacing_x_m ()));
        const int max_x = static_cast<int> (
          std::ceil ((std::max (a.x_m, b.x_m) + radius) / grid.spacing_x_m ()));
        const int min_y = static_cast<int> (std::floor (
          (std::min (a.z_m, b.z_m) - radius) / grid.spacing_y_m ()));
        const int max_y = static_cast<int> (
          std::ceil ((std::max (a.z_m, b.z_m) + radius) / grid.spacing_y_m ()));
        for (int unwrapped_y = min_y; unwrapped_y <= max_y; ++unwrapped_y)
          for (int unwrapped_x = min_x; unwrapped_x <= max_x; ++unwrapped_x) {
            if (!periodic && (unwrapped_x < 0 || unwrapped_x >= width ||
                              unwrapped_y < 0 || unwrapped_y >= height))
              continue;
            const TrailAlignmentPoint point { unwrapped_x * grid.spacing_x_m (),
                                              unwrapped_y *
                                                grid.spacing_y_m () };
            const TrailAlignmentPoint ap = point - a;
            const float t = std::clamp (
              (ap.x_m * ab.x_m + ap.z_m * ab.z_m) / length2, 0.0f, 1.0f);
            const TrailAlignmentPoint nearest = a + ab * t;
            const float candidate = distance (point, nearest);
            if (candidate > radius)
              continue;
            const int x =
              periodic ? wrap_index (unwrapped_x, width) : unwrapped_x;
            const int y =
              periodic ? wrap_index (unwrapped_y, height) : unwrapped_y;
            const std::size_t cell = static_cast<std::size_t> (y) * width + x;
            if (candidate < result.distance_m[cell]) {
              result.distance_m[cell] = candidate;
              const float inverse_length = 1.0f / std::sqrt (length2);
              result.signed_distance_m[cell] =
                ((point.x_m - nearest.x_m) * -ab.z_m +
                 (point.z_m - nearest.z_m) * ab.x_m) *
                inverse_length;
              result.segment[cell] = segment;
              result.segment_t[cell] = t;
            }
          }
      }
      return result;
    }

    float sample_height_m (std::span<const float> heights,
                           const TerrainGrid& grid,
                           TrailAlignmentPoint point) {
      const int width = static_cast<int> (grid.unique_width ());
      const int height = static_cast<int> (grid.unique_height ());
      float x = point.x_m / grid.spacing_x_m ();
      float y = point.z_m / grid.spacing_y_m ();
      if (grid.topology == Topology::Torus) {
        x = wrap_coordinate (x, static_cast<float> (width));
        y = wrap_coordinate (y, static_cast<float> (height));
      } else {
        x = std::clamp (x, 0.0f, static_cast<float> (width - 1));
        y = std::clamp (y, 0.0f, static_cast<float> (height - 1));
      }
      const int x0 = static_cast<int> (std::floor (x));
      const int y0 = static_cast<int> (std::floor (y));
      const int x1 = grid.topology == Topology::Torus
                       ? wrap_index (x0 + 1, width)
                       : std::min (x0 + 1, width - 1);
      const int y1 = grid.topology == Topology::Torus
                       ? wrap_index (y0 + 1, height)
                       : std::min (y0 + 1, height - 1);
      const float tx = x - x0;
      const float ty = y - y0;
      const float h0 =
        std::lerp (heights[static_cast<std::size_t> (y0) * width + x0],
                   heights[static_cast<std::size_t> (y0) * width + x1],
                   tx);
      const float h1 =
        std::lerp (heights[static_cast<std::size_t> (y1) * width + x0],
                   heights[static_cast<std::size_t> (y1) * width + x1],
                   tx);
      return std::lerp (h0, h1, ty) * grid.height_scale_m ();
    }

    struct PlanningGrid {
      struct EdgeProfile {
        float mean_grade;
        float maximum_grade;
        float maximum_achievable_grade;
        float maximum_elevation;
      };

      const TerrainView& terrain;
      const DrainageGraph& drainage;
      const FloodField& flood;
      int source_width;
      int source_height;
      int width;
      int height;
      float spacing_x;
      float spacing_y;
      float height_scale;
      bool periodic;

      int wrap_x (int x) const {
        if (periodic)
          return wrap_index (x, width);
        return std::clamp (x, 0, width - 1);
      }

      int wrap_y (int y) const {
        if (periodic)
          return wrap_index (y, height);
        return std::clamp (y, 0, height - 1);
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

      float elevation (std::size_t node) const {
        return terrain.at (source_x (x (node)), source_y (y (node))) *
               height_scale;
      }

      bool wet (std::size_t node) const {
        return flood.water_depth.values ()[source_cell (node)] > 1e-7f;
      }

      float catchment (std::size_t node) const {
        return drainage.contributing_area.values ()[source_cell (node)];
      }

      float delta_x (int from, int to) const {
        int delta = to - from;
        if (periodic && delta > width / 2)
          delta -= width;
        if (periodic && delta < -width / 2)
          delta += width;
        return static_cast<float> (delta);
      }

      float delta_y (int from, int to) const {
        int delta = to - from;
        if (periodic && delta > height / 2)
          delta -= height;
        if (periodic && delta < -height / 2)
          delta += height;
        return static_cast<float> (delta);
      }

      float distance (std::size_t a, std::size_t b) const {
        return std::hypot (delta_x (x (a), x (b)) * spacing_x,
                           delta_y (y (a), y (b)) * spacing_y);
      }

      float grade (std::size_t node) const {
        float maximum = 0.0f;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0)
              continue;
            const std::size_t neighbor =
              this->node (x (node) + dx, y (node) + dy);
            const float run = distance (node, neighbor);
            if (run > 0.0f)
              maximum = std::max (
                maximum,
                std::fabs (elevation (neighbor) - elevation (node)) / run);
          }
        return maximum;
      }

      EdgeProfile edge_profile (std::size_t from,
                                std::size_t to,
                                float earthwork_budget) const {
        const int x0 = source_x (x (from));
        const int y0 = source_y (y (from));
        int dx = source_x (x (to)) - x0;
        int dy = source_y (y (to)) - y0;
        if (periodic && dx > source_width / 2)
          dx -= source_width;
        if (periodic && dx < -source_width / 2)
          dx += source_width;
        if (periodic && dy > source_height / 2)
          dy -= source_height;
        if (periodic && dy < -source_height / 2)
          dy += source_height;
        const int steps = std::max (std::abs (dx), std::abs (dy));
        float accumulated_rise = 0.0f;
        float accumulated_run = 0.0f;
        float maximum_grade = 0.0f;
        float maximum_achievable = 0.0f;
        int previous_x = x0;
        int previous_y = y0;
        const auto source_x_at = [this] (int value) {
          return periodic ? wrap_index (value, source_width)
                          : std::clamp (value, 0, source_width - 1);
        };
        const auto source_y_at = [this] (int value) {
          return periodic ? wrap_index (value, source_height)
                          : std::clamp (value, 0, source_height - 1);
        };
        float previous_elevation =
          terrain.at (source_x_at (x0), source_y_at (y0)) * height_scale;
        float maximum_elevation = previous_elevation;
        for (int step = 1; step <= steps; ++step) {
          const float t = static_cast<float> (step) / steps;
          int current_x = x0 + static_cast<int> (std::round (dx * t));
          int current_y = y0 + static_cast<int> (std::round (dy * t));
          const float current_elevation =
            terrain.at (source_x_at (current_x), source_y_at (current_y)) *
            height_scale;
          maximum_elevation = std::max (maximum_elevation, current_elevation);
          const float run = std::hypot (
            (current_x - previous_x) * terrain.grid ().spacing_x_m (),
            (current_y - previous_y) * terrain.grid ().spacing_y_m ());
          if (run > 0.0f) {
            const float rise =
              std::fabs (current_elevation - previous_elevation);
            accumulated_rise += rise;
            accumulated_run += run;
            maximum_grade = std::max (maximum_grade, rise / run);
            maximum_achievable =
              std::max (maximum_achievable,
                        std::max (0.0f, rise - earthwork_budget) / run);
          }
          previous_x = current_x;
          previous_y = current_y;
          previous_elevation = current_elevation;
        }
        return { .mean_grade = accumulated_run > 0.0f
                                 ? accumulated_rise / accumulated_run
                                 : 0.0f,
                 .maximum_grade = maximum_grade,
                 .maximum_achievable_grade = maximum_achievable,
                 .maximum_elevation = maximum_elevation };
      }
    };

    float sea_elevation (const PlanningGrid& grid,
                         const TrailFormation& parameters) {
      return parameters.sea_level * grid.height_scale;
    }

    float preferred_route_elevation (const PlanningGrid& grid,
                                     const TrailFormation& parameters) {
      return sea_elevation (grid, parameters) +
             meters_value (parameters.highland_preference_height_above_sea);
    }

    float alpine_avoidance_elevation (const PlanningGrid& grid,
                                      const TrailFormation& parameters) {
      return sea_elevation (grid, parameters) +
             meters_value (parameters.alpine_avoidance_height_above_sea);
    }

    float highland_ratio (float elevation,
                          const PlanningGrid& grid,
                          const TrailFormation& parameters) {
      const float preferred = preferred_route_elevation (grid, parameters);
      const float avoidance = alpine_avoidance_elevation (grid, parameters);
      return std::max (0.0f, elevation - preferred) /
             std::max (avoidance - preferred, 1.0f);
    }

    PlanningGrid planning_grid (const TerrainView& terrain,
                                const DrainageGraph& drainage,
                                const FloodField& flood) {
      const TerrainGrid& source = terrain.grid ();
      const int source_width = static_cast<int> (source.unique_width ());
      const int source_height = static_cast<int> (source.unique_height ());
      constexpr float planning_spacing = 16.0f;
      const int width =
        std::clamp (static_cast<int> (std::round (
                      source_width * source.spacing_x_m () / planning_spacing)),
                    std::min (8, source_width),
                    source_width);
      const int height = std::clamp (
        static_cast<int> (std::round (source_height * source.spacing_y_m () /
                                      planning_spacing)),
        std::min (8, source_height),
        source_height);
      return { .terrain = terrain,
               .drainage = drainage,
               .flood = flood,
               .source_width = source_width,
               .source_height = source_height,
               .width = width,
               .height = height,
               .spacing_x = source_width * source.spacing_x_m () / width,
               .spacing_y = source_height * source.spacing_y_m () / height,
               .height_scale = source.height_scale_m (),
               .periodic = source.topology == Topology::Torus };
    }

    TrailAlignment
    make_alignment (const PlanningGrid& planner,
                    const std::vector<std::size_t>& coarse_circuit) {
      const TerrainGrid& source = planner.terrain.grid ();
      const float source_spacing_x = source.spacing_x_m ();
      const float source_spacing_z = source.spacing_y_m ();
      const float period_x = source.unique_width () * source_spacing_x;
      const float period_z = source.unique_height () * source_spacing_z;
      std::vector<TrailAlignmentPoint> raw;
      raw.reserve (coarse_circuit.size ());
      for (const std::size_t node : coarse_circuit) {
        TrailAlignmentPoint point {
          planner.source_x (planner.x (node)) * source_spacing_x,
          planner.source_y (planner.y (node)) * source_spacing_z
        };
        if (!raw.empty () && source.topology == Topology::Torus) {
          point.x_m = raw.back ().x_m +
                      wrapped_delta (point.x_m - raw.back ().x_m, period_x);
          point.z_m = raw.back ().z_m +
                      wrapped_delta (point.z_m - raw.back ().z_m, period_z);
        }
        if (raw.empty () || distance (raw.back (), point) > 1e-4f)
          raw.push_back (point);
      }
      if (raw.size () < 4)
        throw std::runtime_error (
          "trail circuit is too short for a smooth alignment");

      // A damped periodic cubic Hermite curve passes through the A* route
      // while rounding its eight-way corners. Short handles keep the curve
      // close to the searched corridor instead of producing Catmull-Rom-style
      // loops or large overshoots at alternating turns.
      constexpr float tangent_scale = 0.34f;
      const float sample_spacing = std::clamp (
        0.4f * std::min (source_spacing_x, source_spacing_z), 0.75f, 2.0f);
      const TrailAlignmentPoint closure =
        nearest_image (raw.front (), raw.back (), source);
      TrailAlignment alignment;
      for (std::size_t segment = 0; segment < raw.size (); ++segment) {
        const TrailAlignmentPoint a = raw[segment];
        const TrailAlignmentPoint b =
          segment + 1 < raw.size () ? raw[segment + 1] : closure;
        const TrailAlignmentPoint before =
          segment > 0 ? raw[segment - 1]
                      : nearest_image (raw.back (), a, source);
        TrailAlignmentPoint after;
        if (segment + 2 < raw.size ())
          after = raw[segment + 2];
        else if (segment + 1 < raw.size ())
          after = closure;
        else
          after = nearest_image (raw[1], b, source);
        const TrailAlignmentPoint tangent_a = (b - before) * tangent_scale;
        const TrailAlignmentPoint tangent_b = (after - a) * tangent_scale;
        const int subdivisions = std::max (
          1, static_cast<int> (std::ceil (distance (a, b) / sample_spacing)));
        for (int step = 0; step < subdivisions; ++step) {
          const float t = static_cast<float> (step) / subdivisions;
          const float t2 = t * t;
          const float t3 = t2 * t;
          const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
          const float h10 = t3 - 2.0f * t2 + t;
          const float h01 = -2.0f * t3 + 3.0f * t2;
          const float h11 = t3 - t2;
          const TrailAlignmentPoint point =
            a * h00 + tangent_a * h10 + b * h01 + tangent_b * h11;
          if (alignment.points.empty () ||
              distance (alignment.points.back (), point) > 1e-4f)
            alignment.points.push_back (point);
        }
      }
      if (alignment.points.size () < 4)
        throw std::runtime_error (
          "trail alignment sampling produced too few points");
      double length_m = 0.0;
      for (std::size_t i = 1; i < alignment.points.size (); ++i)
        length_m += distance (alignment.points[i - 1], alignment.points[i]);
      length_m += distance (alignment.points.back (),
                            alignment_closure (alignment, source));
      alignment.length = length_m * mp_units::si::metre;
      return alignment;
    }

    float nearest_water (const PlanningGrid& grid,
                         std::size_t node,
                         float search_distance) {
      const int reach = std::max (
        1,
        static_cast<int> (std::ceil (
          search_distance / std::min (grid.spacing_x, grid.spacing_y))));
      float nearest = std::numeric_limits<float>::infinity ();
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
      float score;
    };

    std::vector<HomeBaseSite>
    choose_home_bases (const PlanningGrid& grid,
                       const TrailFormation& parameters) {
      const float sea = sea_elevation (grid, parameters);
      const float target_water =
        meters_value (parameters.home_base_water_distance);
      const int shelter_reach =
        std::clamp (static_cast<int> (std::round (
                      120.0f / std::min (grid.spacing_x, grid.spacing_y))),
                    2,
                    8);
      std::vector<HomeBaseSite> candidates;
      for (std::size_t node = 0;
           node < static_cast<std::size_t> (grid.width) * grid.height;
           ++node) {
        if (grid.wet (node))
          continue;
        // The torus has no physical edge, but the home base establishes the
        // coordinate chart used by the map. Keep its visible circuit away
        // from that arbitrary cut so one loop reads as one loop in the Lab.
        if (grid.periodic && (grid.x (node) < grid.width * 0.12f ||
                              grid.x (node) > grid.width * 0.88f ||
                              grid.y (node) < grid.height * 0.12f ||
                              grid.y (node) > grid.height * 0.88f))
          continue;
        const float elevation = grid.elevation (node);
        const float above_sea = elevation - sea;
        const float slope = grid.grade (node);
        if (above_sea < meters_value (parameters.minimum_height_above_sea) ||
            slope > 0.75f)
          continue;
        const float water = nearest_water (grid, node, target_water * 2.5f);
        if (!std::isfinite (water))
          continue;

        float shelter = 0.0f;
        float distant_relief = 0.0f;
        constexpr int directions = 12;
        for (int direction = 0; direction < directions; ++direction) {
          const float angle =
            2.0f * std::numbers::pi_v<float> * direction / directions;
          const int dx =
            static_cast<int> (std::round (std::cos (angle) * shelter_reach));
          const int dy =
            static_cast<int> (std::round (std::sin (angle) * shelter_reach));
          const float rise = grid.elevation (grid.node (grid.x (node) + dx,
                                                        grid.y (node) + dy)) -
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
        const float water_score =
          36.0f - 0.20f * std::fabs (water - target_water);
        const float flat_score = 30.0f - 110.0f * slope;
        const float shelter_score =
          12.0f - 0.35f * std::fabs (std::clamp (shelter, 0.0f, 40.0f) - 16.0f);
        const float view_score =
          std::clamp (distant_relief, 0.0f, 180.0f) * 0.08f;
        const float alpine = highland_ratio (elevation, grid, parameters);
        const float alpine_penalty = 45.0f * alpine * alpine;
        const float score = water_score + flat_score + shelter_score +
                            view_score - alpine_penalty;
        candidates.push_back ({ node, score });
      }
      if (candidates.empty ())
        throw std::runtime_error ("no dry home-base site near water");
      std::ranges::sort (candidates, {}, &HomeBaseSite::score);
      std::ranges::reverse (candidates);
      std::vector<HomeBaseSite> sites;
      const float separation = std::max (
        120.0f, 0.22f * meters_value (parameters.desired_circuit_radius));
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

    std::size_t choose_scenic_focus (const PlanningGrid& grid,
                                     std::size_t base,
                                     const TrailFormation& parameters) {
      const float world_span =
        std::min (grid.width * grid.spacing_x, grid.height * grid.spacing_y);
      const float desired =
        std::clamp (meters_value (parameters.desired_circuit_radius),
                    0.12f * world_span,
                    0.36f * world_span);
      const int relief_reach = std::clamp (
        static_cast<int> (std::round (
          desired * 0.28f / std::min (grid.spacing_x, grid.spacing_y))),
        2,
        12);
      float best_score = -std::numeric_limits<float>::infinity ();
      std::size_t best = base;
      for (std::size_t node = 0;
           node < static_cast<std::size_t> (grid.width) * grid.height;
           ++node) {
        if (grid.periodic && (grid.x (node) < grid.width * 0.07f ||
                              grid.x (node) > grid.width * 0.93f ||
                              grid.y (node) < grid.height * 0.07f ||
                              grid.y (node) > grid.height * 0.93f))
          continue;
        const float distance = grid.distance (base, node);
        if (distance < 0.52f * desired || distance > 1.48f * desired)
          continue;
        const float elevation = grid.elevation (node);
        float low = elevation;
        float high = low;
        for (int dy : { -relief_reach, 0, relief_reach })
          for (int dx : { -relief_reach, 0, relief_reach }) {
            const float height = grid.elevation (
              grid.node (grid.x (node) + dx, grid.y (node) + dy));
            low = std::min (low, height);
            high = std::max (high, height);
          }
        const float distance_score =
          30.0f * (1.0f - std::fabs (distance - desired) / desired);
        const float feature_score =
          (grid.wet (node) ? 38.0f : 0.0f) +
          std::clamp (high - low, 0.0f, 160.0f) * 0.22f;
        // Relief is scenery, not an instruction to put the trail on its
        // summit. Prefer a low viewpoint beside the feature and progressively
        // distrust alpine focuses as they approach the avoidance height.
        const float alpine = highland_ratio (elevation, grid, parameters);
        const float score =
          distance_score + feature_score - 55.0f * alpine * alpine;
        if (score > best_score) {
          best_score = score;
          best = node;
        }
      }
      if (best == base)
        throw std::runtime_error ("no scenic focus for trail circuit");
      return best;
    }

    std::vector<std::uint8_t> routable_land (const PlanningGrid& grid,
                                             std::size_t start) {
      const std::size_t count =
        static_cast<std::size_t> (grid.width) * grid.height;
      std::vector<std::uint8_t> reachable (count, 0);
      if (grid.wet (start))
        return reachable;
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
            const float run = grid.distance (current, next);
            if (run <= 0.0f)
              continue;
            const float grade =
              std::fabs (grid.elevation (next) - grid.elevation (current)) /
              run;
            if (grade > maximum_traversable_grade)
              continue;
            reachable[next] = 1;
            frontier.push (next);
          }
      }
      return reachable;
    }

    std::size_t
    nearest_buildable_site (const PlanningGrid& grid,
                            float ideal_x,
                            float ideal_y,
                            int search_radius,
                            const std::vector<std::uint8_t>& reachable,
                            const TrailFormation& parameters) {
      float best_score = std::numeric_limits<float>::infinity ();
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
          const float alpine =
            highland_ratio (grid.elevation (node), grid, parameters);
          const float score =
            std::hypot (static_cast<float> (dx), static_cast<float> (dy)) +
            12.0f * grid.grade (node) + 18.0f * alpine * alpine;
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
        const float alpine =
          highland_ratio (grid.elevation (node), grid, parameters);
        const float score = grid.distance (ideal, node) /
                              std::min (grid.spacing_x, grid.spacing_y) +
                            12.0f * grid.grade (node) + 18.0f * alpine * alpine;
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

    std::vector<std::size_t>
    shortest_ride (const PlanningGrid& grid,
                   std::size_t start,
                   std::size_t goal,
                   const TrailFormation& parameters,
                   const std::vector<std::uint8_t>& avoid = {}) {
      constexpr int headings = 8;
      constexpr int no_heading = headings;
      constexpr int states_per_node = headings + 1;
      constexpr int heading_x[headings] = { -1, 0, 1, 1, 1, 0, -1, -1 };
      constexpr int heading_y[headings] = { -1, -1, -1, 0, 1, 1, 1, 0 };
      struct Candidate {
        float estimate;
        std::size_t state;
        bool operator> (const Candidate& other) const {
          return estimate > other.estimate;
        }
      };
      const std::size_t node_count =
        static_cast<std::size_t> (grid.width) * grid.height;
      const std::size_t state_count = node_count * states_per_node;
      const float infinity = std::numeric_limits<float>::infinity ();
      std::vector<float> cost (state_count, infinity);
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
      cost[start_state] = 0.0f;
      frontier.push ({ grid.distance (start, goal), start_state });
      const float maximum_grade =
        parameters.maximum_grade.numerical_value_in (mp_units::one);
      const float designed_grade =
        parameters.designed_grade.numerical_value_in (mp_units::one);
      const float earthwork_budget = meters_value (parameters.maximum_cut) +
                                     meters_value (parameters.maximum_fill);
      // Construction capacity is a feasibility limit, not a discount. A
      // larger cut/fill allowance must not make a steep edge more attractive
      // than a contour-following edge that needs less work.
      constexpr float preferred_earthwork_m = 4.0f;
      const float target_catchment =
        std::sqrt (square_meters_value (parameters.minimum_catchment_area) *
                   square_meters_value (parameters.maximum_catchment_area));
      const float direct_distance =
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
        if (current.estimate >
            cost[current.state] + grid.distance (current_node, goal) + 1e-4f)
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
          const float run = grid.distance (current_node, next);
          if (run <= 0.0f)
            continue;
          const PlanningGrid::EdgeProfile profile =
            grid.edge_profile (current_node, next, earthwork_budget);
          const float rise =
            std::fabs (grid.elevation (next) - grid.elevation (current_node));
          const float grade =
            std::max (profile.mean_grade, 0.55f * profile.maximum_grade);
          const float achievable_grade = profile.maximum_achievable_grade;
          if (achievable_grade > maximum_grade + 0.04f)
            continue;
          const float maximum_excess =
            std::max (0.0f, achievable_grade - maximum_grade) /
            std::max (maximum_grade, 0.01f);
          const float grade_scale = std::max (designed_grade, 0.01f);
          const float grade_ratio = grade / grade_scale;
          const float earthwork = std::max (0.0f, rise - designed_grade * run);
          const float earthwork_ratio = earthwork / preferred_earthwork_m;
          const float detour_ratio =
            (grid.distance (start, next) + grid.distance (next, goal)) /
            direct_distance;
          if (detour_ratio > 4.0f)
            continue;
          const float corridor = std::max (0.0f, detour_ratio - 1.12f);
          float turn = 0.0f;
          if (previous_heading != no_heading) {
            const float dot =
              (heading_x[previous_heading] * heading_x[next_heading] +
               heading_y[previous_heading] * heading_y[next_heading]) /
              std::sqrt (static_cast<float> (
                (heading_x[previous_heading] * heading_x[previous_heading] +
                 heading_y[previous_heading] * heading_y[previous_heading]) *
                (heading_x[next_heading] * heading_x[next_heading] +
                 heading_y[next_heading] * heading_y[next_heading])));
            turn = 1.0f - dot;
          }
          const float valley = std::fabs (std::log (
            std::max (grid.catchment (next), 1.0f) / target_catchment));
          const float alpine =
            highland_ratio (profile.maximum_elevation, grid, parameters);
          const int seam_distance =
            std::min ({ grid.x (next),
                        grid.width - 1 - grid.x (next),
                        grid.y (next),
                        grid.height - 1 - grid.y (next) });
          const float chart_edge =
            grid.periodic && seam_distance < 4
              ? static_cast<float> (4 - seam_distance) * 8.0f
              : 0.0f;
          const float edge =
            run * (1.0f + 0.70f * grade_ratio * grade_ratio +
                   12.0f * earthwork_ratio * earthwork_ratio +
                   30.0f * maximum_excess * maximum_excess +
                   2.5f * turn * turn + 20.0f * corridor * corridor +
                   0.10f * valley + 8.0f * alpine * alpine + chart_edge);
          const float next_cost = cost[current.state] + edge;
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
                     std::vector<std::uint8_t>& avoid) {
      const float open_radius =
        1.75f * std::max (grid.spacing_x, grid.spacing_y);
      for (const std::size_t node : path) {
        if (grid.distance (node, open_start) < open_radius ||
            grid.distance (node, open_end) < open_radius)
          continue;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
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
      float score;
    };

    std::optional<CoarseCircuitPlan>
    explore_circuit (const PlanningGrid& planner,
                     HomeBaseSite site,
                     const TrailFormation& parameters) {
      const std::size_t home_base = site.node;
      const std::vector<std::uint8_t> reachable =
        routable_land (planner, home_base);
      const std::size_t focus =
        choose_scenic_focus (planner, home_base, parameters);
      float forward_x =
        planner.delta_x (planner.x (home_base), planner.x (focus));
      float forward_y =
        planner.delta_y (planner.y (home_base), planner.y (focus));
      const float forward_length = std::hypot (forward_x, forward_y);
      forward_x /= std::max (forward_length, 1.0f);
      forward_y /= std::max (forward_length, 1.0f);
      const float flank = std::clamp (
        0.12f * std::min (planner.width, planner.height), 2.0f, 18.0f);
      const int anchor_search = std::max (2, static_cast<int> (flank * 0.55f));
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

      const std::size_t planner_cells =
        static_cast<std::size_t> (planner.width) * planner.height;
      std::vector<std::size_t> outbound =
        shortest_ride (planner, home_base, left, parameters);
      std::vector<std::uint8_t> outbound_avoid (planner_cells, 0);
      avoid_path (planner, outbound, left, far, outbound_avoid);
      append_path (
        outbound,
        shortest_ride (planner, left, far, parameters, outbound_avoid));
      if (outbound.empty () || outbound.front () != home_base ||
          outbound.back () != far)
        return std::nullopt;

      std::vector<std::uint8_t> inbound_avoid (planner_cells, 0);
      avoid_path (planner, outbound, home_base, far, inbound_avoid);
      std::vector<std::size_t> inbound =
        shortest_ride (planner, home_base, right, parameters, inbound_avoid);
      avoid_path (planner, inbound, right, far, inbound_avoid);
      append_path (
        inbound,
        shortest_ride (planner, right, far, parameters, inbound_avoid));
      if (inbound.empty () || inbound.front () != home_base ||
          inbound.back () != far)
        return std::nullopt;

      std::vector<std::size_t> circuit = outbound;
      if (inbound.size () > 2)
        for (std::size_t i = inbound.size () - 1; i-- > 1;)
          circuit.push_back (inbound[i]);

      float route_cost = 0.0f;
      for (std::size_t i = 0; i < circuit.size (); ++i) {
        const std::size_t from = circuit[i];
        const std::size_t to = circuit[(i + 1) % circuit.size ()];
        const float run = planner.distance (from, to);
        const float grade =
          run > 0.0f
            ? std::fabs (planner.elevation (to) - planner.elevation (from)) /
                run
            : 0.0f;
        const float alpine =
          highland_ratio (planner.elevation (to), planner, parameters);
        route_cost +=
          run * (1.0f + 8.0f * grade * grade + 4.0f * alpine * alpine);
      }
      return CoarseCircuitPlan { .home_base = home_base,
                                 .scenic_focus = focus,
                                 .left = left,
                                 .far = far,
                                 .right = right,
                                 .circuit = std::move (circuit),
                                 .score = route_cost - 24.0f * site.score };
    }

    std::vector<CellIndex>
    expand_circuit (const PlanningGrid& grid,
                    const std::vector<std::size_t>& coarse_circuit) {
      std::vector<CellIndex> circuit;
      std::vector<std::uint8_t> seen (
        static_cast<std::size_t> (grid.source_width) * grid.source_height, 0);
      const auto append_cell = [&circuit, &seen, &grid] (int x, int y) {
        if (grid.periodic) {
          x = wrap_index (x, grid.source_width);
          y = wrap_index (y, grid.source_height);
        } else {
          x = std::clamp (x, 0, grid.source_width - 1);
          y = std::clamp (y, 0, grid.source_height - 1);
        }
        const std::size_t cell =
          static_cast<std::size_t> (y) * grid.source_width + x;
        if (!seen[cell]) {
          seen[cell] = 1;
          circuit.emplace_back (static_cast<std::uint32_t> (cell));
        }
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
        if (grid.periodic && dx > grid.source_width / 2)
          dx -= grid.source_width;
        if (grid.periodic && dx < -grid.source_width / 2)
          dx += grid.source_width;
        if (grid.periodic && dy > grid.source_height / 2)
          dy -= grid.source_height;
        if (grid.periodic && dy < -grid.source_height / 2)
          dy += grid.source_height;
        const int steps = std::max (std::abs (dx), std::abs (dy));
        for (int step = 0; step < steps; ++step) {
          const float t = steps ? static_cast<float> (step) / steps : 0.0f;
          append_cell (x0 + static_cast<int> (std::round (dx * t)),
                       y0 + static_cast<int> (std::round (dy * t)));
        }
      }
      return circuit;
    }

    bool circuit_is_contiguous (const TerrainGrid& grid,
                                const std::vector<CellIndex>& circuit) {
      if (circuit.size () < 2)
        return false;
      const int width = static_cast<int> (grid.unique_width ());
      const int height = static_cast<int> (grid.unique_height ());
      for (std::size_t i = 0; i < circuit.size (); ++i) {
        const CellIndex from = circuit[i];
        const CellIndex to = circuit[(i + 1) % circuit.size ()];
        int dx = std::abs (static_cast<int> (from.value % width) -
                           static_cast<int> (to.value % width));
        int dy = std::abs (static_cast<int> (from.value / width) -
                           static_cast<int> (to.value / width));
        if (grid.topology == Topology::Torus) {
          dx = std::min (dx, width - dx);
          dy = std::min (dy, height - dy);
        }
        if ((dx == 0 && dy == 0) || dx > 1 || dy > 1)
          return false;
      }
      return true;
    }
  }

  void TrailFormation::validate () const {
    if (!std::isfinite (sea_level) ||
        !std::isfinite (square_meters_value (minimum_catchment_area)) ||
        !std::isfinite (square_meters_value (maximum_catchment_area)) ||
        minimum_catchment_area <=
          0.0f * mp_units::si::metre * mp_units::si::metre ||
        maximum_catchment_area < minimum_catchment_area ||
        !std::isfinite (meters_value (minimum_height_above_sea)) ||
        minimum_height_above_sea < 0.0f * mp_units::si::metre ||
        !std::isfinite (meters_value (width)) ||
        width <= 0.0f * mp_units::si::metre ||
        !std::isfinite (meters_value (shoulder_blend)) ||
        shoulder_blend < 0.0f * mp_units::si::metre ||
        !std::isfinite (meters_value (maximum_cut)) ||
        maximum_cut < 0.0f * mp_units::si::metre ||
        !std::isfinite (meters_value (maximum_fill)) ||
        maximum_fill < 0.0f * mp_units::si::metre ||
        !std::isfinite (maximum_grade.numerical_value_in (mp_units::one)) ||
        maximum_grade < 0.0f * terrain_slope[mp_units::one] ||
        !std::isfinite (designed_grade.numerical_value_in (mp_units::one)) ||
        designed_grade < 0.0f * terrain_slope[mp_units::one] ||
        designed_grade > maximum_grade ||
        !std::isfinite (crossfall.numerical_value_in (mp_units::one)) ||
        crossfall < 0.0f * terrain_slope[mp_units::one] ||
        crossfall > 0.1f * terrain_slope[mp_units::one] ||
        grading_iterations < 0 ||
        !std::isfinite (meters_value (home_base_water_distance)) ||
        home_base_water_distance <= 0.0f * mp_units::si::metre ||
        !std::isfinite (meters_value (home_base_pad_radius)) ||
        home_base_pad_radius <= 0.0f * mp_units::si::metre ||
        !std::isfinite (meters_value (desired_circuit_radius)) ||
        desired_circuit_radius <= 0.0f * mp_units::si::metre ||
        !std::isfinite (meters_value (highland_preference_height_above_sea)) ||
        highland_preference_height_above_sea <= 0.0f * mp_units::si::metre ||
        !std::isfinite (meters_value (alpine_avoidance_height_above_sea)) ||
        alpine_avoidance_height_above_sea <
          highland_preference_height_above_sea)
      throw std::invalid_argument ("trail formation parameters are invalid");
  }

  TransformDescription TrailFormation::description () const noexcept {
    return { "trails",
             "MOTORCYCLE CIRCUIT",
             { SpatialScope::Global, EvaluationOrder::Iterative } };
  }

  std::string TrailFormation::detail () const {
    return format_trail_transform_float (
             meters_value (desired_circuit_radius) / 1000.0f, 1) +
           " km radius / " +
           format_trail_transform_float (meters_value (width), 1) +
           " m path / " +
           format_trail_transform_float (
             designed_grade.numerical_value_in (mp_units::one) * 100.0f, 0) +
           "% design / " +
           format_trail_transform_float (
             maximum_grade.numerical_value_in (mp_units::one) * 100.0f, 0) +
           "% max / " +
           format_trail_transform_float (
             crossfall.numerical_value_in (mp_units::one) * 100.0f, 0) +
           "% crossfall / " +
           format_trail_transform_float (
             meters_value (alpine_avoidance_height_above_sea), 0) +
           " m alpine avoid";
  }

  std::size_t TrailFormation::property_count () const noexcept {
    return 14;
  }

  TransformProperty TrailFormation::property (std::size_t index) const {
    switch (index) {
    case 0:
      return { "MIN CATCHMENT (M2)",
               format_trail_transform_count (static_cast<int> (
                 square_meters_value (minimum_catchment_area))),
               ParameterDomain::Continuous };
    case 1:
      return { "MAX CATCHMENT (M2)",
               format_trail_transform_count (static_cast<int> (
                 square_meters_value (maximum_catchment_area))),
               ParameterDomain::Continuous };
    case 2:
      return { "PATH WIDTH (M)",
               format_trail_transform_float (meters_value (width), 1),
               ParameterDomain::Continuous };
    case 3:
      return { "SHOULDER (M)",
               format_trail_transform_float (meters_value (shoulder_blend), 1),
               ParameterDomain::Continuous };
    case 4:
      return { "MAX CUT (M)",
               format_trail_transform_float (meters_value (maximum_cut), 2),
               ParameterDomain::Continuous };
    case 5:
      return { "MAX FILL (M)",
               format_trail_transform_float (meters_value (maximum_fill), 2),
               ParameterDomain::Continuous };
    case 6:
      return { "MAX GRADE",
               format_trail_transform_float (
                 maximum_grade.numerical_value_in (mp_units::one), 2),
               ParameterDomain::Continuous };
    case 7:
      return { "DESIGNED GRADE",
               format_trail_transform_float (
                 designed_grade.numerical_value_in (mp_units::one), 2),
               ParameterDomain::Continuous };
    case 8:
      return { "BASE TO WATER (M)",
               format_trail_transform_float (
                 meters_value (home_base_water_distance), 0),
               ParameterDomain::Continuous };
    case 9:
      return { "BASE PAD (M)",
               format_trail_transform_float (
                 meters_value (home_base_pad_radius), 0),
               ParameterDomain::Continuous };
    case 10:
      return { "CIRCUIT RADIUS (M)",
               format_trail_transform_float (
                 meters_value (desired_circuit_radius), 0),
               ParameterDomain::Continuous };
    case 11:
      return { "HIGHLAND START (M)",
               format_trail_transform_float (
                 meters_value (highland_preference_height_above_sea), 0),
               ParameterDomain::Continuous };
    case 12:
      return { "ALPINE AVOID (M)",
               format_trail_transform_float (
                 meters_value (alpine_avoidance_height_above_sea), 0),
               ParameterDomain::Continuous };
    case 13:
      return { "CROSSFALL",
               format_trail_transform_float (
                 crossfall.numerical_value_in (mp_units::one), 2),
               ParameterDomain::Continuous };
    default:
      invalid_trail_transform_property_index ();
    }
  }

  TrailNetwork analyze_trail_network (const TerrainView& terrain,
                                      const TrailFormation& parameters,
                                      const DrainageGraph& drainage,
                                      const FloodField& flood) {
    MOPPE_PROFILE_ZONE ("analyze_trail_network");
    parameters.validate ();
    const TerrainGrid& grid = terrain.grid ();
    const int width = static_cast<int> (grid.unique_width ());
    const int height = static_cast<int> (grid.unique_height ());
    const std::size_t count = grid.unique_size ();
    const bool periodic = grid.topology == Topology::Torus;
    if (drainage.width () != static_cast<std::size_t> (width) ||
        drainage.height () != static_cast<std::size_t> (height) ||
        flood.width () != static_cast<std::size_t> (width) ||
        flood.height () != static_cast<std::size_t> (height))
      throw std::invalid_argument (
        "trail network readings do not match terrain");

    const PlanningGrid planner = planning_grid (terrain, drainage, flood);
    std::optional<CoarseCircuitPlan> chosen;
    std::vector<CellIndex> chosen_cells;
    for (const HomeBaseSite site : choose_home_bases (planner, parameters)) {
      try {
        std::optional<CoarseCircuitPlan> expedition =
          explore_circuit (planner, site, parameters);
        if (!expedition || (chosen && expedition->score >= chosen->score))
          continue;
        std::vector<CellIndex> cells =
          expand_circuit (planner, expedition->circuit);
        if (cells.size () >= 4 && circuit_is_contiguous (grid, cells)) {
          chosen = std::move (expedition);
          chosen_cells = std::move (cells);
        }
      } catch (const std::runtime_error&) {
        // An expedition can fail to find a focus while another base succeeds.
      }
    }
    if (!chosen)
      throw std::runtime_error (
        "no home-base expedition found a complete trail circuit");
    const std::size_t home_base = chosen->home_base;
    const std::size_t focus = chosen->scenic_focus;
    const std::size_t left = chosen->left;
    const std::size_t far = chosen->far;
    const std::size_t right = chosen->right;
    TrailAlignment alignment = make_alignment (planner, chosen->circuit);
    std::vector<CellIndex> cells = std::move (chosen_cells);

    std::vector<CellIndex> receiver (count, no_cell);
    std::vector<TrailComponentId> component_by_cell (count, no_trail_component);
    const TrailComponentId component { 0 };
    for (std::size_t i = 0; i < cells.size (); ++i) {
      receiver[cells[i].value] = cells[(i + 1) % cells.size ()];
      component_by_cell[cells[i].value] = component;
    }
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
                     .circuit = cells };
    const CellCount circuit_cells = cell_count (cells.size ());

    std::vector<float> influence (count, 0.0f);
    std::vector<float> home_base_influence (count, 0.0f);
    // Unlike the constructed height stamp, the material footprint keeps its
    // authored width. Sampling a distance to the continuous alignment makes
    // a narrow path legible without inflating its core to a whole grid cell.
    const float half_width = 0.5f * meters_value (parameters.width);
    const float blend = meters_value (parameters.shoulder_blend);
    const float radius = half_width + blend;
    const AlignmentRaster material_raster =
      rasterize_alignment (grid, alignment, radius);
    for (std::size_t cell = 0; cell < count; ++cell)
      if (flood.water_depth.values ()[cell] <= 1e-7f &&
          material_raster.distance_m[cell] < radius)
        influence[cell] =
          shoulder_ramp (material_raster.distance_m[cell], half_width, blend);

    const float base_radius = meters_value (parameters.home_base_pad_radius);
    const float base_blend = std::max (6.0f, 0.45f * base_radius);
    const int base_reach_x =
      static_cast<int> (
        std::ceil ((base_radius + base_blend) / grid.spacing_x_m ())) +
      1;
    const int base_reach_y =
      static_cast<int> (
        std::ceil ((base_radius + base_blend) / grid.spacing_y_m ())) +
      1;
    const int base_x = static_cast<int> (home_base_cell.value % width);
    const int base_y = static_cast<int> (home_base_cell.value / width);
    for (int dy = -base_reach_y; dy <= base_reach_y; ++dy)
      for (int dx = -base_reach_x; dx <= base_reach_x; ++dx) {
        int x = base_x + dx;
        int y = base_y + dy;
        if (periodic) {
          x = wrap_index (x, width);
          y = wrap_index (y, height);
        } else if (x < 0 || x >= width || y < 0 || y >= height)
          continue;
        const float distance =
          std::hypot (dx * grid.spacing_x_m (), dy * grid.spacing_y_m ());
        const std::size_t cell = static_cast<std::size_t> (y) * width + x;
        if (flood.water_depth.values ()[cell] <= 1e-7f)
          home_base_influence[cell] =
            shoulder_ramp (distance, base_radius, base_blend);
      }

    const FieldSamplingGrid2D domain { .width =
                                         static_cast<std::size_t> (width),
                                       .height =
                                         static_cast<std::size_t> (height),
                                       .max_x = grid.spacing_x_m () * width,
                                       .max_y = grid.spacing_y_m () * height };
    return { .source_grid = grid,
             .plan = std::move (plan),
             .receiver = std::move (receiver),
             .component_by_cell = std::move (component_by_cell),
             .cells = std::move (cells),
             .components = { { .id = component,
                               .anchor_cell = home_base_cell,
                               .cells = circuit_cells } },
             .alignment = std::move (alignment),
             .formed_width = parameters.width,
             .earthwork_delta_m = std::vector<float> (count, 0.0f),
             .influence = ScalarRaster (domain, std::move (influence)),
             .home_base_influence =
               ScalarRaster (domain, std::move (home_base_influence)) };
  }

  TrailNetwork analyze_trail_network (const TerrainView& terrain,
                                      const TrailFormation& parameters) {
    const DrainageGraph drainage = analyze_drainage (terrain);
    const FloodField flood =
      analyze_standing_water (terrain, parameters.sea_level);
    return analyze_trail_network (terrain, parameters, drainage, flood);
  }

  std::vector<float> expand_trail_influence (const TrailNetwork& network) {
    const std::size_t width = network.source_grid.width;
    const std::size_t height = network.source_grid.height;
    const std::size_t unique_width = network.source_grid.unique_width ();
    const std::size_t unique_height = network.source_grid.unique_height ();
    if (network.influence.values ().size () != unique_width * unique_height)
      throw std::invalid_argument (
        "trail influence does not match its source grid");
    std::vector<float> expanded (width * height);
    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x)
        expanded[y * width + x] =
          network.influence
            .values ()[(y % unique_height) * unique_width + x % unique_width];
    return expanded;
  }

  std::vector<float> expand_home_base_influence (const TrailNetwork& network) {
    const std::size_t width = network.source_grid.width;
    const std::size_t height = network.source_grid.height;
    const std::size_t unique_width = network.source_grid.unique_width ();
    const std::size_t unique_height = network.source_grid.unique_height ();
    if (network.home_base_influence.values ().size () !=
        unique_width * unique_height)
      throw std::invalid_argument (
        "home base influence does not match its source grid");
    std::vector<float> expanded (width * height);
    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x)
        expanded[y * width + x] =
          network.home_base_influence
            .values ()[(y % unique_height) * unique_width + x % unique_width];
    return expanded;
  }

  meters_f64_t trail_circuit_length (const TrailNetwork& network) {
    if (network.alignment.length > 0.0 * mp_units::si::metre)
      return network.alignment.length;
    meters_f64_t length = 0.0 * mp_units::si::metre;
    for (const CellIndex cell : network.cells) {
      const CellIndex next = network.receiver[cell.value];
      if (next != no_cell)
        length +=
          receiver_distance_m (cell.value, next.value, network.source_grid) *
          mp_units::si::metre;
    }
    return length;
  }

  TrailFormationResult form_trails (const TerrainView& terrain,
                                    const TrailFormation& parameters) {
    MOPPE_PROFILE_ZONE ("form_trails");
    parameters.validate ();
    const TerrainGrid& grid = terrain.grid ();
    const int width = static_cast<int> (grid.unique_width ());
    const int height = static_cast<int> (grid.unique_height ());
    const std::size_t count = grid.unique_size ();
    const float height_scale = grid.height_scale_m ();
    const float cell_area = square_meters_value (grid.cell_area ());

    std::vector<float> original (count);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        original[static_cast<std::size_t> (y) * width + x] = terrain.at (x, y);

    const DrainageGraph drainage = analyze_drainage (terrain);
    const FloodField flood =
      analyze_standing_water (terrain, parameters.sea_level);
    TrailNetwork network =
      analyze_trail_network (terrain, parameters, drainage, flood);

    // Solve the vertical profile along arc-length samples of the continuous
    // alignment. Horizontal intent and longitudinal grade are deliberately
    // separate: the former says where the trail goes, while this pass says
    // what the bulldozer can build there within bounded cut and fill.
    const std::size_t alignment_count = network.alignment.points.size ();
    std::vector<float> original_profile_m (alignment_count);
    for (std::size_t point = 0; point < alignment_count; ++point)
      original_profile_m[point] =
        sample_height_m (original, grid, network.alignment.points[point]);
    std::vector<float> grade_m = original_profile_m;
    const float maximum_grade =
      parameters.maximum_grade.numerical_value_in (mp_units::one);
    const float maximum_cut = meters_value (parameters.maximum_cut);
    const float maximum_fill = meters_value (parameters.maximum_fill);
    for (int iteration = 0;
         iteration < count_value (parameters.grading_iterations);
         ++iteration) {
      for (std::size_t point = 0; point < alignment_count; ++point) {
        const std::size_t next = (point + 1) % alignment_count;
        const TrailAlignmentPoint next_position =
          next ? network.alignment.points[next]
               : alignment_closure (network.alignment, grid);
        const float allowed =
          maximum_grade *
          distance (network.alignment.points[point], next_position);
        const float difference = grade_m[point] - grade_m[next];
        if (std::fabs (difference) <= allowed)
          continue;
        const float excess = std::fabs (difference) - allowed;
        const float direction = difference > 0.0f ? 1.0f : -1.0f;
        grade_m[point] -= direction * 0.5f * excess;
        grade_m[next] += direction * 0.5f * excess;
        grade_m[point] = std::clamp (grade_m[point],
                                     original_profile_m[point] - maximum_cut,
                                     original_profile_m[point] + maximum_fill);
        grade_m[next] = std::clamp (grade_m[next],
                                    original_profile_m[next] - maximum_cut,
                                    original_profile_m[next] + maximum_fill);
      }
    }

    // Raster construction is derived from the nearest point on that same
    // alignment. The geometric core still grows to one source cell at coarse
    // resolutions so collision remains a continuous formed surface; the
    // independently generated material footprint retains the authored width.
    const float authored_half_width = 0.5f * meters_value (parameters.width);
    const float half_width =
      std::max (authored_half_width,
                1.01f * std::max (grid.spacing_x_m (), grid.spacing_y_m ()));
    const float blend = meters_value (parameters.shoulder_blend);
    const float radius = half_width + blend;
    const AlignmentRaster formation_raster =
      rasterize_alignment (grid, network.alignment, radius);

    // Pick the naturally lower side along the alignment, falling back to the
    // loop exterior where the ground is effectively level. The crossfall is
    // clamped to the authored tread width; a coarse preview may widen the
    // construction stamp for continuity, but must not magnify a
    // three-centimetres-per-metre drainage slope into a large terrace.
    const float crossfall =
      parameters.crossfall.numerical_value_in (mp_units::one);
    const float probe_distance =
      std::max (authored_half_width,
                0.5f * std::min (grid.spacing_x_m (), grid.spacing_y_m ()));
    std::vector<float> downhill_side (alignment_count, 1.0f);
    double signed_area = 0.0;
    for (std::size_t segment = 0; segment < alignment_count; ++segment) {
      const TrailAlignmentPoint a = network.alignment.points[segment];
      const TrailAlignmentPoint b =
        segment + 1 < alignment_count
          ? network.alignment.points[segment + 1]
          : alignment_closure (network.alignment, grid);
      signed_area += static_cast<double> (a.x_m) * b.z_m -
                     static_cast<double> (b.x_m) * a.z_m;
    }
    const float exterior_side = signed_area >= 0.0 ? -1.0f : 1.0f;
    for (std::size_t segment = 0; segment < alignment_count; ++segment) {
      const TrailAlignmentPoint a = network.alignment.points[segment];
      const TrailAlignmentPoint b =
        segment + 1 < alignment_count
          ? network.alignment.points[segment + 1]
          : alignment_closure (network.alignment, grid);
      const TrailAlignmentPoint along = b - a;
      const float segment_length = distance (a, b);
      if (segment_length <= 1e-5f)
        continue;
      const TrailAlignmentPoint midpoint = a + along * 0.5f;
      const TrailAlignmentPoint normal { -along.z_m / segment_length,
                                         along.x_m / segment_length };
      const float positive_height =
        sample_height_m (original, grid, midpoint + normal * probe_distance);
      const float negative_height =
        sample_height_m (original, grid, midpoint + normal * -probe_distance);
      const float natural_difference = negative_height - positive_height;
      const float natural_side = natural_difference >= 0.0f ? 1.0f : -1.0f;
      const float transition = std::clamp (
        (std::fabs (natural_difference) - 0.05f) / 0.20f, 0.0f, 1.0f);
      const float smooth_transition =
        transition * transition * (3.0f - 2.0f * transition);
      // On an unambiguously sloped sidehill, drain with gravity. Around a
      // ridge, saddle, or nearly level patch, ease toward the loop exterior
      // instead of letting tiny height noise flip the crossfall every couple
      // of metres.
      downhill_side[segment] =
        std::lerp (exterior_side, natural_side, smooth_transition);
    }

    std::vector<float> shaped = original;
    std::vector<float> earthwork_delta_m (count, 0.0f);
    TrailFormationReport report;
    report.centerline_cells = cell_count (network.cells.size ());
    report.connected_components = network.components.size ();
    report.circuit_length = trail_circuit_length (network);
    double absolute_change = 0.0;
    double maximum_change = 0.0;
    double cut_volume = 0.0;
    double fill_volume = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell) {
      if (formation_raster.segment[cell] >= alignment_count ||
          flood.water_depth.values ()[cell] > 1e-7f)
        continue;
      const float original_m = original[cell] * height_scale;
      const std::size_t segment = formation_raster.segment[cell];
      const std::size_t next = (segment + 1) % alignment_count;
      const float centerline_m = std::lerp (
        grade_m[segment], grade_m[next], formation_raster.segment_t[cell]);
      const float across_m =
        std::clamp (formation_raster.signed_distance_m[cell],
                    -authored_half_width,
                    authored_half_width);
      const float target_m =
        centerline_m - crossfall * downhill_side[segment] * across_m;
      const float influence =
        shoulder_ramp (formation_raster.distance_m[cell], half_width, blend);
      const float shaped_m =
        std::clamp (original_m + influence * (target_m - original_m),
                    original_m - maximum_cut,
                    original_m + maximum_fill);
      const double change = shaped_m - original_m;
      if (std::fabs (change) <= 1e-6)
        continue;
      shaped[cell] = shaped_m / height_scale;
      earthwork_delta_m[cell] = static_cast<float> (change);
      ++report.shaped_cells;
      absolute_change += std::fabs (change);
      maximum_change = std::max (maximum_change, std::fabs (change));
      if (change < 0.0)
        cut_volume += -change * cell_area;
      else
        fill_volume += change * cell_area;
    }
    report.cut_volume = cut_volume * mp_units::si::metre * mp_units::si::metre *
                        mp_units::si::metre;
    report.fill_volume = fill_volume * mp_units::si::metre *
                         mp_units::si::metre * mp_units::si::metre;
    report.maximum_absolute_change = maximum_change * mp_units::si::metre;
    if (report.shaped_cells)
      report.mean_absolute_change = absolute_change /
                                    count_value (report.shaped_cells) *
                                    mp_units::si::metre;
    double grade_distance = 0.0;
    double accumulated_rise = 0.0;
    double maximum_height_above_sea = 0.0;
    const double sea_m = parameters.sea_level * height_scale;
    for (std::size_t point = 0; point < alignment_count; ++point) {
      const std::size_t next = (point + 1) % alignment_count;
      const TrailAlignmentPoint next_position =
        next ? network.alignment.points[next]
             : alignment_closure (network.alignment, grid);
      const double run =
        distance (network.alignment.points[point], next_position);
      const double elevation_m =
        sample_height_m (shaped, grid, network.alignment.points[point]);
      maximum_height_above_sea =
        std::max (maximum_height_above_sea, elevation_m - sea_m);
      if (run <= 0.0)
        continue;
      const double rise = std::fabs (
        sample_height_m (shaped, grid, network.alignment.points[point]) -
        sample_height_m (shaped, grid, next_position));
      const double grade = rise / run;
      grade_distance += run;
      accumulated_rise += rise;
      report.maximum_centerline_step =
        std::max (meters_value (report.maximum_centerline_step), run) *
        mp_units::si::metre;
      report.maximum_centerline_grade =
        std::max (report.maximum_centerline_grade, grade);
      if (grade > maximum_grade + 1e-5f)
        ++report.grade_exceptions;
    }
    if (grade_distance > 0.0)
      report.mean_centerline_grade = accumulated_rise / grade_distance;
    report.maximum_centerline_height_above_sea =
      maximum_height_above_sea * mp_units::si::metre;
    network.earthwork_delta_m = std::move (earthwork_delta_m);
    return { .heights = std::move (shaped),
             .network = std::move (network),
             .report = report };
  }
}
