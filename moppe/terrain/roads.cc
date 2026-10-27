#include <moppe/terrain/roads.hh>

#include <moppe/parallel.hh>
#include <moppe/profile.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <queue>
#include <unordered_map>

namespace moppe::terrain {
  namespace {
    constexpr float infinite = std::numeric_limits<float>::infinity ();

    float square (float value) {
      return value * value;
    }

    // The planning lattice: the world read every twenty metres or so, with
    // what a road needs to know of each node.
    struct Lattice {
      enum Water : std::uint8_t { Dry, Shallow, Deep, Sea };

      int width = 0;
      int height = 0;
      float spacing_x = 0.0f;
      float spacing_z = 0.0f;
      std::vector<float> elevation;
      std::vector<std::uint8_t> water;
      // Zero off the rivers; on them, how dear a bridge is, growing with
      // the country the river drains.
      std::vector<float> river;
      std::vector<float> catchment_m2;
      // The steepest fall to a dry neighbour.
      std::vector<float> grade;
      // Per node and step (steps, below), how far the ground between the
      // two nodes stands off the straight line joining them, on average:
      // the cut and fill a road along that step would need, which the
      // nodes alone do not see (a spur crossing between them).
      std::vector<float> earthwork;
      // What stands on the ground and how wet it is (RoadGround).
      std::vector<float> forest;
      std::vector<float> mire;
      std::vector<float> wetness;

      std::size_t node (int x, int y) const {
        return static_cast<std::size_t> (wrap_index (y, height)) * width +
               static_cast<std::size_t> (wrap_index (x, width));
      }

      int x (std::size_t node) const {
        return static_cast<int> (node % static_cast<std::size_t> (width));
      }

      int y (std::size_t node) const {
        return static_cast<int> (node / static_cast<std::size_t> (width));
      }

      std::size_t size () const {
        return elevation.size ();
      }

      int delta (int from, int to, int period) const {
        int d = to - from;
        if (d > period / 2)
          d -= period;
        if (d < -period / 2)
          d += period;
        return d;
      }

      float distance (std::size_t a, std::size_t b) const {
        return std::hypot (delta (x (a), x (b), width) * spacing_x,
                           delta (y (a), y (b), height) * spacing_z);
      }

      bool dry (std::size_t node) const {
        return water[node] == Dry && river[node] == 0.0f;
      }
    };

    // The steps a search may take: the eight neighbours and the eight
    // knight's moves, so that a route may hold a grade across a slope
    // without a staircase of right angles.
    constexpr std::array<std::array<int, 2>, 16> steps { {
      { 1, 0 },
      { -1, 0 },
      { 0, 1 },
      { 0, -1 },
      { 1, 1 },
      { 1, -1 },
      { -1, 1 },
      { -1, -1 },
      { 2, 1 },
      { 2, -1 },
      { -2, 1 },
      { -2, -1 },
      { 1, 2 },
      { 1, -2 },
      { -1, 2 },
      { -1, -2 },
    } };

    Lattice read_lattice (const TerrainDomain& domain,
                          std::span<const SurfaceElevation> elevations,
                          const FloodField& flood,
                          const DrainageGraph& drainage,
                          const RoadGround& ground,
                          float river_area_m2,
                          float sea_level) {
      const std::span<const float> heights =
        surface_elevation_values (elevations);
      const int source_width = static_cast<int> (domain.width ());
      const int source_height = static_cast<int> (domain.height ());
      const float source_x = domain.spacing_x ().numerical_value_in (u::m);
      const float source_z = domain.spacing_z ().numerical_value_in (u::m);
      constexpr float spacing = 20.0f;
      Lattice lattice;
      lattice.width = std::clamp (
        static_cast<int> (std::round (source_width * source_x / spacing)),
        std::min (8, source_width),
        source_width);
      lattice.height = std::clamp (
        static_cast<int> (std::round (source_height * source_z / spacing)),
        std::min (8, source_height),
        source_height);
      lattice.spacing_x = source_width * source_x / lattice.width;
      lattice.spacing_z = source_height * source_z / lattice.height;
      const std::size_t count =
        static_cast<std::size_t> (lattice.width) * lattice.height;
      lattice.elevation.resize (count);
      lattice.water.resize (count);
      lattice.river.assign (count, 0.0f);
      lattice.catchment_m2.resize (count);
      lattice.grade.resize (count);
      lattice.forest.assign (count, 0.0f);
      lattice.mire.assign (count, 0.0f);
      lattice.wetness.assign (count, 0.0f);

      const auto source_cell = [&] (int x, int y) {
        return static_cast<std::size_t> (y * source_height / lattice.height) *
                 source_width +
               static_cast<std::size_t> (x * source_width / lattice.width);
      };
      for (int y = 0; y < lattice.height; ++y)
        for (int x = 0; x < lattice.width; ++x) {
          const std::size_t node = lattice.node (x, y);
          const std::size_t cell = source_cell (x, y);
          lattice.elevation[node] = heights[cell];
          const float depth = flood.water_depth_m (cell);
          // The sea is the water standing at its level, as the map reads it.
          lattice.water[node] = depth <= 1e-3f ? Lattice::Dry
                                : flood.water_level_m (cell) < sea_level + 0.35f
                                  ? Lattice::Sea
                                : depth > 1.5f ? Lattice::Deep
                                               : Lattice::Shallow;
          lattice.catchment_m2[node] =
            drainage.contributing_area_at (cell).numerical_value_in (u::m *
                                                                     u::m);
          const auto read = [cell] (std::span<const float> field) {
            return cell < field.size () ? std::clamp (field[cell], 0.0f, 1.0f)
                                        : 0.0f;
          };
          lattice.forest[node] = read (ground.forest_cover);
          lattice.mire[node] = read (ground.mire);
          lattice.wetness[node] = read (ground.wetness);
        }

      // Every river cell marks the node it falls in, and the band is
      // widened by a node either side so that no step of the search can
      // leap a river without paying for its bridge.
      std::vector<float> marked (count, 0.0f);
      for (int cy = 0; cy < source_height; ++cy)
        for (int cx = 0; cx < source_width; ++cx) {
          const std::size_t cell =
            static_cast<std::size_t> (cy) * source_width + cx;
          const float area =
            drainage.contributing_area_at (cell).numerical_value_in (u::m *
                                                                     u::m);
          if (area < river_area_m2 || flood.water_depth_m (cell) > 1e-3f)
            continue;
          const std::size_t node =
            lattice.node (cx * lattice.width / source_width,
                          cy * lattice.height / source_height);
          marked[node] = std::max (
            marked[node], 1.0f + 0.5f * std::log2 (area / river_area_m2));
        }
      for (int y = 0; y < lattice.height; ++y)
        for (int x = 0; x < lattice.width; ++x) {
          float strongest = 0.0f;
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
              strongest =
                std::max (strongest, marked[lattice.node (x + dx, y + dy)]);
          lattice.river[lattice.node (x, y)] = strongest;
        }

      for (std::size_t node = 0; node < count; ++node) {
        float steepest = 0.0f;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0)
              continue;
            const std::size_t other =
              lattice.node (lattice.x (node) + dx, lattice.y (node) + dy);
            // The ground's own slope: a shore is not steep for the bed
            // beneath the water beside it.
            if (lattice.water[other] != Lattice::Dry)
              continue;
            steepest = std::max (
              steepest,
              std::fabs (lattice.elevation[other] - lattice.elevation[node]) /
                lattice.distance (node, other));
          }
        lattice.grade[node] = steepest;
      }

      // Bilinear over the source, wrapping, in metres.
      const auto ground_at = [&] (float x_m, float z_m) {
        const float gx = x_m / source_x;
        const float gz = z_m / source_z;
        const float fx = std::floor (gx);
        const float fz = std::floor (gz);
        const int x = static_cast<int> (fx);
        const int z = static_cast<int> (fz);
        const auto at = [&] (int cx, int cz) {
          return heights
            [static_cast<std::size_t> (wrap_index (cz, source_height)) *
               source_width +
             static_cast<std::size_t> (wrap_index (cx, source_width))];
        };
        const float tx = gx - fx;
        const float tz = gz - fz;
        return std::lerp (std::lerp (at (x, z), at (x + 1, z), tx),
                          std::lerp (at (x, z + 1), at (x + 1, z + 1), tx),
                          tz);
      };
      lattice.earthwork.assign (count * steps.size (), 0.0f);
      for (std::size_t node = 0; node < count; ++node) {
        const float ax =
          static_cast<float> (static_cast<int> (
            source_cell (lattice.x (node), lattice.y (node)) % source_width)) *
          source_x;
        const float az =
          static_cast<float> (static_cast<int> (
            source_cell (lattice.x (node), lattice.y (node)) / source_width)) *
          source_z;
        for (std::size_t k = 0; k < steps.size (); ++k) {
          const std::size_t other = lattice.node (
            lattice.x (node) + steps[k][0], lattice.y (node) + steps[k][1]);
          const float dx = steps[k][0] * lattice.spacing_x;
          const float dz = steps[k][1] * lattice.spacing_z;
          const int samples =
            std::max (2,
                      static_cast<int> (std::ceil (
                        std::hypot (dx, dz) / std::max (source_x, source_z))));
          float off = 0.0f;
          for (int i = 1; i < samples; ++i) {
            const float t = static_cast<float> (i) / samples;
            const float line =
              std::lerp (lattice.elevation[node], lattice.elevation[other], t);
            off += std::fabs (ground_at (ax + dx * t, az + dz * t) - line);
          }
          lattice.earthwork[node * steps.size () + k] = off / (samples - 1);
        }
      }
      return lattice;
    }

    enum Way : std::uint8_t { None, Path, Built };

    // How dear each mover finds a step from a to b of the given run.
    struct Mover {
      enum class Kind { Cart, Walker } kind;
      // How much less an existing way costs to follow than to build.
      float reuse = 1.0f;

      float step (const Lattice& lattice,
                  const std::vector<std::uint8_t>& ways,
                  std::size_t a,
                  std::size_t b,
                  float run,
                  float earthwork = 0.0f) const {
        const float grade =
          std::fabs (lattice.elevation[b] - lattice.elevation[a]) / run;
        float cost = 0.0f;
        if (kind == Kind::Cart) {
          if (lattice.water[a] >= Lattice::Deep ||
              lattice.water[b] >= Lattice::Deep || grade > 0.16f)
            return infinite;
          // A mire swallows a road: past half cover it is not built on,
          // and below that it is dear (Galin et al. 2010's terms, with
          // forest for their tree density).
          if (lattice.mire[b] > 0.5f)
            return infinite;
          cost = run * (1.0f + square (grade / 0.06f));
          if (lattice.water[b] == Lattice::Shallow)
            cost *= 20.0f;
          cost += run * 4.0f * lattice.river[b];
          cost += run * (1.5f * lattice.forest[b] + 6.0f * lattice.mire[b] +
                         1.0f * lattice.wetness[b] + 1.0f * earthwork);
          if (ways[b] == Built)
            cost *= reuse;
        } else {
          if (lattice.water[b] != Lattice::Dry || grade > 0.5f)
            return infinite;
          cost = run * (1.0f + square (grade / 0.2f));
          cost += run * 1.5f * lattice.river[b];
          cost += run * (0.4f * lattice.forest[b] + 2.0f * lattice.mire[b] +
                         0.3f * lattice.wetness[b]);
          if (ways[b] != None)
            cost *= reuse;
        }
        return cost;
      }
    };

    struct Search {
      std::vector<float> cost;
      std::vector<std::uint32_t> from;
    };

    Search search (const Lattice& lattice,
                   const std::vector<std::uint8_t>& ways,
                   const std::vector<std::size_t>& sources,
                   const Mover& mover) {
      Search result { std::vector<float> (lattice.size (), infinite),
                      std::vector<std::uint32_t> (lattice.size ()) };
      using Entry = std::pair<float, std::uint32_t>;
      std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
      for (const std::size_t source : sources) {
        result.cost[source] = 0.0f;
        result.from[source] = static_cast<std::uint32_t> (source);
        open.push ({ 0.0f, static_cast<std::uint32_t> (source) });
      }
      while (!open.empty ()) {
        const auto [cost, node] = open.top ();
        open.pop ();
        if (cost > result.cost[node])
          continue;
        const int x = lattice.x (node);
        const int y = lattice.y (node);
        for (std::size_t k = 0; k < steps.size (); ++k) {
          const auto [dx, dy] = steps[k];
          const std::size_t next = lattice.node (x + dx, y + dy);
          const float run =
            std::hypot (dx * lattice.spacing_x, dy * lattice.spacing_z);
          float step = mover.step (lattice,
                                   ways,
                                   node,
                                   next,
                                   run,
                                   lattice.earthwork[node * steps.size () + k]);
          // A knight's move passes over two cells on its way: both must be
          // passable too, or it would cut the corner of a mire or a lake.
          if (step < infinite && (std::abs (dx) == 2 || std::abs (dy) == 2)) {
            const int mx = std::abs (dx) == 2 ? dx / 2 : 0;
            const int my = std::abs (dy) == 2 ? dy / 2 : 0;
            for (const std::size_t over :
                 { lattice.node (x + mx, y + my),
                   lattice.node (x + (mx ? mx : dx), y + (my ? my : dy)) })
              if (!(mover.step (lattice, ways, node, over, 0.5f * run) <
                    infinite))
                step = infinite;
          }
          if (!(step < infinite))
            continue;
          const float reached = cost + step;
          if (reached < result.cost[next]) {
            result.cost[next] = reached;
            result.from[next] = node;
            open.push ({ reached, static_cast<std::uint32_t> (next) });
          }
        }
      }
      return result;
    }

    // From a reached node back to the source the search came from.
    std::vector<std::size_t> trace (const Search& found, std::size_t node) {
      std::vector<std::size_t> path { node };
      while (found.from[node] != node) {
        node = found.from[node];
        path.push_back (node);
      }
      return path;
    }

    float path_length (const Lattice& lattice,
                       const std::vector<std::size_t>& path) {
      float length = 0.0f;
      for (std::size_t i = 1; i < path.size (); ++i)
        length += lattice.distance (path[i - 1], path[i]);
      return length;
    }

    std::vector<RoadPoint> unwrap (const Lattice& lattice,
                                   const std::vector<std::size_t>& nodes) {
      std::vector<RoadPoint> points;
      int x = lattice.x (nodes.front ());
      int y = lattice.y (nodes.front ());
      for (std::size_t i = 0; i < nodes.size (); ++i) {
        if (i > 0) {
          x += lattice.delta (
            lattice.x (nodes[i - 1]), lattice.x (nodes[i]), lattice.width);
          y += lattice.delta (
            lattice.y (nodes[i - 1]), lattice.y (nodes[i]), lattice.height);
        }
        points.push_back ({ x * lattice.spacing_x, y * lattice.spacing_z });
      }
      // Two rounds of corner cutting take the lattice's angles off.
      for (int round = 0; round < 2 && points.size () > 2; ++round) {
        std::vector<RoadPoint> smooth { points.front () };
        for (std::size_t i = 0; i + 1 < points.size (); ++i) {
          const RoadPoint a = points[i];
          const RoadPoint b = points[i + 1];
          smooth.push_back (
            { 0.75f * a.x_m + 0.25f * b.x_m, 0.75f * a.z_m + 0.25f * b.z_m });
          smooth.push_back (
            { 0.25f * a.x_m + 0.75f * b.x_m, 0.25f * a.z_m + 0.75f * b.z_m });
        }
        smooth.push_back (points.back ());
        points = std::move (smooth);
      }
      return points;
    }

    struct Planner {
      const Lattice& lattice;
      std::vector<std::uint8_t> ways;
      RoadNetwork network;
      // Each way's lattice nodes, beside network.roads.
      std::vector<std::vector<std::size_t>> road_nodes;

      std::vector<std::size_t> built () const {
        std::vector<std::size_t> nodes;
        for (std::size_t node = 0; node < ways.size (); ++node)
          if (ways[node] == Built)
            nodes.push_back (node);
        return nodes;
      }

      // Lays a traced path as a way of the given kind: the stretches that
      // are new, each with the old node it leaves from or joins.
      void lay (const std::vector<std::size_t>& path, Road::Kind kind) {
        const std::uint8_t mark = kind == Road::Kind::Road ? Built : Path;
        std::vector<std::size_t> piece;
        const auto emit = [&] {
          if (piece.size () >= 2) {
            network.roads.push_back ({ kind, unwrap (lattice, piece) });
            road_nodes.push_back (piece);
          }
          piece.clear ();
        };
        for (std::size_t i = 0; i < path.size (); ++i) {
          const bool old = ways[path[i]] >= mark;
          if (!old) {
            if (piece.empty () && i > 0)
              piece.push_back (path[i - 1]);
            piece.push_back (path[i]);
          } else if (!piece.empty ()) {
            piece.push_back (path[i]);
            emit ();
          }
        }
        emit ();
        for (const std::size_t node : path)
          ways[node] = std::max (ways[node], mark);
      }

      RoadPlace place (RoadPlace::Kind kind, std::size_t node) const {
        return { kind,
                 lattice.x (node) * lattice.spacing_x,
                 lattice.y (node) * lattice.spacing_z,
                 lattice.elevation[node] };
      }

      // Builds a road from the network to the node; false if none reaches.
      bool connect (std::size_t node, const Search& from_network) {
        if (!(from_network.cost[node] < infinite))
          return false;
        lay (trace (from_network, node), Road::Kind::Road);
        return true;
      }
    };

    float torus_distance (const Lattice& lattice, RoadPlace a, RoadPlace b) {
      const float period_x = lattice.width * lattice.spacing_x;
      const float period_z = lattice.height * lattice.spacing_z;
      float dx = std::fmod (std::fabs (a.x_m - b.x_m), period_x);
      float dz = std::fmod (std::fabs (a.z_m - b.z_m), period_z);
      dx = std::min (dx, period_x - dx);
      dz = std::min (dz, period_z - dz);
      return std::hypot (dx, dz);
    }

    // The node nearest a point in metres.
    std::size_t node_at (const Lattice& lattice, float x_m, float z_m) {
      return lattice.node (
        static_cast<int> (std::lround (x_m / lattice.spacing_x)),
        static_cast<int> (std::lround (z_m / lattice.spacing_z)));
    }

    // Walks the line from a point some way above one node toward another
    // and says whether the ground keeps out of it.
    bool sees (const Lattice& lattice,
               std::size_t from,
               float eye,
               std::size_t to,
               float target) {
      const float dx = static_cast<float> (
        lattice.delta (lattice.x (from), lattice.x (to), lattice.width));
      const float dy = static_cast<float> (
        lattice.delta (lattice.y (from), lattice.y (to), lattice.height));
      const float a = lattice.elevation[from] + eye;
      const float b = lattice.elevation[to] + target;
      const int count = static_cast<int> (
        std::ceil (std::max (std::fabs (dx), std::fabs (dy))));
      for (int i = 1; i < count; ++i) {
        const float t = static_cast<float> (i) / count;
        const std::size_t node = lattice.node (
          lattice.x (from) + static_cast<int> (std::lround (dx * t)),
          lattice.y (from) + static_cast<int> (std::lround (dy * t)));
        if (lattice.elevation[node] > a + (b - a) * t + 0.5f)
          return false;
      }
      return true;
    }

    // How much low, workable country a sanctuary here would see and be
    // seen from: Peatfield's rule that a peak sanctuary sees the region it
    // serves, whose fires are lit to be seen from the homes below, mostly
    // within an hour's walk.
    float regard (const Lattice& lattice, std::size_t site) {
      constexpr int rays = 64;
      constexpr float step_m = 40.0f;
      constexpr float reach_m = 4500.0f;
      const float eye = lattice.elevation[site] + 4.0f;
      float score = 0.0f;
      for (int ray = 0; ray < rays; ++ray) {
        const float angle =
          2.0f * std::numbers::pi_v<float> * static_cast<float> (ray) / rays;
        const float ux = std::cos (angle) / lattice.spacing_x;
        const float uy = std::sin (angle) / lattice.spacing_z;
        float horizon = -infinite;
        for (float d = 60.0f; d < reach_m; d += step_m) {
          const std::size_t node = lattice.node (
            lattice.x (site) + static_cast<int> (std::lround (ux * d)),
            lattice.y (site) + static_cast<int> (std::lround (uy * d)));
          const float ground = lattice.elevation[node];
          const bool served = lattice.water[node] == Lattice::Dry &&
                              lattice.grade[node] < 0.08f &&
                              ground < eye - 30.0f;
          if (served && (ground + 1.5f - eye) / d >= horizon)
            score += d * (1.0f - d / reach_m);
          horizon = std::max (horizon, (ground - eye) / d);
        }
      }
      return score / 1.0e6f;
    }

    struct Sanctuary {
      std::size_t node;
      float score;
    };

    std::vector<Sanctuary> site_sanctuaries (const Lattice& lattice,
                                             float sea_level) {
      // The highest node of each 200 m block, and those blocks higher than
      // every other within a kilometre: the summits.
      const int block = std::max (
        1, static_cast<int> (std::lround (200.0f / lattice.spacing_x)));
      const int across = std::max (1, lattice.width / block);
      const int down = std::max (1, lattice.height / block);
      std::vector<std::size_t> tops (static_cast<std::size_t> (across) * down,
                                     0);
      std::vector<float> top_height (tops.size (), -infinite);
      for (int y = 0; y < down * block; ++y)
        for (int x = 0; x < across * block; ++x) {
          const std::size_t b = static_cast<std::size_t> (y / block) * across +
                                static_cast<std::size_t> (x / block);
          const std::size_t node = lattice.node (x, y);
          if (lattice.elevation[node] > top_height[b]) {
            top_height[b] = lattice.elevation[node];
            tops[b] = node;
          }
        }
      std::vector<std::size_t> summits;
      for (int by = 0; by < down; ++by)
        for (int bx = 0; bx < across; ++bx) {
          const std::size_t b = static_cast<std::size_t> (by) * across + bx;
          if (top_height[b] - sea_level < 40.0f)
            continue;
          bool highest = true;
          for (int dy = -2; dy <= 2 && highest; ++dy)
            for (int dx = -2; dx <= 2 && highest; ++dx) {
              const std::size_t other =
                static_cast<std::size_t> (wrap_index (by + dy, down)) * across +
                static_cast<std::size_t> (wrap_index (bx + dx, across));
              highest = other == b || top_height[other] < top_height[b] ||
                        (top_height[other] == top_height[b] && other > b);
            }
          if (!highest)
            continue;
          // A summit that stands over its country, not a bump on a plateau.
          float lowest = infinite;
          const int reach = static_cast<int> (1500.0f / lattice.spacing_x);
          for (int dy = -reach; dy <= reach; dy += 5)
            for (int dx = -reach; dx <= reach; dx += 5)
              lowest = std::min (
                lowest,
                lattice.elevation[lattice.node (lattice.x (tops[b]) + dx,
                                                lattice.y (tops[b]) + dy)]);
          if (top_height[b] - lowest >= 50.0f)
            summits.push_back (tops[b]);
        }

      // Each summit's best site: the top itself or a shoulder up to 500 m
      // off and 45 m down, as Petsopha stands on the peak that overlooks its
      // town rather than the highest of three.
      std::vector<Sanctuary> sites (summits.size ());
      parallel_for (
        summits.size (), 1, [&] (std::size_t begin, std::size_t end) {
          for (std::size_t i = begin; i < end; ++i) {
            const std::size_t summit = summits[i];
            const int reach = static_cast<int> (500.0f / lattice.spacing_x);
            Sanctuary best { summit, regard (lattice, summit) };
            for (int dy = -reach; dy <= reach; dy += 3)
              for (int dx = -reach; dx <= reach; dx += 3) {
                if (dx * dx + dy * dy > reach * reach || (dx == 0 && dy == 0))
                  continue;
                const std::size_t node = lattice.node (lattice.x (summit) + dx,
                                                       lattice.y (summit) + dy);
                if (lattice.water[node] != Lattice::Dry ||
                    lattice.elevation[node] < lattice.elevation[summit] - 45.0f)
                  continue;
                const float score = regard (lattice, node);
                if (score > best.score)
                  best = { node, score };
              }
            sites[i] = best;
          }
        });
      std::sort (sites.begin (), sites.end (), [] (Sanctuary a, Sanctuary b) {
        return a.score != b.score ? a.score > b.score : a.node < b.node;
      });

      // The best of them, spread out: about one to every twelve square
      // kilometres.
      const float area_km2 = lattice.width * lattice.spacing_x *
                             lattice.height * lattice.spacing_z / 1.0e6f;
      const std::size_t limit = static_cast<std::size_t> (
        std::max (2.0f, std::round (area_km2 / 12.0f)));
      std::vector<Sanctuary> kept;
      for (const Sanctuary& site : sites) {
        if (kept.size () >= limit ||
            (!kept.empty () && site.score < 0.3f * kept.front ().score))
          break;
        const bool crowded = std::any_of (
          kept.begin (), kept.end (), [&] (const Sanctuary& other) {
            return lattice.distance (site.node, other.node) < 1800.0f;
          });
        if (!crowded)
          kept.push_back (site);
      }
      return kept;
    }

    // Lakes worth a road, largest first, each by the dry node where its
    // water leaves: the neighbour draining the most country.
    std::vector<std::size_t> lake_outlets (const Lattice& lattice) {
      std::vector<std::int32_t> label (lattice.size (), -1);
      struct Lake {
        std::size_t cells = 0;
        std::size_t outlet = 0;
        float outlet_area = -1.0f;
      };
      std::vector<Lake> lakes;
      std::vector<std::size_t> stack;
      for (std::size_t start = 0; start < lattice.size (); ++start) {
        if (label[start] >= 0 || lattice.water[start] == Lattice::Dry ||
            lattice.water[start] == Lattice::Sea)
          continue;
        Lake lake;
        const auto id = static_cast<std::int32_t> (lakes.size ());
        label[start] = id;
        stack.push_back (start);
        while (!stack.empty ()) {
          const std::size_t node = stack.back ();
          stack.pop_back ();
          ++lake.cells;
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              const std::size_t next =
                lattice.node (lattice.x (node) + dx, lattice.y (node) + dy);
              if (lattice.water[next] == Lattice::Dry) {
                if (lattice.catchment_m2[next] > lake.outlet_area &&
                    lattice.grade[next] < 0.2f) {
                  lake.outlet_area = lattice.catchment_m2[next];
                  lake.outlet = next;
                }
              } else if (lattice.water[next] != Lattice::Sea &&
                         label[next] < 0) {
                label[next] = id;
                stack.push_back (next);
              }
            }
        }
        lakes.push_back (lake);
      }
      std::sort (
        lakes.begin (), lakes.end (), [] (const Lake& a, const Lake& b) {
          return a.cells != b.cells ? a.cells > b.cells : a.outlet < b.outlet;
        });
      const float node_area = lattice.spacing_x * lattice.spacing_z;
      const float area_km2 =
        static_cast<float> (lattice.size ()) * node_area / 1.0e6f;
      const std::size_t limit = static_cast<std::size_t> (
        std::max (2.0f, std::round (area_km2 / 10.0f)));
      std::vector<std::size_t> outlets;
      for (const Lake& lake : lakes) {
        if (outlets.size () >= limit ||
            static_cast<float> (lake.cells) * node_area < 2.5e5f)
          break;
        if (lake.outlet_area >= 0.0f)
          outlets.push_back (lake.outlet);
      }
      return outlets;
    }

    // A well-mixed hash of a few numbers (lowbias32), read as [0, 1).
    float roll (std::uint32_t seed, std::size_t node, std::uint32_t salt) {
      std::uint32_t x = seed ^
                        (static_cast<std::uint32_t> (node) * 0x9e3779b9U) ^
                        (salt * 0x85ebca6bU);
      for (int round = 0; round < 2; ++round) {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
      }
      return static_cast<float> (x >> 8) / static_cast<float> (1u << 24);
    }

    // When a place of each kind was founded and how likely, and how long
    // ago, it went quiet, in years. The sanctuaries are older than any
    // farm and outlive the people who lit their fires (Peatfield 1983);
    // the trailhead is this century's.
    void roll_history (RoadPlace& place, std::size_t node, std::uint32_t seed) {
      struct History {
        float founded_low, founded_high;
        float abandon_chance;
        float abandoned_low, abandoned_high;
      };
      History history {};
      switch (place.kind) {
      case RoadPlace::Kind::Trailhead:
        history = { 25.0f, 60.0f, 0.0f, 0.0f, 0.0f };
        break;
      case RoadPlace::Kind::Landing:
        history = { 250.0f, 600.0f, 0.25f, 15.0f, 150.0f };
        break;
      case RoadPlace::Kind::LakeOutlet:
        history = { 150.0f, 500.0f, 0.4f, 10.0f, 120.0f };
        break;
      case RoadPlace::Kind::Sanctuary:
        history = { 1500.0f, 3500.0f, 0.6f, 150.0f, 1500.0f };
        break;
      }
      const auto between = [&] (float low, float high, std::uint32_t salt) {
        return std::round (low + (high - low) * roll (seed, node, salt));
      };
      place.founded_years_ago =
        between (history.founded_low, history.founded_high, 1);
      place.abandoned_years_ago =
        roll (seed, node, 2) < history.abandon_chance
          ? std::min (
              between (history.abandoned_low, history.abandoned_high, 3),
              std::round (0.9f * place.founded_years_ago))
          : 0.0f;
    }

    float wrapped (float delta, float period) {
      return delta - period * std::round (delta / period);
    }

    // The polyline again at about `step` metres between points, its ends
    // kept.
    std::vector<RoadPoint> resample (const std::vector<RoadPoint>& line,
                                     float step) {
      float length = 0.0f;
      for (std::size_t i = 1; i < line.size (); ++i)
        length += std::hypot (line[i].x_m - line[i - 1].x_m,
                              line[i].z_m - line[i - 1].z_m);
      const int count =
        std::max (1, static_cast<int> (std::ceil (length / step)));
      const float spacing = length / static_cast<float> (count);
      std::vector<RoadPoint> result { line.front () };
      std::size_t segment = 1;
      float segment_start = 0.0f;
      for (int k = 1; k < count; ++k) {
        const float at = spacing * static_cast<float> (k);
        float segment_length = 0.0f;
        while (segment < line.size ()) {
          segment_length =
            std::hypot (line[segment].x_m - line[segment - 1].x_m,
                        line[segment].z_m - line[segment - 1].z_m);
          if (segment_start + segment_length >= at)
            break;
          segment_start += segment_length;
          ++segment;
        }
        if (segment >= line.size ())
          break;
        const float t =
          segment_length > 0.0f ? (at - segment_start) / segment_length : 0.0f;
        const RoadPoint a = line[segment - 1];
        const RoadPoint b = line[segment];
        result.push_back (
          { a.x_m + (b.x_m - a.x_m) * t, a.z_m + (b.z_m - a.z_m) * t });
      }
      result.push_back (line.back ());
      return result;
    }

    // Eases the lattice's turns into curves: each point moves to the mean
    // of its neighbours within `radius`, the window narrowing toward the
    // ends, which stay put.
    template <typename Passable>
    void ease (std::vector<RoadPoint>& line,
               int radius,
               int passes,
               const Passable& passable) {
      const int n = static_cast<int> (line.size ());
      for (int pass = 0; pass < passes; ++pass) {
        std::vector<RoadPoint> eased = line;
        for (int i = 1; i + 1 < n; ++i) {
          const int r = std::min ({ radius, i, n - 1 - i });
          float x = 0.0f, z = 0.0f;
          for (int j = i - r; j <= i + r; ++j) {
            x += line[static_cast<std::size_t> (j)].x_m;
            z += line[static_cast<std::size_t> (j)].z_m;
          }
          const RoadPoint point { x / (2 * r + 1), z / (2 * r + 1) };
          // A curve may not cut a corner into water or mire.
          if (passable (point))
            eased[static_cast<std::size_t> (i)] = point;
        }
        line = std::move (eased);
      }
    }

    // The nearest point to `at` on an earlier road, across the seam, and
    // how far it lies.
    std::pair<RoadPoint, float> nearest_on (const Road& road,
                                            RoadPoint at,
                                            float period_x,
                                            float period_z) {
      RoadPoint best = at;
      float best_distance = infinite;
      for (std::size_t i = 1; i < road.points.size (); ++i) {
        const float ax = wrapped (road.points[i - 1].x_m - at.x_m, period_x);
        const float az = wrapped (road.points[i - 1].z_m - at.z_m, period_z);
        const float dx = road.points[i].x_m - road.points[i - 1].x_m;
        const float dz = road.points[i].z_m - road.points[i - 1].z_m;
        const float length2 = dx * dx + dz * dz;
        const float t =
          length2 > 0.0f
            ? std::clamp (-(ax * dx + az * dz) / length2, 0.0f, 1.0f)
            : 0.0f;
        const float px = ax + dx * t;
        const float pz = az + dz * t;
        const float distance = std::hypot (px, pz);
        if (distance < best_distance) {
          best_distance = distance;
          best = { at.x_m + px, at.z_m + pz };
        }
      }
      return { best, best_distance };
    }

    // Moves a line's end onto `target`, carrying the first stretch of the
    // line with it and less the further from the end.
    void shift_end (std::vector<RoadPoint>& line, bool back, RoadPoint target) {
      constexpr float reach = 40.0f;
      const RoadPoint end = back ? line.back () : line.front ();
      const float sx = target.x_m - end.x_m;
      const float sz = target.z_m - end.z_m;
      float along = 0.0f;
      const std::size_t n = line.size ();
      for (std::size_t k = 0; k < n; ++k) {
        RoadPoint& point = line[back ? n - 1 - k : k];
        if (k > 0) {
          const RoadPoint& previous = line[back ? n - k : k - 1];
          along +=
            std::hypot (point.x_m - previous.x_m, point.z_m - previous.z_m);
        }
        if (along >= reach)
          break;
        const float t = 1.0f - along / reach;
        const float weight = t * t * (3.0f - 2.0f * t);
        point.x_m += sx * weight;
        point.z_m += sz * weight;
      }
    }

    // The planned lines as built: eased into curves at four metres between
    // points, and each end that leaves an earlier way moved onto it.
    void finish_lines (const Lattice& lattice, std::vector<Road>& roads) {
      const float period_x = lattice.width * lattice.spacing_x;
      const float period_z = lattice.height * lattice.spacing_z;
      for (std::size_t r = 0; r < roads.size (); ++r) {
        const bool built = roads[r].kind == Road::Kind::Road;
        std::vector<RoadPoint> line = resample (roads[r].points, 4.0f);
        ease (line, 5, 3, [&] (RoadPoint point) {
          const std::size_t node = node_at (lattice, point.x_m, point.z_m);
          return built ? lattice.water[node] < Lattice::Deep &&
                           lattice.mire[node] <= 0.5f
                       : lattice.water[node] == Lattice::Dry;
        });
        for (const bool back : { false, true }) {
          const RoadPoint end = back ? line.back () : line.front ();
          RoadPoint best = end;
          float best_distance = 30.0f;
          for (std::size_t earlier = 0; earlier < r; ++earlier) {
            const auto [point, distance] =
              nearest_on (roads[earlier], end, period_x, period_z);
            if (distance < best_distance) {
              best_distance = distance;
              best = point;
            }
          }
          shift_end (line, back, best);
        }
        roads[r].points = std::move (line);
      }
    }
  }

  RoadNetwork plan_roads (const TerrainDomain& domain,
                          std::span<const SurfaceElevation> elevations,
                          const FloodField& flood,
                          const DrainageGraph& drainage,
                          const RoadGround& ground,
                          square_meters_t river_area,
                          CellIndex home_base,
                          float sea_level,
                          std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("plan_roads");
    const Lattice lattice =
      read_lattice (domain,
                    elevations,
                    flood,
                    drainage,
                    ground,
                    river_area.numerical_value_in (u::m * u::m),
                    sea_level);
    Planner planner {
      lattice, std::vector<std::uint8_t> (lattice.size (), None), {}, {}
    };
    const Mover cart { Mover::Kind::Cart, 0.3f };
    const Mover walker { Mover::Kind::Walker, 0.5f };

    // The trailhead roots the network.
    std::size_t trailhead =
      lattice.node (lattice.width / 2, lattice.height / 2);
    if (home_base != no_cell) {
      const std::size_t width = domain.width ();
      trailhead =
        lattice.node (static_cast<int> (home_base.value % width) *
                        lattice.width / static_cast<int> (width),
                      static_cast<int> (home_base.value / width) *
                        lattice.height / static_cast<int> (domain.height ()));
    }
    planner.ways[trailhead] = Built;
    planner.network.places.push_back (
      planner.place (RoadPlace::Kind::Trailhead, trailhead));

    // The landing: the gentle shore the road reaches the sea at most
    // cheaply.
    {
      const Search found =
        search (lattice, planner.ways, planner.built (), cart);
      std::size_t best = lattice.size ();
      for (std::size_t node = 0; node < lattice.size (); ++node) {
        if (!lattice.dry (node) || lattice.grade[node] > 0.1f ||
            !(found.cost[node] < infinite))
          continue;
        bool shore = false;
        for (int dy = -1; dy <= 1 && !shore; ++dy)
          for (int dx = -1; dx <= 1 && !shore; ++dx)
            shore = lattice.water[lattice.node (lattice.x (node) + dx,
                                                lattice.y (node) + dy)] ==
                    Lattice::Sea;
        if (shore &&
            (best == lattice.size () || found.cost[node] < found.cost[best]))
          best = node;
      }
      if (best != lattice.size () && planner.connect (best, found))
        planner.network.places.push_back (
          planner.place (RoadPlace::Kind::Landing, best));
    }

    for (const std::size_t outlet : lake_outlets (lattice)) {
      const Search found =
        search (lattice, planner.ways, planner.built (), cart);
      if (planner.connect (outlet, found))
        planner.network.places.push_back (
          planner.place (RoadPlace::Kind::LakeOutlet, outlet));
    }

    // Each sanctuary is reached by a road to the foot of its climb and a
    // footpath up: the foot is where building road on stops paying for
    // itself against walking.
    std::vector<std::size_t> sanctuaries;
    for (const Sanctuary& site : site_sanctuaries (lattice, sea_level)) {
      const Search by_road =
        search (lattice, planner.ways, planner.built (), cart);
      const Search on_foot =
        search (lattice, planner.ways, { site.node }, walker);
      std::size_t foot = lattice.size ();
      float best = infinite;
      for (std::size_t node = 0; node < lattice.size (); ++node) {
        const float cost = by_road.cost[node] + 3.0f * on_foot.cost[node];
        if (cost < best) {
          best = cost;
          foot = node;
        }
      }
      if (foot == lattice.size ())
        continue;
      planner.connect (foot, by_road);
      std::vector<std::size_t> climb = trace (on_foot, foot);
      if (climb.size () >= 2)
        planner.lay (climb, Road::Kind::Footpath);
      sanctuaries.push_back (planner.network.places.size ());
      planner.network.places.push_back (
        planner.place (RoadPlace::Kind::Sanctuary, site.node));
    }

    // A tree of roads makes long ways round between neighbours. Where the
    // network's way between two places near each other is more than twice
    // as long as a road straight between them would be, build that road,
    // a few times over, the worst first (Emilien et al. 2012's cycles).
    std::vector<std::size_t> ends;
    for (const RoadPlace& place : planner.network.places) {
      std::size_t node = node_at (lattice, place.x_m, place.z_m);
      if (place.kind == RoadPlace::Kind::Sanctuary) {
        // A sanctuary's road ends at its foot: the nearest built node.
        float nearest = infinite;
        for (std::size_t other = 0; other < lattice.size (); ++other)
          if (planner.ways[other] == Built &&
              lattice.distance (node, other) < nearest) {
            nearest = lattice.distance (node, other);
            node = other;
          }
      }
      ends.push_back (node);
    }
    for (int round = 0; round < 3; ++round) {
      struct Shortcut {
        float ratio = 2.0f;
        std::vector<std::size_t> path;
      };
      std::vector<Shortcut> best (ends.size ());
      parallel_for (ends.size (), 1, [&] (std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
          // Along the roads alone, and anywhere a road could be built.
          std::vector<std::uint8_t> none (lattice.size (), None);
          const Search fresh = search (lattice, none, { ends[i] }, cart);
          Search along { std::vector<float> (lattice.size (), infinite),
                         std::vector<std::uint32_t> (lattice.size ()) };
          {
            using Entry = std::pair<float, std::uint32_t>;
            std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
            along.cost[ends[i]] = 0.0f;
            open.push ({ 0.0f, static_cast<std::uint32_t> (ends[i]) });
            while (!open.empty ()) {
              const auto [cost, node] = open.top ();
              open.pop ();
              if (cost > along.cost[node])
                continue;
              for (const auto& [dx, dy] : steps) {
                const std::size_t next =
                  lattice.node (lattice.x (node) + dx, lattice.y (node) + dy);
                if (planner.ways[next] != Built)
                  continue;
                const float reached = cost + lattice.distance (node, next);
                if (reached < along.cost[next]) {
                  along.cost[next] = reached;
                  open.push ({ reached, static_cast<std::uint32_t> (next) });
                }
              }
            }
          }
          for (std::size_t j = 0; j < ends.size (); ++j) {
            if (j == i || ends[j] == ends[i] ||
                !(fresh.cost[ends[j]] < infinite) ||
                torus_distance (lattice,
                                planner.network.places[i],
                                planner.network.places[j]) > 5000.0f)
              continue;
            std::vector<std::size_t> path = trace (fresh, ends[j]);
            const float direct = path_length (lattice, path);
            const float ratio = along.cost[ends[j]] / std::max (direct, 1.0f);
            if (direct < 4500.0f && ratio > best[i].ratio)
              best[i] = { ratio, std::move (path) };
          }
        }
      });
      const auto worst = std::max_element (
        best.begin (), best.end (), [] (const Shortcut& a, const Shortcut& b) {
          return a.ratio < b.ratio;
        });
      if (worst == best.end () || worst->path.empty ())
        break;
      planner.lay (worst->path, Road::Kind::Road);
    }

    for (std::size_t a = 0; a < sanctuaries.size (); ++a)
      for (std::size_t b = a + 1; b < sanctuaries.size (); ++b) {
        const RoadPlace& one = planner.network.places[sanctuaries[a]];
        const RoadPlace& two = planner.network.places[sanctuaries[b]];
        if (torus_distance (lattice, one, two) < 6000.0f &&
            sees (lattice,
                  node_at (lattice, one.x_m, one.z_m),
                  4.0f,
                  node_at (lattice, two.x_m, two.z_m),
                  4.0f))
          planner.network.sightlines.push_back (
            { sanctuaries[a], sanctuaries[b] });
      }

    // Each place's history, and each way's from the journeys along it:
    // every pair of places, by the network's shortest way between them
    // (footpaths counted twice as long, being slower).
    for (RoadPlace& place : planner.network.places)
      roll_history (place, node_at (lattice, place.x_m, place.z_m), seed);
    std::unordered_map<std::uint64_t, std::size_t> marker;
    for (std::size_t road = 0; road < planner.road_nodes.size (); ++road) {
      const std::vector<std::size_t>& nodes = planner.road_nodes[road];
      const std::size_t k = (nodes.size () - 1) / 2;
      const std::uint64_t a = nodes[k];
      const std::uint64_t b = nodes[k + 1];
      marker[a * lattice.size () + b] = road;
      marker[b * lattice.size () + a] = road;
    }
    std::vector<RoadPlace>& places = planner.network.places;
    std::vector<Road>& roads = planner.network.roads;
    std::vector<bool> lived_on (roads.size (), false);
    std::vector<float> last_used (roads.size (), infinite);
    for (std::size_t i = 0; i < places.size (); ++i) {
      const std::size_t from = node_at (lattice, places[i].x_m, places[i].z_m);
      Search along { std::vector<float> (lattice.size (), infinite),
                     std::vector<std::uint32_t> (lattice.size ()) };
      using Entry = std::pair<float, std::uint32_t>;
      std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
      along.cost[from] = 0.0f;
      along.from[from] = static_cast<std::uint32_t> (from);
      open.push ({ 0.0f, static_cast<std::uint32_t> (from) });
      while (!open.empty ()) {
        const auto [cost, node] = open.top ();
        open.pop ();
        if (cost > along.cost[node])
          continue;
        for (const auto& [dx, dy] : steps) {
          const std::size_t next =
            lattice.node (lattice.x (node) + dx, lattice.y (node) + dy);
          if (planner.ways[next] == None)
            continue;
          const float slow = planner.ways[next] == Path ? 2.0f : 1.0f;
          const float reached = cost + slow * lattice.distance (node, next);
          if (reached < along.cost[next]) {
            along.cost[next] = reached;
            along.from[next] = node;
            open.push ({ reached, static_cast<std::uint32_t> (next) });
          }
        }
      }
      for (std::size_t j = i + 1; j < places.size (); ++j) {
        const std::size_t to = node_at (lattice, places[j].x_m, places[j].z_m);
        if (!(along.cost[to] < infinite))
          continue;
        const float since =
          std::min (places[i].founded_years_ago, places[j].founded_years_ago);
        const bool alive = places[i].abandoned_years_ago == 0.0f &&
                           places[j].abandoned_years_ago == 0.0f;
        const float ended = std::max (places[i].abandoned_years_ago,
                                      places[j].abandoned_years_ago);
        const std::vector<std::size_t> path = trace (along, to);
        for (std::size_t step = 1; step < path.size (); ++step) {
          const auto found = marker.find (
            static_cast<std::uint64_t> (path[step - 1]) * lattice.size () +
            path[step]);
          if (found == marker.end ())
            continue;
          Road& road = roads[found->second];
          ++road.traffic;
          road.age_years = std::max (road.age_years, since);
          lived_on[found->second] = lived_on[found->second] || alive;
          last_used[found->second] = std::min (last_used[found->second], ended);
        }
      }
    }
    for (std::size_t road = 0; road < roads.size (); ++road)
      roads[road].abandoned_years_ago =
        lived_on[road] || !(last_used[road] < infinite) ? 0.0f
                                                        : last_used[road];
    finish_lines (lattice, planner.network.roads);
    return std::move (planner.network);
  }
}
