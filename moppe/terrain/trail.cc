#include <moppe/terrain/trail.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::terrain {
  namespace {
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
          p.grading_iterations < 0 ||
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
    };

    PlanningGrid planning_grid (const TerrainView& terrain,
                                const DrainageGraph& drainage,
                                const FloodField& flood) {
      const TerrainGrid& source = terrain.grid ();
      const int source_width = static_cast<int> (source.unique_width ());
      const int source_height = static_cast<int> (source.unique_height ());
      constexpr float planning_spacing = 24.0f;
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

    std::size_t choose_home_base (const PlanningGrid& grid,
                                  const TrailFormation& parameters) {
      const float sea = parameters.sea_level * grid.height_scale;
      const float target_water =
        meters_value (parameters.home_base_water_distance);
      const int shelter_reach =
        std::clamp (static_cast<int> (std::round (
                      120.0f / std::min (grid.spacing_x, grid.spacing_y))),
                    2,
                    8);
      float best_score = -std::numeric_limits<float>::infinity ();
      std::size_t best = 0;
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
        if (score > best_score) {
          best_score = score;
          best = node;
        }
      }
      if (!std::isfinite (best_score))
        throw std::runtime_error ("no dry home-base site near water");
      return best;
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

    std::size_t nearest_buildable_site (const PlanningGrid& grid,
                                        float ideal_x,
                                        float ideal_y,
                                        int search_radius) {
      float best_score = std::numeric_limits<float>::infinity ();
      std::size_t best = grid.node (static_cast<int> (std::round (ideal_x)),
                                    static_cast<int> (std::round (ideal_y)));
      for (int dy = -search_radius; dy <= search_radius; ++dy)
        for (int dx = -search_radius; dx <= search_radius; ++dx) {
          const std::size_t node =
            grid.node (static_cast<int> (std::round (ideal_x)) + dx,
                       static_cast<int> (std::round (ideal_y)) + dy);
          if (grid.wet (node))
            continue;
          const float score =
            std::hypot (static_cast<float> (dx), static_cast<float> (dy)) +
            12.0f * grid.grade (node);
          if (score < best_score) {
            best_score = score;
            best = node;
          }
        }
      return best;
    }

    std::vector<std::size_t>
    shortest_ride (const PlanningGrid& grid,
                   std::size_t start,
                   std::size_t goal,
                   const TrailFormation& parameters,
                   const std::vector<std::uint8_t>& avoid = {}) {
      struct Candidate {
        float estimate;
        std::size_t node;
        bool operator> (const Candidate& other) const {
          return estimate > other.estimate;
        }
      };
      const std::size_t count =
        static_cast<std::size_t> (grid.width) * grid.height;
      const float infinity = std::numeric_limits<float>::infinity ();
      std::vector<float> cost (count, infinity);
      std::vector<std::size_t> previous (count, count);
      std::priority_queue<Candidate,
                          std::vector<Candidate>,
                          std::greater<Candidate>>
        frontier;
      cost[start] = 0.0f;
      frontier.push ({ grid.distance (start, goal), start });
      const float maximum_grade =
        parameters.maximum_grade.numerical_value_in (mp_units::one);
      const float target_catchment =
        std::sqrt (square_meters_value (parameters.minimum_catchment_area) *
                   square_meters_value (parameters.maximum_catchment_area));
      while (!frontier.empty ()) {
        const Candidate current = frontier.top ();
        frontier.pop ();
        if (current.node == goal)
          break;
        if (current.estimate >
            cost[current.node] + grid.distance (current.node, goal) + 1e-4f)
          continue;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0)
              continue;
            const std::size_t next = grid.node (grid.x (current.node) + dx,
                                                grid.y (current.node) + dy);
            if (grid.wet (next) && next != goal)
              continue;
            const float run = grid.distance (current.node, next);
            if (run <= 0.0f)
              continue;
            const float grade = std::fabs (grid.elevation (next) -
                                           grid.elevation (current.node)) /
                                run;
            if (grade > 0.85f)
              continue;
            const float excess = std::max (0.0f, grade - maximum_grade);
            const float valley = std::fabs (std::log (
              std::max (grid.catchment (next), 1.0f) / target_catchment));
            const float avoidance =
              !avoid.empty () && avoid[next] ? 45.0f : 0.0f;
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
              run * (1.0f + 4.0f * grade + 42.0f * excess * excess +
                     0.10f * valley + avoidance + chart_edge);
            const float next_cost = cost[current.node] + edge;
            if (next_cost < cost[next]) {
              cost[next] = next_cost;
              previous[next] = current.node;
              frontier.push ({ next_cost + grid.distance (next, goal), next });
            }
          }
      }
      if (previous[goal] == count && start != goal)
        return {};
      std::vector<std::size_t> path;
      for (std::size_t node = goal;; node = previous[node]) {
        path.push_back (node);
        if (node == start)
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
    const std::size_t home_base = choose_home_base (planner, parameters);
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
                              anchor_search);
    const std::size_t far = nearest_buildable_site (planner,
                                                    focus_x + forward_x * flank,
                                                    focus_y + forward_y * flank,
                                                    anchor_search);
    const std::size_t right =
      nearest_buildable_site (planner,
                              focus_x + forward_y * flank,
                              focus_y - forward_x * flank,
                              anchor_search);

    std::vector<std::size_t> outbound;
    append_path (outbound,
                 shortest_ride (planner, home_base, left, parameters));
    append_path (outbound, shortest_ride (planner, left, far, parameters));
    if (outbound.empty () || outbound.front () != home_base ||
        outbound.back () != far)
      throw std::runtime_error ("could not route first half of trail circuit");

    std::vector<std::uint8_t> avoid (
      static_cast<std::size_t> (planner.width) * planner.height, 0);
    for (const std::size_t node : outbound)
      if (planner.distance (node, home_base) > 3.0f * planner.spacing_x &&
          planner.distance (node, far) > 3.0f * planner.spacing_x)
        avoid[node] = 1;
    std::vector<std::size_t> inbound;
    append_path (inbound,
                 shortest_ride (planner, home_base, right, parameters, avoid));
    append_path (inbound,
                 shortest_ride (planner, right, far, parameters, avoid));
    if (inbound.empty () || inbound.front () != home_base ||
        inbound.back () != far)
      throw std::runtime_error ("could not route second half of trail circuit");

    std::vector<std::size_t> coarse_circuit = outbound;
    if (inbound.size () > 2)
      for (std::size_t i = inbound.size () - 1; i-- > 1;)
        coarse_circuit.push_back (inbound[i]);
    std::vector<CellIndex> cells = expand_circuit (planner, coarse_circuit);
    if (cells.size () < 4)
      throw std::runtime_error (
        "trail circuit collapsed during materialization");

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
    return { .heights = std::move (shaped),
             .network = std::move (network),
             .report = report };
  }
}
