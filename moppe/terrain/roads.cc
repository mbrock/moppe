#include <moppe/terrain/roads.hh>

#include <moppe/correct_math.hh>
#include <moppe/parallel.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <queue>

namespace moppe::terrain {
  namespace {
    constexpr float infinite = std::numeric_limits<float>::infinity ();
    // What a step no mover may take costs, and a place not reached.
    constexpr meters_t unreachable = infinite * u::m;
    constexpr auto years = age[astronomy::Julian_year];

    using ForestCover = quantity<map::forest_cover[one], float>;
    using MireCover = quantity<map::sphagnum_abundance[one], float>;

    // How far one height stands above or below another.
    meters_t rise (SurfaceElevation from, SurfaceElevation to) {
      return quantity_cast<isq::height> (abs (to - from));
    }

    // A rise over its run, as a way's grade.
    grade_t grade_of (meters_t rise, meters_t run) {
      return quantity_cast<grade> (rise / run);
    }

    // The height a fraction `t` of the way from `a` to `b`, by std::lerp.
    SurfaceElevation
    interpolate (SurfaceElevation a, SurfaceElevation b, float t) {
      return surface_elevation_point (std::lerp (surface_elevation_value (a),
                                                 surface_elevation_value (b),
                                                 t) *
                                      u::m);
    }

    // A whole number of years, rounded half away from zero.
    age_t whole_years (age_t value) {
      return std::round (value.numerical_value_in (astronomy::Julian_year)) *
             years;
    }

    // How far a line turns from the way `from` to the way `to`, radians,
    // positive to the left.
    float turning (const Vec2& from, const Vec2& to) {
      return cr::atan2 (from.x () * to.z () - from.z () * to.x (),
                        from.x () * to.x () + from.z () * to.z ());
    }

    // The ground at full resolution, as the roads are built on it.
    struct FineGround {
      std::span<const SurfaceElevation> heights;
      const FloodField& flood;
      int width;
      int height;
      meters_t spacing_x;
      meters_t spacing_z;

      std::size_t cell (int x, int z) const {
        return static_cast<std::size_t> (wrap_index (z, height)) * width +
               static_cast<std::size_t> (wrap_index (x, width));
      }

      // Bilinear over the cells, wrapping.
      SurfaceElevation at (plan_position_t p) const {
        const float gx = static_cast<float> (x_of (p) / spacing_x);
        const float gz = static_cast<float> (z_of (p) / spacing_z);
        const float fx = std::floor (gx);
        const float fz = std::floor (gz);
        const int x = static_cast<int> (fx);
        const int z = static_cast<int> (fz);
        const float tx = gx - fx;
        const float tz = gz - fz;
        return interpolate (
          interpolate (heights[cell (x, z)], heights[cell (x + 1, z)], tx),
          interpolate (
            heights[cell (x, z + 1)], heights[cell (x + 1, z + 1)], tx),
          tz);
      }

      meters_t depth (plan_position_t p) const {
        return flood.water_depths ()[cell (
          static_cast<int> (
            std::lround (static_cast<float> (x_of (p) / spacing_x))),
          static_cast<int> (
            std::lround (static_cast<float> (z_of (p) / spacing_z))))];
      }
    };

    // The planning lattice: the world read every twenty metres or so, with
    // what a road needs to know of each node.
    struct Lattice {
      enum Water : std::uint8_t { Dry, Shallow, Deep, Sea };

      int width = 0;
      int height = 0;
      meters_t spacing_x {};
      meters_t spacing_z {};
      std::vector<SurfaceElevation> elevation;
      std::vector<std::uint8_t> water;
      // Zero off the rivers; on them, how dear a bridge is, growing with
      // the country the river drains.
      std::vector<scale_factor_t> river;
      std::vector<square_meters_t> catchment;
      // The steepest fall to a dry neighbour.
      std::vector<slope_t> steepness;
      // Per node and step (steps, below), how far the ground between the
      // two nodes stands off the straight line joining them, on average:
      // the cut and fill a road along that step would need, which the
      // nodes alone do not see (a spur crossing between them).
      std::vector<meters_t> earthwork;
      // What stands on the ground and how wet it is (RoadGround).
      std::vector<ForestCover> forest;
      std::vector<MireCover> mire;
      std::vector<SoilWetness> wetness;

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

      meters_t distance (std::size_t a, std::size_t b) const {
        return hypot (
          plan_displacement (delta (x (a), x (b), width) * spacing_x,
                             delta (y (a), y (b), height) * spacing_z));
      }

      bool dry (std::size_t node) const {
        return water[node] == Dry && river[node] == 0.0f * scale_factor[one];
      }

      // The world's period, as the lattice covers it.
      plan_extent_t period () const {
        return plan_extent (width * spacing_x, height * spacing_z);
      }

      // Where a node stands.
      plan_position_t at (std::size_t node) const {
        return plan_position (x (node) * spacing_x, y (node) * spacing_z);
      }
    };

    // Past half cover a mire swallows a road, and a road keeps out of it.
    constexpr MireCover impassable_mire = 0.5f * map::sphagnum_abundance[one];
    constexpr square_meters_t square_kilometre = 1.0e6f * u::m * u::m;

    // How far `level` stands above `base` (below, if negative).
    meters_t above (SurfaceElevation level, SurfaceElevation base) {
      return quantity_cast<isq::height> (level - base);
    }

    // Half way between two heights.
    SurfaceElevation midway (SurfaceElevation a, SurfaceElevation b) {
      return SurfaceElevation (0.5f *
                               (a.delta_from_zero () + b.delta_from_zero ()));
    }

    // `wa` of the way to `a` and `wb` of the way to `b`, from the origin.
    plan_position_t
    weighted (float wa, plan_position_t a, float wb, plan_position_t b) {
      return plan_position_t (wa * a.delta_from_zero () +
                              wb * b.delta_from_zero ());
    }

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
                          square_meters_t river_area,
                          SurfaceElevation sea_level) {
      const int source_width = static_cast<int> (domain.width ());
      const int source_height = static_cast<int> (domain.height ());
      const meters_t source_x = domain.spacing_x ();
      const meters_t source_z = domain.spacing_z ();
      constexpr meters_t spacing = 20.0f * u::m;
      Lattice lattice;
      lattice.width =
        std::clamp (static_cast<int> (std::round (
                      static_cast<float> (source_width * source_x / spacing))),
                    std::min (8, source_width),
                    source_width);
      lattice.height =
        std::clamp (static_cast<int> (std::round (
                      static_cast<float> (source_height * source_z / spacing))),
                    std::min (8, source_height),
                    source_height);
      lattice.spacing_x = source_width * source_x / lattice.width;
      lattice.spacing_z = source_height * source_z / lattice.height;
      const std::size_t count =
        static_cast<std::size_t> (lattice.width) * lattice.height;
      lattice.elevation.resize (count);
      lattice.water.resize (count);
      lattice.river.assign (count, 0.0f * scale_factor[one]);
      lattice.catchment.resize (count);
      lattice.steepness.resize (count);
      lattice.forest.assign (count, 0.0f * map::forest_cover[one]);
      lattice.mire.assign (count, 0.0f * map::sphagnum_abundance[one]);
      lattice.wetness.assign (count, 0.0f * soil_wetness[one]);

      const auto source_cell = [&] (int x, int y) {
        return static_cast<std::size_t> (y * source_height / lattice.height) *
                 source_width +
               static_cast<std::size_t> (x * source_width / lattice.width);
      };
      for (int y = 0; y < lattice.height; ++y)
        for (int x = 0; x < lattice.width; ++x) {
          const std::size_t node = lattice.node (x, y);
          const std::size_t cell = source_cell (x, y);
          lattice.elevation[node] = elevations[cell];
          const StandingWaterDepth depth = flood.water_depths ()[cell];
          // The sea is the water standing at its level, as the map reads it.
          lattice.water[node] =
            depth <= 1e-3f * u::m ? Lattice::Dry
            : flood.water_levels ()[cell] < sea_level + 0.35f * u::m
              ? Lattice::Sea
            : depth > 1.5f * u::m ? Lattice::Deep
                                  : Lattice::Shallow;
          lattice.catchment[node] = drainage.contributing_area_at (cell);
          const auto read =
            [cell]<typename Cover> (std::span<const Cover> field) {
              return cell < field.size () ? std::clamp (field[cell],
                                                        0.0f * Cover::reference,
                                                        1.0f * Cover::reference)
                                          : 0.0f * Cover::reference;
            };
          lattice.forest[node] = read (ground.forest_cover);
          lattice.mire[node] = read (ground.mire);
          lattice.wetness[node] = read (ground.wetness);
        }

      // Every river cell marks the node it falls in, and the band is
      // widened by a node either side so that no step of the search can
      // leap a river without paying for its bridge.
      std::vector<scale_factor_t> marked (count, 0.0f * scale_factor[one]);
      for (int cy = 0; cy < source_height; ++cy)
        for (int cx = 0; cx < source_width; ++cx) {
          const std::size_t cell =
            static_cast<std::size_t> (cy) * source_width + cx;
          const square_meters_t area = drainage.contributing_area_at (cell);
          if (area < river_area || flood.water_depths ()[cell] > 1e-3f * u::m)
            continue;
          const std::size_t node =
            lattice.node (cx * lattice.width / source_width,
                          cy * lattice.height / source_height);
          marked[node] = std::max (
            marked[node],
            (1.0f + 0.5f * cr::log2 (static_cast<float> (area / river_area))) *
              scale_factor[one]);
        }
      for (int y = 0; y < lattice.height; ++y)
        for (int x = 0; x < lattice.width; ++x) {
          scale_factor_t strongest = 0.0f * scale_factor[one];
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
              strongest =
                std::max (strongest, marked[lattice.node (x + dx, y + dy)]);
          lattice.river[lattice.node (x, y)] = strongest;
        }

      for (std::size_t node = 0; node < count; ++node) {
        slope_t steepest = 0.0f * terrain_slope[one];
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
              quantity_cast<terrain_slope> (
                rise (lattice.elevation[node], lattice.elevation[other]) /
                lattice.distance (node, other)));
          }
        lattice.steepness[node] = steepest;
      }

      // Bilinear over the source, wrapping.
      const FineGround source { elevations,    flood,    source_width,
                                source_height, source_x, source_z };
      lattice.earthwork.assign (count * steps.size (), 0.0f * u::m);
      parallel_for (count, 2048, [&] (size_t begin, size_t end) {
        for (std::size_t node = begin; node < end; ++node) {
          const meters_t ax =
            static_cast<float> (static_cast<int> (
              source_cell (lattice.x (node), lattice.y (node)) %
              source_width)) *
            source_x;
          const meters_t az =
            static_cast<float> (static_cast<int> (
              source_cell (lattice.x (node), lattice.y (node)) /
              source_width)) *
            source_z;
          const plan_position_t a = plan_position (ax, az);
          for (std::size_t k = 0; k < steps.size (); ++k) {
            const std::size_t other = lattice.node (
              lattice.x (node) + steps[k][0], lattice.y (node) + steps[k][1]);
            const plan_displacement_t d = plan_displacement (
              steps[k][0] * lattice.spacing_x, steps[k][1] * lattice.spacing_z);
            const int samples =
              std::max (2,
                        static_cast<int> (std::ceil (static_cast<float> (
                          hypot (d) / std::max (source_x, source_z)))));
            meters_t off = 0.0f * u::m;
            for (int i = 1; i < samples; ++i) {
              const float t = static_cast<float> (i) / samples;
              const SurfaceElevation line = interpolate (
                lattice.elevation[node], lattice.elevation[other], t);
              off += rise (line, source.at (a + d * t));
            }
            lattice.earthwork[node * steps.size () + k] = off / (samples - 1);
          }
        }
      });
      return lattice;
    }

    enum Way : std::uint8_t { None, Path, Built };

    // How dear each mover finds a step from a to b of the given run, as
    // the length of easy road it is worth.
    struct Mover {
      enum class Kind { Cart, Walker } kind;
      // How much less an existing way costs to follow than to build.
      proportion_t reuse = 1.0f * proportion[one];

      // The terrain price of a directed edge is shared by every search.
      // Only the reuse discount changes as roads are laid.
      std::vector<meters_t> price;
      Mover (const Lattice& lattice, Kind kind, proportion_t reuse);

      meters_t cost (std::size_t node, std::size_t k, std::uint8_t way) const {
        const meters_t base = price[node * steps.size () + k];
        const bool reused = kind == Kind::Cart ? way == Built : way != None;
        return reused ? meters_t (base * reuse) : base;
      }

      meters_t step (const Lattice& lattice,
                     std::size_t a,
                     std::size_t b,
                     meters_t run,
                     meters_t earthwork = 0.0f * u::m) const {
        // Each metre the ground stands off the step's line costs a metre.
        constexpr per_metre_t per_earthwork = 1.0f / u::m;
        const grade_t pitch =
          grade_of (rise (lattice.elevation[a], lattice.elevation[b]), run);
        meters_t cost = 0.0f * u::m;
        if (kind == Kind::Cart) {
          if (lattice.water[a] >= Lattice::Deep ||
              lattice.water[b] >= Lattice::Deep || pitch > 0.16f * grade[one])
            return unreachable;
          // A mire swallows a road: past half cover it is not built on,
          // and below that it is dear (Galin et al. 2010's terms, with
          // forest for their tree density).
          if (lattice.mire[b] > impassable_mire)
            return unreachable;
          const auto steep = pitch / (0.06f * grade[one]);
          cost = run * (1.0f + steep * steep);
          if (lattice.water[b] == Lattice::Shallow)
            cost *= 20.0f;
          cost += quantity_cast<isq::length> (run * 4.0f * lattice.river[b]);
          cost += meters_t (
            run * (1.5f * lattice.forest[b] + 6.0f * lattice.mire[b] +
                   1.0f * lattice.wetness[b] + per_earthwork * earthwork));
        } else {
          if (lattice.water[b] != Lattice::Dry || pitch > 0.5f * grade[one])
            return unreachable;
          const auto steep = pitch / (0.2f * grade[one]);
          cost = run * (1.0f + steep * steep);
          cost += quantity_cast<isq::length> (run * 1.5f * lattice.river[b]);
          cost +=
            meters_t (run * (0.4f * lattice.forest[b] + 2.0f * lattice.mire[b] +
                             0.3f * lattice.wetness[b]));
        }
        return cost;
      }
    };

    struct Search {
      std::vector<meters_t> cost;
      std::vector<std::uint32_t> from;
    };

    // The open list of a search: the cheapest node reached first.
    using Entry = std::pair<meters_t, std::uint32_t>;
    using Open = std::priority_queue<Entry, std::vector<Entry>, std::greater<>>;

    // What the mover pays for step k (of steps) from a node, unreachable
    // where it may not go.
    meters_t terrain_cost (const Lattice& lattice,
                           const Mover& mover,
                           std::size_t node,
                           std::size_t k) {
      const int x = lattice.x (node);
      const int y = lattice.y (node);
      const auto [dx, dy] = steps[k];
      const std::size_t next = lattice.node (x + dx, y + dy);
      const meters_t run = hypot (
        plan_displacement (dx * lattice.spacing_x, dy * lattice.spacing_z));
      meters_t step = mover.step (
        lattice, node, next, run, lattice.earthwork[node * steps.size () + k]);
      // A knight's move passes over two cells on its way: both must be
      // passable too, or it would cut the corner of a mire or a lake.
      if (step < unreachable && (std::abs (dx) == 2 || std::abs (dy) == 2)) {
        const int mx = std::abs (dx) == 2 ? dx / 2 : 0;
        const int my = std::abs (dy) == 2 ? dy / 2 : 0;
        for (const std::size_t over :
             { lattice.node (x + mx, y + my),
               lattice.node (x + (mx ? mx : dx), y + (my ? my : dy)) })
          if (!(mover.step (lattice, node, over, 0.5f * run) < unreachable))
            step = unreachable;
      }
      return step;
    }

    Mover::Mover (const Lattice& lattice, Kind type, proportion_t discount)
        : kind (type), reuse (discount),
          price (lattice.size () * steps.size ()) {
      parallel_for (lattice.size (), 2048, [&] (size_t begin, size_t end) {
        for (size_t node = begin; node < end; ++node)
          for (size_t k = 0; k < steps.size (); ++k)
            price[node * steps.size () + k] =
              terrain_cost (lattice, *this, node, k);
      });
    }

    Search search (const Lattice& lattice,
                   const std::vector<std::uint8_t>& ways,
                   const std::vector<std::size_t>& sources,
                   const Mover& mover) {
      Search result { std::vector<meters_t> (lattice.size (), unreachable),
                      std::vector<std::uint32_t> (lattice.size ()) };
      Open open;
      for (const std::size_t source : sources) {
        result.cost[source] = 0.0f * u::m;
        result.from[source] = static_cast<std::uint32_t> (source);
        open.push ({ 0.0f * u::m, static_cast<std::uint32_t> (source) });
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
          const meters_t step = mover.cost (node, k, ways[next]);
          if (!(step < unreachable))
            continue;
          const meters_t reached = cost + step;
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

    // The cheapest way once round the world from `source` back to it, the
    // way round `axis` (0 across x, 1 across z) and back: a search over two
    // copies of the lattice joined end to end round that axis, where a step
    // over the seam passes from one copy to the other, from the source on
    // the first to the source on the second. Its nodes in order, the source
    // at both ends, and its cost; no nodes if there is no way round.
    std::pair<std::vector<std::size_t>, meters_t>
    search_round (const Lattice& lattice,
                  const std::vector<std::uint8_t>& ways,
                  std::size_t source,
                  const Mover& mover,
                  int axis) {
      const std::size_t size = lattice.size ();
      std::vector<meters_t> cost (2 * size, unreachable);
      std::vector<std::uint32_t> from (2 * size);
      Open open;
      const std::size_t target = source + size;
      cost[source] = 0.0f * u::m;
      from[source] = static_cast<std::uint32_t> (source);
      open.push ({ 0.0f * u::m, static_cast<std::uint32_t> (source) });
      while (!open.empty ()) {
        const auto [reached_cost, lifted] = open.top ();
        open.pop ();
        if (reached_cost > cost[lifted])
          continue;
        if (lifted == target)
          break;
        const std::size_t node = lifted % size;
        const std::size_t lap = lifted / size;
        for (std::size_t k = 0; k < steps.size (); ++k) {
          const int along = axis == 0 ? lattice.x (node) + steps[k][0]
                                      : lattice.y (node) + steps[k][1];
          const int period = axis == 0 ? lattice.width : lattice.height;
          const std::size_t next =
            lattice.node (lattice.x (node) + steps[k][0],
                          lattice.y (node) + steps[k][1]) +
            size * ((along < 0 || along >= period) ? 1 - lap : lap);
          const meters_t step = mover.cost (node, k, ways[next % size]);
          if (!(step < unreachable))
            continue;
          const meters_t reached = reached_cost + step;
          if (reached < cost[next]) {
            cost[next] = reached;
            from[next] = lifted;
            open.push ({ reached, static_cast<std::uint32_t> (next) });
          }
        }
      }
      if (!(cost[target] < unreachable))
        return { {}, unreachable };
      std::vector<std::size_t> path;
      for (std::size_t lifted = target;; lifted = from[lifted]) {
        path.push_back (lifted % size);
        if (lifted == source)
          break;
      }
      std::ranges::reverse (path);
      return { std::move (path), cost[target] };
    }

    meters_t path_length (const Lattice& lattice,
                          const std::vector<std::size_t>& path) {
      meters_t length = 0.0f * u::m;
      for (std::size_t i = 1; i < path.size (); ++i)
        length += lattice.distance (path[i - 1], path[i]);
      return length;
    }

    std::vector<plan_position_t>
    unwrap (const Lattice& lattice, const std::vector<std::size_t>& nodes) {
      std::vector<plan_position_t> points;
      int x = lattice.x (nodes.front ());
      int y = lattice.y (nodes.front ());
      for (std::size_t i = 0; i < nodes.size (); ++i) {
        if (i > 0) {
          x += lattice.delta (
            lattice.x (nodes[i - 1]), lattice.x (nodes[i]), lattice.width);
          y += lattice.delta (
            lattice.y (nodes[i - 1]), lattice.y (nodes[i]), lattice.height);
        }
        points.push_back (
          plan_position (x * lattice.spacing_x, y * lattice.spacing_z));
      }
      // Two rounds of corner cutting take the lattice's angles off.
      for (int round = 0; round < 2 && points.size () > 2; ++round) {
        std::vector<plan_position_t> smooth { points.front () };
        for (std::size_t i = 0; i + 1 < points.size (); ++i) {
          const plan_position_t a = points[i];
          const plan_position_t b = points[i + 1];
          smooth.push_back (weighted (0.75f, a, 0.25f, b));
          smooth.push_back (weighted (0.25f, a, 0.75f, b));
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

      // Lays a way round the world (search_round's) whole, as the loop.
      void lay_round (const std::vector<std::size_t>& path) {
        Road road { Road::Kind::Road, unwrap (lattice, path) };
        road.loop = true;
        network.roads.push_back (std::move (road));
        road_nodes.push_back (path);
        for (const std::size_t node : path)
          ways[node] = Built;
      }

      RoadPlace place (RoadPlace::Kind kind, std::size_t node) const {
        return { kind, lattice.at (node), lattice.elevation[node] };
      }

      // Builds a road from the network to the node; false if none reaches.
      bool connect (std::size_t node, const Search& from_network) {
        if (!(from_network.cost[node] < unreachable))
          return false;
        lay (trace (from_network, node), Road::Kind::Road);
        return true;
      }
    };

    meters_t torus_distance (const Lattice& lattice, RoadPlace a, RoadPlace b) {
      return hypot (minimum_image (a.at - b.at, lattice.period ()));
    }

    // The node nearest a point.
    std::size_t node_at (const Lattice& lattice, plan_position_t at) {
      return lattice.node (
        static_cast<int> (
          std::lround (static_cast<float> (x_of (at) / lattice.spacing_x))),
        static_cast<int> (
          std::lround (static_cast<float> (z_of (at) / lattice.spacing_z))));
    }

    // Walks the line from a point some way above one node toward another
    // and says whether the ground keeps out of it.
    bool sees (const Lattice& lattice,
               std::size_t from,
               meters_t eye,
               std::size_t to,
               meters_t target) {
      const float dx = static_cast<float> (
        lattice.delta (lattice.x (from), lattice.x (to), lattice.width));
      const float dy = static_cast<float> (
        lattice.delta (lattice.y (from), lattice.y (to), lattice.height));
      const SurfaceElevation a = lattice.elevation[from] + eye;
      const SurfaceElevation b = lattice.elevation[to] + target;
      const int count = static_cast<int> (
        std::ceil (std::max (std::fabs (dx), std::fabs (dy))));
      for (int i = 1; i < count; ++i) {
        const float t = static_cast<float> (i) / count;
        const std::size_t node = lattice.node (
          lattice.x (from) + static_cast<int> (std::lround (dx * t)),
          lattice.y (from) + static_cast<int> (std::lround (dy * t)));
        if (lattice.elevation[node] > a + (b - a) * t + 0.5f * u::m)
          return false;
      }
      return true;
    }

    // How much low, workable country a sanctuary here would see and be
    // seen from: Peatfield's rule that a peak sanctuary sees the region it
    // serves, whose fires are lit to be seen from the homes below, mostly
    // within an hour's walk. In millions of metres of country, each ray's
    // reach weighed toward its middle.
    scale_factor_t regard (const Lattice& lattice, std::size_t site) {
      constexpr int rays = 64;
      constexpr meters_t step = 40.0f * u::m;
      constexpr meters_t reach = 4500.0f * u::m;
      const SurfaceElevation eye = lattice.elevation[site] + 4.0f * u::m;
      meters_t score = 0.0f * u::m;
      for (int ray = 0; ray < rays; ++ray) {
        const radians_t angle = 2.0f * std::numbers::pi_v<float> *
                                static_cast<float> (ray) / rays * u::rad;
        const per_metre_t ux = moppe::cos (angle) / lattice.spacing_x;
        const per_metre_t uy = moppe::sin (angle) / lattice.spacing_z;
        grade_t horizon = -infinite * grade[one];
        for (meters_t d = 60.0f * u::m; d < reach; d += step) {
          const std::size_t node = lattice.node (
            lattice.x (site) +
              static_cast<int> (std::lround (static_cast<float> (ux * d))),
            lattice.y (site) +
              static_cast<int> (std::lround (static_cast<float> (uy * d))));
          const SurfaceElevation ground = lattice.elevation[node];
          const bool served =
            lattice.water[node] == Lattice::Dry &&
            lattice.steepness[node] < 0.08f * terrain_slope[one] &&
            ground < eye - 30.0f * u::m;
          if (served &&
              grade_of (above (ground + 1.5f * u::m, eye), d) >= horizon)
            score += d * (1.0f - d / reach);
          horizon = std::max (horizon, grade_of (above (ground, eye), d));
        }
      }
      return quantity_cast<scale_factor> (score / (1.0e6f * u::m));
    }

    struct Sanctuary {
      std::size_t node;
      scale_factor_t score;
    };

    std::vector<Sanctuary> site_sanctuaries (const Lattice& lattice,
                                             SurfaceElevation sea_level) {
      // The highest node of each 200 m block, and those blocks higher than
      // every other within a kilometre: the summits.
      const int block =
        std::max (1,
                  static_cast<int> (std::lround (
                    static_cast<float> (200.0f * u::m / lattice.spacing_x))));
      const int across = std::max (1, lattice.width / block);
      const int down = std::max (1, lattice.height / block);
      std::vector<std::size_t> tops (static_cast<std::size_t> (across) * down,
                                     0);
      std::vector<SurfaceElevation> top_height (
        tops.size (), surface_elevation_point (-infinite * u::m));
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
          if (top_height[b] - sea_level < 40.0f * u::m)
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
          SurfaceElevation lowest = surface_elevation_point (infinite * u::m);
          const int reach = static_cast<int> (
            static_cast<float> (1500.0f * u::m / lattice.spacing_x));
          for (int dy = -reach; dy <= reach; dy += 5)
            for (int dx = -reach; dx <= reach; dx += 5)
              lowest = std::min (
                lowest,
                lattice.elevation[lattice.node (lattice.x (tops[b]) + dx,
                                                lattice.y (tops[b]) + dy)]);
          if (top_height[b] - lowest >= 50.0f * u::m)
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
            const int reach = static_cast<int> (
              static_cast<float> (500.0f * u::m / lattice.spacing_x));
            Sanctuary best { summit, regard (lattice, summit) };
            for (int dy = -reach; dy <= reach; dy += 3)
              for (int dx = -reach; dx <= reach; dx += 3) {
                if (dx * dx + dy * dy > reach * reach || (dx == 0 && dy == 0))
                  continue;
                const std::size_t node = lattice.node (lattice.x (summit) + dx,
                                                       lattice.y (summit) + dy);
                if (lattice.water[node] != Lattice::Dry ||
                    lattice.elevation[node] <
                      lattice.elevation[summit] - 45.0f * u::m)
                  continue;
                const scale_factor_t score = regard (lattice, node);
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
      // kilometres. A fire is lit to be answered, so a site in sight of
      // one already kept counts for more than one that sees none of them
      // (Peatfield's intervisible sanctuaries): each next site is the best
      // by its regard, a site out of sight of every other taken at a
      // third of it.
      const square_meters_t area =
        lattice.width * lattice.spacing_x * lattice.height * lattice.spacing_z;
      const std::size_t limit = static_cast<std::size_t> (std::max (
        2.0f,
        std::round (static_cast<float> (area / square_kilometre / 12.0f))));
      constexpr proportion_t alone = 0.35f * proportion[one];
      std::vector<Sanctuary> kept;
      std::vector<bool> taken (sites.size (), false);
      while (kept.size () < limit) {
        std::size_t best = sites.size ();
        scale_factor_t best_worth = 0.0f * scale_factor[one];
        for (std::size_t i = 0; i < sites.size (); ++i) {
          const Sanctuary& site = sites[i];
          if (taken[i] ||
              (!kept.empty () && site.score < 0.1f * kept.front ().score))
            continue;
          bool crowded = false;
          bool answered = kept.empty ();
          for (const Sanctuary& other : kept) {
            const meters_t apart = lattice.distance (site.node, other.node);
            crowded = crowded || apart < 1400.0f * u::m;
            answered =
              answered ||
              (apart < 6000.0f * u::m &&
               sees (lattice, site.node, 4.0f * u::m, other.node, 4.0f * u::m));
          }
          const scale_factor_t worth = quantity_cast<scale_factor> (
            site.score * (answered ? 1.0f * proportion[one] : alone));
          if (!crowded && worth > best_worth) {
            best = i;
            best_worth = worth;
          }
        }
        if (best == sites.size () ||
            (!kept.empty () && best_worth < 0.3f * kept.front ().score))
          break;
        taken[best] = true;
        kept.push_back (sites[best]);
      }
      return kept;
    }

    // Lakes worth a road, largest first, each by the dry node where its
    // water leaves: the neighbour draining the most country. A lake of a
    // few hectares is worth one, and so is a pond where a river leaves
    // it, which is a millpond made.
    std::vector<std::size_t> lake_outlets (const Lattice& lattice,
                                           square_meters_t river_area) {
      std::vector<std::int32_t> label (lattice.size (), -1);
      struct Lake {
        std::size_t cells = 0;
        std::size_t outlet = 0;
        square_meters_t outlet_area = -1.0f * u::m * u::m;
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
                if (lattice.catchment[next] > lake.outlet_area &&
                    lattice.steepness[next] < 0.2f * terrain_slope[one]) {
                  lake.outlet_area = lattice.catchment[next];
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
      const square_meters_t node_area = lattice.spacing_x * lattice.spacing_z;
      const square_meters_t world_area =
        static_cast<float> (lattice.size ()) * node_area;
      const std::size_t limit = static_cast<std::size_t> (
        std::max (2.0f,
                  std::round (static_cast<float> (world_area /
                                                  square_kilometre / 10.0f))));
      std::vector<std::size_t> outlets;
      for (const Lake& lake : lakes) {
        if (outlets.size () >= limit)
          break;
        const square_meters_t area =
          static_cast<float> (lake.cells) * node_area;
        if (lake.outlet_area >= 0.0f * u::m * u::m &&
            (area >= 4.0e4f * u::m * u::m ||
             (area >= 8.0e3f * u::m * u::m && lake.outlet_area >= river_area)))
          outlets.push_back (lake.outlet);
      }
      return outlets;
    }

    // A well-mixed hash of a few numbers (lowbias32), read as [0, 1).
    proportion_t
    roll (std::uint32_t seed, std::size_t node, std::uint32_t salt) {
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
      return static_cast<float> (x >> 8) / static_cast<float> (1u << 24) *
             proportion[one];
    }

    // When a place of each kind was founded and how likely, and how long
    // ago, it went quiet. The sanctuaries are older than any farm and
    // outlive the people who lit their fires (Peatfield 1983); the
    // trailhead is this century's.
    void roll_history (RoadPlace& place, std::size_t node, std::uint32_t seed) {
      struct History {
        age_t founded_low, founded_high;
        probability_t abandon_chance;
        age_t abandoned_low, abandoned_high;
      };
      constexpr auto chance = probability[one];
      History history {};
      switch (place.kind) {
      case RoadPlace::Kind::Trailhead:
        history = {
          25.0f * years, 60.0f * years, 0.0f * chance,
          0.0f * years,  0.0f * years,
        };
        break;
      case RoadPlace::Kind::Landing:
        history = {
          250.0f * years, 600.0f * years, 0.25f * chance,
          15.0f * years,  150.0f * years,
        };
        break;
      case RoadPlace::Kind::LakeOutlet:
        history = {
          150.0f * years, 500.0f * years, 0.4f * chance,
          10.0f * years,  120.0f * years,
        };
        break;
      case RoadPlace::Kind::Sanctuary:
        history = {
          1500.0f * years, 3500.0f * years, 0.6f * chance,
          150.0f * years,  1500.0f * years,
        };
        break;
      }
      const auto between = [&] (age_t low, age_t high, std::uint32_t salt) {
        return whole_years (low +
                            age_t ((high - low) * roll (seed, node, salt)));
      };
      place.founded = between (history.founded_low, history.founded_high, 1);
      place.abandoned =
        // The roll, drawn against the chance the place was given up.
        quantity_cast<probability> (quantity_cast<dimensionless> (
          roll (seed, node, 2))) < history.abandon_chance
          ? std::min (
              between (history.abandoned_low, history.abandoned_high, 3),
              whole_years (0.9f * place.founded))
          : 0.0f * years;
    }

    // The polyline again at about `step` between points, its ends kept.
    std::vector<plan_position_t>
    resample (const std::vector<plan_position_t>& line, meters_t step) {
      meters_t length = 0.0f * u::m;
      for (std::size_t i = 1; i < line.size (); ++i)
        length += hypot (line[i] - line[i - 1]);
      const int count = std::max (
        1, static_cast<int> (std::ceil (static_cast<float> (length / step))));
      const meters_t spacing = length / static_cast<float> (count);
      std::vector<plan_position_t> result { line.front () };
      std::size_t segment = 1;
      meters_t segment_start = 0.0f * u::m;
      for (int k = 1; k < count; ++k) {
        const meters_t at = spacing * static_cast<float> (k);
        meters_t segment_length = 0.0f * u::m;
        while (segment < line.size ()) {
          segment_length = hypot (line[segment] - line[segment - 1]);
          if (segment_start + segment_length >= at)
            break;
          segment_start += segment_length;
          ++segment;
        }
        if (segment >= line.size ())
          break;
        const proportion_t t =
          segment_length > 0.0f * u::m
            ? quantity_cast<proportion> ((at - segment_start) / segment_length)
            : 0.0f * proportion[one];
        const plan_position_t a = line[segment - 1];
        const plan_position_t b = line[segment];
        result.push_back (a + plan_displacement_t ((b - a) * t));
      }
      result.push_back (line.back ());
      return result;
    }

    // Eases the lattice's turns into curves: each point moves to the mean
    // of its neighbours within `radius`, the window narrowing toward the
    // ends, which stay put.
    template <typename Passable>
    void ease (std::vector<plan_position_t>& line,
               int radius,
               int passes,
               const Passable& passable) {
      const int n = static_cast<int> (line.size ());
      for (int pass = 0; pass < passes; ++pass) {
        std::vector<plan_position_t> eased = line;
        for (int i = 1; i + 1 < n; ++i) {
          const int r = std::min ({ radius, i, n - 1 - i });
          plan_displacement_t sum = plan_displacement (Vec2 (0.0f, 0.0f));
          for (int j = i - r; j <= i + r; ++j)
            sum += line[static_cast<std::size_t> (j)].delta_from_zero ();
          const plan_position_t point (sum / static_cast<float> (2 * r + 1));
          // A curve may not cut a corner into water or mire.
          if (passable (point))
            eased[static_cast<std::size_t> (i)] = point;
        }
        line = std::move (eased);
      }
    }

    // The nearest point to `at` on an earlier road, across the seam, and
    // how far it lies.
    std::pair<plan_position_t, meters_t>
    nearest_on (const Road& road, plan_position_t at, plan_extent_t period) {
      plan_position_t best = at;
      meters_t best_distance = unreachable;
      for (std::size_t i = 1; i < road.points.size (); ++i) {
        const plan_displacement_t a =
          minimum_image (road.points[i - 1] - at, period);
        const plan_displacement_t d = road.points[i] - road.points[i - 1];
        const square_meters_t length2 =
          x_of (d) * x_of (d) + z_of (d) * z_of (d);
        const proportion_t t =
          length2 > 0.0f * u::m * u::m
            ? std::clamp (
                quantity_cast<proportion> (
                  -(x_of (a) * x_of (d) + z_of (a) * z_of (d)) / length2),
                0.0f * proportion[one],
                1.0f * proportion[one])
            : 0.0f * proportion[one];
        const plan_displacement_t p = a + plan_displacement_t (d * t);
        const meters_t distance = hypot (p);
        if (distance < best_distance) {
          best_distance = distance;
          best = at + p;
        }
      }
      return { best, best_distance };
    }

    // Moves a line's end onto `target`, carrying the first stretch of the
    // line with it and less the further from the end.
    void shift_end (std::vector<plan_position_t>& line,
                    bool back,
                    plan_position_t target) {
      const plan_position_t end = back ? line.back () : line.front ();
      const plan_displacement_t shift = target - end;
      // A long way to move is eased over a longer stretch.
      const meters_t reach = std::max (40.0f * u::m, 2.0f * hypot (shift));
      meters_t along = 0.0f * u::m;
      const std::size_t n = line.size ();
      for (std::size_t k = 0; k < n; ++k) {
        plan_position_t& point = line[back ? n - 1 - k : k];
        if (k > 0) {
          const plan_position_t& previous = line[back ? n - k : k - 1];
          along += hypot (point - previous);
        }
        if (along >= reach)
          break;
        const auto t = 1.0f - along / reach;
        const proportion_t weight = t * t * (3.0f - 2.0f * t);
        point += plan_displacement_t (shift * weight);
      }
    }

    // How far apart follow_contours takes its stations along the line.
    constexpr meters_t contour_station = 8.0f * u::m;

    // Lays a cart road's line again over the ground at full resolution,
    // within a corridor either side of the line the lattice found: the
    // way along it that climbs least, so that it winds into a gully to
    // cross it where it is shallow and round a spur rather than through
    // it, as a road holding its grade along a hillside does. The line is
    // taken in stations along the old one, each with offsets across it; a
    // step goes from one station to the next, turning by at most one
    // offset's slope at a time, and the ends stay where they were.
    // `target`, if given, is the height the road can hold at each station,
    // and the line is drawn toward ground at that height.
    std::vector<plan_position_t>
    follow_contours (const Lattice& lattice,
                     const FineGround& ground,
                     const std::vector<plan_position_t>& spine,
                     std::span<const SurfaceElevation> target) {
      constexpr meters_t station = contour_station;
      constexpr meters_t across = 1.5f * u::m;
      constexpr int half = 40;
      constexpr int lean = 5;
      // The tightest bend a cart takes.
      constexpr meters_t radius = 15.0f * u::m;
      // A turn costs as a little more road.
      constexpr auto turn_cost = 10.0f * u::m / (u::rad * u::rad);
      constexpr int states = 2 * lean + 1;
      constexpr int offsets = 2 * half + 1;
      const int n = static_cast<int> (spine.size ());
      if (n < 4)
        return spine;

      // The way across at each station, from the line's direction over
      // some forty metres, and how sharply it turns, so that no offset
      // lies beyond the centre of the bend where the stations' offsets
      // would fold over one another.
      std::vector<plan_direction_t> normal (n);
      for (int i = 0; i < n; ++i) {
        const plan_displacement_t d =
          spine[std::min (n - 1, i + 5)] - spine[std::max (0, i - 5)];
        const meters_t length = std::max (hypot (d), 1e-3f * u::m);
        normal[i] =
          plan_unit_direction (Vec2 (static_cast<float> (-z_of (d) / length),
                                     static_cast<float> (x_of (d) / length)));
      }
      std::vector<int> lowest (n, -half);
      std::vector<int> highest (n, half);
      for (int i = 1; i + 1 < n; ++i) {
        const per_metre_t curvature =
          turning (plan_value (normal[i - 1]), plan_value (normal[i + 1])) /
          (2.0f * station);
        if (abs (curvature) > 1e-4f / u::m) {
          const int limit = static_cast<int> (
            static_cast<float> (0.5f / (abs (curvature) * across)));
          // The bend's centre lies to the side the normal turns away from.
          if (curvature > 0.0f / u::m)
            highest[i] = std::min (half, limit);
          else
            lowest[i] = std::max (-half, -limit);
        }
      }
      const auto position = [&] (int i, int j) {
        return spine[i] + along (normal[i], across) * static_cast<float> (j);
      };
      // What standing at an offset costs per metre, or impassable.
      constexpr scale_factor_t impassable = infinite * scale_factor[one];
      std::vector<scale_factor_t> place (static_cast<std::size_t> (n) *
                                         offsets);
      std::vector<SurfaceElevation> elevation (place.size ());
      for (int i = 0; i < n; ++i)
        for (int j = -half; j <= half; ++j) {
          const plan_position_t p = position (i, j);
          const std::size_t k = static_cast<std::size_t> (i) * offsets +
                                static_cast<std::size_t> (j + half);
          elevation[k] = ground.at (p);
          const std::size_t node = node_at (lattice, p);
          const meters_t depth = ground.depth (p);
          // Ground standing off the height the road can hold there asks
          // for a cutting or a bank: by each two metres, squared.
          const auto off = target.empty ()
                             ? 0.0f * one
                             : rise (target[i], elevation[k]) / (2.0f * u::m);
          place[k] = j < lowest[i] || j > highest[i] || depth > 1.5f * u::m ||
                         lattice.mire[node] > impassable_mire
                       ? impassable
                       : quantity_cast<scale_factor> (
                           (depth > 1e-3f * u::m ? 19.0f : 0.0f) + off * off +
                           4.0f * quantity_cast<mp_units::dimensionless> (
                                    lattice.river[node]) +
                           1.5f * quantity_cast<mp_units::dimensionless> (
                                    lattice.forest[node]) +
                           6.0f * quantity_cast<mp_units::dimensionless> (
                                    lattice.mire[node]) +
                           1.0f * quantity_cast<mp_units::dimensionless> (
                                    lattice.wetness[node]));
        }

      // Over the stations: the cheapest way to each offset arriving with
      // each lean, the offsets it moved across in its last step.
      const std::size_t width = static_cast<std::size_t> (offsets) * states;
      std::vector<meters_t> cost (static_cast<std::size_t> (n) * width,
                                  unreachable);
      std::vector<std::int8_t> came (cost.size (), 0);
      cost[static_cast<std::size_t> (half) * states + lean] = 0.0f * u::m;
      for (int i = 0; i + 1 < n; ++i)
        for (int j = -half; j <= half; ++j)
          for (int d = -lean; d <= lean; ++d) {
            const meters_t here =
              cost[static_cast<std::size_t> (i) * width +
                   static_cast<std::size_t> (j + half) * states + (d + lean)];
            if (!(here < unreachable))
              continue;
            const plan_position_t a = position (i, j);
            const SurfaceElevation ha =
              elevation[static_cast<std::size_t> (i) * offsets +
                        static_cast<std::size_t> (j + half)];
            // Where the last step came from, so the turn is the line's own.
            const plan_position_t before = i > 0 ? position (i - 1, j - d) : a;
            for (int e = std::max (-lean, d - 2); e <= std::min (lean, d + 2);
                 ++e) {
              const int to = j + e;
              if (to < -half || to > half)
                continue;
              const std::size_t k = static_cast<std::size_t> (i + 1) * offsets +
                                    static_cast<std::size_t> (to + half);
              if (!(place[k] < impassable))
                continue;
              const plan_position_t b = position (i + 1, to);
              const meters_t run = std::max (hypot (b - a), 0.5f * u::m);
              radians_t turn = 0.0f * u::rad;
              if (i > 0) {
                turn = std::fabs (turning (plan_value (a - before),
                                           plan_value (b - a))) *
                       u::rad;
                if (turn > run / radius * u::rad)
                  continue;
              }
              const grade_t pitch = grade_of (rise (ha, elevation[k]), run);
              const auto steep = pitch / (0.06f * grade[one]);
              // A turn costs as a little more road, and the ground's
              // grade as the planner's carts find it.
              const meters_t step =
                run * (1.0f + steep * steep +
                       quantity_cast<mp_units::dimensionless> (place[k])) +
                quantity_cast<isq::length> (turn_cost * turn * turn);
              meters_t& there =
                cost[static_cast<std::size_t> (i + 1) * width +
                     static_cast<std::size_t> (to + half) * states +
                     (e + lean)];
              if (here + step < there) {
                there = here + step;
                came[static_cast<std::size_t> (i + 1) * width +
                     static_cast<std::size_t> (to + half) * states +
                     (e + lean)] = static_cast<std::int8_t> (d);
              }
            }
          }
      // Back from the last station's centre, by the cheapest lean.
      int d = 0;
      meters_t best = unreachable;
      for (int e = -lean; e <= lean; ++e) {
        const meters_t c =
          cost[static_cast<std::size_t> (n - 1) * width +
               static_cast<std::size_t> (half) * states + (e + lean)];
        if (c < best) {
          best = c;
          d = e;
        }
      }
      if (!(best < unreachable))
        return spine;
      std::vector<plan_position_t> result (n);
      int j = 0;
      for (int i = n - 1; i >= 0; --i) {
        result[i] = position (i, j);
        if (i == 0)
          break;
        const int previous =
          came[static_cast<std::size_t> (i) * width +
               static_cast<std::size_t> (j + half) * states + (d + lean)];
        j -= d;
        d = previous;
      }
      return result;
    }

    // The planned lines as built: eased into curves at four metres between
    // points, each end that leaves an earlier way moved onto it, and each
    // cart road's laid over the ground at full resolution.
    void finish_lines (const Lattice& lattice,
                       const FineGround& ground,
                       const std::vector<RoadPlace>& places,
                       std::vector<Road>& roads) {
      const plan_extent_t period = lattice.period ();
      constexpr meters_t spacing = 4.0f * u::m;
      for (std::size_t r = 0; r < roads.size (); ++r) {
        const bool built = roads[r].kind == Road::Kind::Road;
        std::vector<plan_position_t> line = resample (roads[r].points, spacing);
        ease (line, 5, 3, [&] (plan_position_t point) {
          const std::size_t node = node_at (lattice, point);
          return built ? lattice.water[node] < Lattice::Deep &&
                           lattice.mire[node] <= impassable_mire
                       : lattice.water[node] == Lattice::Dry;
        });
        // Each end that leaves an earlier way is moved onto it before the
        // line is laid along the contours, which keeps its ends.
        for (const bool back : { false, true }) {
          const plan_position_t end = back ? line.back () : line.front ();
          // An end at a place stays there.
          if (std::ranges::any_of (places, [&] (const RoadPlace& place) {
                return hypot (minimum_image (place.at - end, period)) <
                       15.0f * u::m;
              }))
            continue;
          plan_position_t best = end;
          // An earlier road may since have been laid up to its corridor's
          // width from the line this one was planned off.
          meters_t best_distance = 70.0f * u::m;
          for (std::size_t earlier = 0; earlier < r; ++earlier) {
            const auto [point, distance] =
              nearest_on (roads[earlier], end, period);
            if (distance < best_distance) {
              best_distance = distance;
              best = point;
            }
          }
          shift_end (line, back, best);
        }
        if (built) {
          // Laid again three times, each toward the profile the last line
          // could hold at a cart road's grade.
          const std::vector<plan_position_t> spine =
            resample (line, contour_station);
          std::vector<SurfaceElevation> target;
          std::vector<plan_position_t> laid;
          for (int round = 0; round < 3; ++round) {
            laid = follow_contours (lattice, ground, spine, target);
            const std::size_t n = laid.size ();
            std::vector<SurfaceElevation> lower (n);
            for (std::size_t i = 0; i < n; ++i)
              lower[i] = ground.at (laid[i]);
            std::vector<SurfaceElevation> upper = lower;
            for (std::size_t i = 1; i < n; ++i) {
              const meters_t climb = 0.1f * hypot (laid[i] - laid[i - 1]);
              lower[i] = std::max (lower[i], lower[i - 1] - climb);
              upper[i] = std::min (upper[i], upper[i - 1] + climb);
            }
            for (std::size_t i = n - 1; i-- > 0;) {
              const meters_t climb = 0.1f * hypot (laid[i + 1] - laid[i]);
              lower[i] = std::max (lower[i], lower[i + 1] - climb);
              upper[i] = std::min (upper[i], upper[i + 1] + climb);
            }
            target.resize (n);
            for (std::size_t i = 0; i < n; ++i)
              target[i] = midway (lower[i], upper[i]);
          }
          line = resample (laid, spacing);
          ease (line, 3, 2, [&] (plan_position_t point) {
            const std::size_t node = node_at (lattice, point);
            return ground.depth (point) <= 1.5f * u::m &&
                   lattice.mire[node] <= impassable_mire;
          });
          // Easing draws the points together round the bends; the profile
          // is eased by them in turn, which keeps its grade only where they
          // stand evenly.
          line = resample (line, spacing);
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
                          SurfaceElevation sea_level,
                          std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("plan_roads");
    const Lattice lattice = read_lattice (
      domain, elevations, flood, drainage, ground, river_area, sea_level);
    Planner planner {
      lattice, std::vector<std::uint8_t> (lattice.size (), None), {}, {}
    };
    const Mover cart { lattice, Mover::Kind::Cart, 0.3f * proportion[one] };
    const Mover walker { lattice, Mover::Kind::Walker, 0.5f * proportion[one] };

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
        if (!lattice.dry (node) ||
            lattice.steepness[node] > 0.1f * terrain_slope[one] ||
            !(found.cost[node] < unreachable))
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
      // Inland country can carry a bus even without a gentle landing.
      // Try the same winding route from the trailhead in that case.
      const bool has_landing = best != lattice.size ();
      const auto terminus = has_landing ? best : trailhead;
      std::vector<std::size_t> round;
      meters_t cheapest = unreachable;
      for (const int axis : { 0, 1 }) {
        auto [path, cost] =
          search_round (lattice, planner.ways, terminus, cart, axis);
        if (!path.empty () && cost < cheapest) {
          cheapest = cost;
          round = std::move (path);
        }
      }
      if (!round.empty ()) {
        planner.lay_round (round);
        if (has_landing)
          planner.connect (trailhead,
                           search (lattice, planner.ways, round, cart));
      }
      if (has_landing && (!round.empty () || planner.connect (best, found)))
        planner.network.places.push_back (
          planner.place (RoadPlace::Kind::Landing, best));
    }

    for (const std::size_t outlet : lake_outlets (lattice, river_area)) {
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
      meters_t best = unreachable;
      for (std::size_t node = 0; node < lattice.size (); ++node) {
        const meters_t cost = by_road.cost[node] + 3.0f * on_foot.cost[node];
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
      std::size_t node = node_at (lattice, place.at);
      if (place.kind == RoadPlace::Kind::Sanctuary) {
        // A sanctuary's road ends at its foot: the nearest built node.
        meters_t nearest = unreachable;
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
        scale_factor_t ratio = 2.0f * scale_factor[one];
        std::vector<std::size_t> path;
      };
      std::vector<Shortcut> best (ends.size ());
      parallel_for (ends.size (), 1, [&] (std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
          // Along the roads alone, and anywhere a road could be built.
          std::vector<std::uint8_t> none (lattice.size (), None);
          const Search fresh = search (lattice, none, { ends[i] }, cart);
          Search along { std::vector<meters_t> (lattice.size (), unreachable),
                         std::vector<std::uint32_t> (lattice.size ()) };
          {
            Open open;
            along.cost[ends[i]] = 0.0f * u::m;
            open.push ({ 0.0f * u::m, static_cast<std::uint32_t> (ends[i]) });
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
                const meters_t reached = cost + lattice.distance (node, next);
                if (reached < along.cost[next]) {
                  along.cost[next] = reached;
                  open.push ({ reached, static_cast<std::uint32_t> (next) });
                }
              }
            }
          }
          for (std::size_t j = 0; j < ends.size (); ++j) {
            if (j == i || ends[j] == ends[i] ||
                !(fresh.cost[ends[j]] < unreachable) ||
                torus_distance (lattice,
                                planner.network.places[i],
                                planner.network.places[j]) > 5000.0f * u::m)
              continue;
            std::vector<std::size_t> path = trace (fresh, ends[j]);
            const meters_t direct = path_length (lattice, path);
            const meters_t travelled = along.cost[ends[j]];
            const scale_factor_t ratio = quantity_cast<scale_factor> (
              travelled / std::max (direct, 1.0f * u::m));
            if (direct < 4500.0f * u::m && ratio > best[i].ratio)
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
        if (torus_distance (lattice, one, two) < 6000.0f * u::m &&
            sees (lattice,
                  node_at (lattice, one.at),
                  4.0f * u::m,
                  node_at (lattice, two.at),
                  4.0f * u::m))
          planner.network.sightlines.push_back (
            { sanctuaries[a], sanctuaries[b] });
      }

    // Each place's history, and each way's from the journeys along it:
    // every pair of places, by the network's shortest way between them
    // (footpaths counted twice as long, being slower).
    for (RoadPlace& place : planner.network.places)
      roll_history (place, node_at (lattice, place.at), seed);
    // A journey goes along a way when it passes through a node only that
    // way holds. The search may step across from node to node of the
    // network by any of its steps, so no single edge of a way need lie on
    // a journey along it, but the nodes it holds alone do.
    constexpr std::uint32_t shared = std::numeric_limits<std::uint32_t>::max ();
    constexpr std::uint32_t unheld = shared - 1;
    std::vector<std::uint32_t> holder (lattice.size (), unheld);
    for (std::size_t road = 0; road < planner.road_nodes.size (); ++road)
      for (const std::size_t node : planner.road_nodes[road]) {
        std::uint32_t& held = holder[node];
        held = held == unheld || held == road
                 ? static_cast<std::uint32_t> (road)
                 : shared;
      }
    std::vector<bool> held_alone (planner.road_nodes.size (), false);
    for (const std::uint32_t held : holder)
      if (held < held_alone.size ())
        held_alone[held] = true;
    std::vector<RoadPlace>& places = planner.network.places;
    std::vector<Road>& roads = planner.network.roads;
    constexpr age_t never = infinite * years;
    std::vector<bool> lived_on (roads.size (), false);
    std::vector<age_t> last_used (roads.size (), never);
    for (std::size_t i = 0; i < places.size (); ++i) {
      const std::size_t from = node_at (lattice, places[i].at);
      Search along { std::vector<meters_t> (lattice.size (), unreachable),
                     std::vector<std::uint32_t> (lattice.size ()) };
      Open open;
      along.cost[from] = 0.0f * u::m;
      along.from[from] = static_cast<std::uint32_t> (from);
      open.push ({ 0.0f * u::m, static_cast<std::uint32_t> (from) });
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
          // A footpath is slower: its length counts twice.
          const meters_t reached =
            cost + (planner.ways[next] == Path ? 2.0f : 1.0f) *
                     lattice.distance (node, next);
          if (reached < along.cost[next]) {
            along.cost[next] = reached;
            along.from[next] = node;
            open.push ({ reached, static_cast<std::uint32_t> (next) });
          }
        }
      }
      for (std::size_t j = i + 1; j < places.size (); ++j) {
        const std::size_t to = node_at (lattice, places[j].at);
        if (!(along.cost[to] < unreachable))
          continue;
        const age_t since = std::min (places[i].founded, places[j].founded);
        const bool alive = places[i].abandoned == 0.0f * years &&
                           places[j].abandoned == 0.0f * years;
        const age_t ended = std::max (places[i].abandoned, places[j].abandoned);
        const std::vector<std::size_t> path = trace (along, to);
        std::vector<bool> taken (roads.size (), false);
        for (const std::size_t node : path)
          if (holder[node] < roads.size ())
            taken[holder[node]] = true;
        // A short way between two others may hold no node alone; a
        // journey through all its nodes goes along it.
        for (std::size_t r = 0; r < roads.size (); ++r)
          if (!held_alone[r])
            taken[r] = std::ranges::all_of (
              planner.road_nodes[r], [&] (std::size_t node) {
                return std::ranges::find (path, node) != path.end ();
              });
        for (std::size_t r = 0; r < roads.size (); ++r) {
          if (!taken[r])
            continue;
          Road& road = roads[r];
          ++road.traffic;
          road.age = std::max (road.age, since);
          lived_on[r] = lived_on[r] || alive;
          last_used[r] = std::min (last_used[r], ended);
        }
      }
    }
    for (std::size_t road = 0; road < roads.size (); ++road)
      roads[road].abandoned = lived_on[road] || !(last_used[road] < never)
                                ? 0.0f * years
                                : last_used[road];
    // The bus timetable keeps its road in service from the terminus.
    auto landing =
      std::ranges::find (places, RoadPlace::Kind::Landing, &RoadPlace::kind);
    const auto& terminus =
      landing == places.end () ? places.front () : *landing;
    for (Road& road : roads)
      if (road.loop) {
        ++road.traffic;
        road.age = std::max (road.age, terminus.founded);
        road.abandoned = 0.0f * years;
      }
    const FineGround fine { elevations,
                            flood,
                            static_cast<int> (domain.width ()),
                            static_cast<int> (domain.height ()),
                            domain.spacing_x (),
                            domain.spacing_z () };
    finish_lines (lattice, fine, planner.network.places, planner.network.roads);
    return std::move (planner.network);
  }
}
