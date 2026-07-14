#include <moppe/terrain/trail.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::terrain {
  namespace {
    constexpr float maximum_traversable_grade = 0.85f;

    void validate (const TrailFormation& p) {
      if (!std::isfinite (p.sea_level) ||
          !std::isfinite (square_meters_value (p.minimum_catchment_area)) ||
          !std::isfinite (square_meters_value (p.maximum_catchment_area)) ||
          p.minimum_catchment_area <=
            0.0f * mp_units::si::metre * mp_units::si::metre ||
          p.maximum_catchment_area < p.minimum_catchment_area ||
          !std::isfinite (meters_value (p.minimum_height_above_sea)) ||
          p.minimum_height_above_sea < 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.width)) ||
          p.width <= 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.shoulder_blend)) ||
          p.shoulder_blend < 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.maximum_cut)) ||
          p.maximum_cut < 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.maximum_fill)) ||
          p.maximum_fill < 0.0f * mp_units::si::metre ||
          !std::isfinite (p.maximum_grade.numerical_value_in (mp_units::one)) ||
          p.maximum_grade < 0.0f * terrain_slope[mp_units::one] ||
          !std::isfinite (
            p.designed_grade.numerical_value_in (mp_units::one)) ||
          p.designed_grade < 0.0f * terrain_slope[mp_units::one] ||
          p.designed_grade > p.maximum_grade || p.grading_iterations < 0 ||
          !std::isfinite (meters_value (p.home_base_water_distance)) ||
          p.home_base_water_distance <= 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.home_base_pad_radius)) ||
          p.home_base_pad_radius <= 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.desired_circuit_radius)) ||
          p.desired_circuit_radius <= 0.0f * mp_units::si::metre)
        throw std::invalid_argument ("invalid trail formation parameters");
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

    struct PlanningGrid {
      struct EdgeProfile {
        float mean_grade;
        float maximum_grade;
        float maximum_achievable_grade;
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
        for (int step = 1; step <= steps; ++step) {
          const float t = static_cast<float> (step) / steps;
          int current_x = x0 + static_cast<int> (std::round (dx * t));
          int current_y = y0 + static_cast<int> (std::round (dy * t));
          const float current_elevation =
            terrain.at (source_x_at (current_x), source_y_at (current_y)) *
            height_scale;
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
                 .maximum_achievable_grade = maximum_achievable };
      }
    };

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
      const float sea = parameters.sea_level * grid.height_scale;
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
        const float alpine_penalty =
          std::max (0.0f, above_sea - 180.0f) * 0.08f;
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
        float low = grid.elevation (node);
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
        const float score = distance_score + feature_score;
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
                            const std::vector<std::uint8_t>& reachable) {
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
          const float score =
            std::hypot (static_cast<float> (dx), static_cast<float> (dy)) +
            12.0f * grid.grade (node);
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
        const float score = grid.distance (ideal, node) /
                              std::min (grid.spacing_x, grid.spacing_y) +
                            12.0f * grid.grade (node);
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
            run *
            (1.0f + 0.70f * grade_ratio * grade_ratio +
             12.0f * earthwork_ratio * earthwork_ratio +
             30.0f * maximum_excess * maximum_excess + 2.5f * turn * turn +
             20.0f * corridor * corridor + 0.10f * valley + chart_edge);
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
                                reachable);
      const std::size_t far =
        nearest_buildable_site (planner,
                                focus_x + forward_x * flank,
                                focus_y + forward_y * flank,
                                anchor_search,
                                reachable);
      const std::size_t right =
        nearest_buildable_site (planner,
                                focus_x + forward_y * flank,
                                focus_y - forward_x * flank,
                                anchor_search,
                                reachable);

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
        route_cost += run * (1.0f + 8.0f * grade * grade);
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

  TrailNetwork analyze_trail_network (const TerrainView& terrain,
                                      const TrailFormation& parameters,
                                      const DrainageGraph& drainage,
                                      const FloodField& flood) {
    MOPPE_PROFILE_ZONE ("analyze_trail_network");
    validate (parameters);
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
    const float half_width =
      std::max (0.5f * meters_value (parameters.width),
                1.01f * std::max (grid.spacing_x_m (), grid.spacing_y_m ()));
    const float blend = meters_value (parameters.shoulder_blend);
    const float radius = half_width + blend;
    constexpr int stamp_limit_cells = 64;
    const int reach_x = std::min (
      stamp_limit_cells,
      static_cast<int> (std::ceil (radius / grid.spacing_x_m ())) + 1);
    const int reach_y = std::min (
      stamp_limit_cells,
      static_cast<int> (std::ceil (radius / grid.spacing_y_m ())) + 1);
    for (const CellIndex center : cells) {
      const int cx = static_cast<int> (center.value % width);
      const int cy = static_cast<int> (center.value / width);
      for (int dy = -reach_y; dy <= reach_y; ++dy)
        for (int dx = -reach_x; dx <= reach_x; ++dx) {
          int x = cx + dx;
          int y = cy + dy;
          if (periodic) {
            x = wrap_index (x, width);
            y = wrap_index (y, height);
          } else if (x < 0 || x >= width || y < 0 || y >= height)
            continue;
          const float distance =
            std::hypot (dx * grid.spacing_x_m (), dy * grid.spacing_y_m ());
          if (distance >= radius)
            continue;
          const std::size_t cell = static_cast<std::size_t> (y) * width + x;
          if (flood.water_depth.values ()[cell] > 1e-7f)
            continue;
          influence[cell] = std::max (
            influence[cell], shoulder_ramp (distance, half_width, blend));
        }
    }

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
    validate (parameters);
    const TerrainGrid& grid = terrain.grid ();
    const int width = static_cast<int> (grid.unique_width ());
    const int height = static_cast<int> (grid.unique_height ());
    const std::size_t count = grid.unique_size ();
    const bool periodic = grid.topology == Topology::Torus;
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

    // Project every selected drainage edge toward the requested grade. Both
    // endpoints move, but their own earthwork limits prevent the path from
    // replacing the shape of the valley with an engineered road cutting.
    std::vector<float> grade_m (count);
    for (std::size_t cell = 0; cell < count; ++cell)
      grade_m[cell] = original[cell] * height_scale;
    const float maximum_grade =
      parameters.maximum_grade.numerical_value_in (mp_units::one);
    const float maximum_cut = meters_value (parameters.maximum_cut);
    const float maximum_fill = meters_value (parameters.maximum_fill);
    for (int iteration = 0;
         iteration < count_value (parameters.grading_iterations);
         ++iteration) {
      for (std::size_t cell = 0; cell < count; ++cell) {
        const CellIndex receiver = network.receiver[cell];
        if (!network.contains (
              CellIndex { static_cast<std::uint32_t> (cell) }) ||
            receiver == no_cell)
          continue;
        const std::size_t next = receiver.value;
        const float allowed =
          maximum_grade * receiver_distance_m (cell, next, grid);
        const float difference = grade_m[cell] - grade_m[next];
        if (std::fabs (difference) <= allowed)
          continue;
        const float excess = std::fabs (difference) - allowed;
        const float direction = difference > 0.0f ? 1.0f : -1.0f;
        grade_m[cell] -= direction * 0.5f * excess;
        grade_m[next] += direction * 0.5f * excess;
        grade_m[cell] =
          std::clamp (grade_m[cell],
                      original[cell] * height_scale - maximum_cut,
                      original[cell] * height_scale + maximum_fill);
        grade_m[next] =
          std::clamp (grade_m[next],
                      original[next] * height_scale - maximum_cut,
                      original[next] * height_scale + maximum_fill);
      }
    }

    // A weighted stamp makes confluences independent of traversal order. At
    // coarse preview resolutions the effective core grows just enough for
    // adjacent D8 samples to remain a continuous ribbon.
    std::vector<float> target_sum (count, 0.0f);
    std::vector<float> influence_sum (count, 0.0f);
    const float half_width =
      std::max (0.5f * meters_value (parameters.width),
                1.01f * std::max (grid.spacing_x_m (), grid.spacing_y_m ()));
    const float blend = meters_value (parameters.shoulder_blend);
    const float radius = half_width + blend;
    constexpr int stamp_limit_cells = 64;
    const int reach_x = std::min (
      stamp_limit_cells,
      static_cast<int> (std::ceil (radius / grid.spacing_x_m ())) + 1);
    const int reach_y = std::min (
      stamp_limit_cells,
      static_cast<int> (std::ceil (radius / grid.spacing_y_m ())) + 1);
    for (const CellIndex center : network.cells) {
      const int cx = static_cast<int> (center.value % width);
      const int cy = static_cast<int> (center.value / width);
      for (int dy = -reach_y; dy <= reach_y; ++dy)
        for (int dx = -reach_x; dx <= reach_x; ++dx) {
          int x = cx + dx;
          int y = cy + dy;
          if (periodic) {
            x = wrap_index (x, width);
            y = wrap_index (y, height);
          } else if (x < 0 || x >= width || y < 0 || y >= height)
            continue;
          const float distance =
            std::hypot (dx * grid.spacing_x_m (), dy * grid.spacing_y_m ());
          if (distance >= radius)
            continue;
          const std::size_t cell = static_cast<std::size_t> (y) * width + x;
          if (flood.water_depth.values ()[cell] > 1e-7f)
            continue;
          const float influence = shoulder_ramp (distance, half_width, blend);
          target_sum[cell] += influence * grade_m[center.value];
          influence_sum[cell] += influence;
        }
    }

    std::vector<float> shaped = original;
    TrailFormationReport report;
    report.centerline_cells = cell_count (network.cells.size ());
    report.connected_components = network.components.size ();
    report.circuit_length = trail_circuit_length (network);
    double absolute_change = 0.0;
    double maximum_change = 0.0;
    double cut_volume = 0.0;
    double fill_volume = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell) {
      if (influence_sum[cell] <= 0.0f)
        continue;
      const float original_m = original[cell] * height_scale;
      const float target_m = target_sum[cell] / influence_sum[cell];
      const float influence = std::min (influence_sum[cell], 1.0f);
      const float shaped_m =
        std::clamp (original_m + influence * (target_m - original_m),
                    original_m - maximum_cut,
                    original_m + maximum_fill);
      const double change = shaped_m - original_m;
      if (std::fabs (change) <= 1e-6)
        continue;
      shaped[cell] = shaped_m / height_scale;
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
    for (const CellIndex cell : network.cells) {
      const CellIndex next = network.receiver[cell.value];
      if (next == no_cell)
        continue;
      const double run = receiver_distance_m (cell.value, next.value, grid);
      if (run <= 0.0)
        continue;
      const double rise =
        std::fabs (shaped[cell.value] - shaped[next.value]) * height_scale;
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
    return { .heights = std::move (shaped),
             .network = std::move (network),
             .report = report };
  }
}
