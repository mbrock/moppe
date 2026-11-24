#include <moppe/correct_math.hh>
#include <moppe/terrain/channel.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  ChannelFormationResult
  detail::form_channels (const TerrainDomain& domain,
                         std::span<const SurfaceElevation> elevations,
                         const FloodField& flood,
                         const LakeCensus& census,
                         const DrainageGraph& drainage,
                         const RiverNetwork& rivers,
                         const ChannelFormation& parameters) {
    MOPPE_PROFILE_ZONE ("form_channels");
    const TerrainDomain& grid = flood.domain ();
    if (domain != grid)
      throw std::invalid_argument ("channel terrain does not match hydrology");
    const int width = static_cast<int> (grid.width ());
    const int height = static_cast<int> (grid.height ());
    const std::size_t count = grid.width () * grid.height ();
    if (census.cell_count () != count || drainage.receiver.size () != count)
      throw std::invalid_argument ("channel inputs do not share a grid");
    const meters_t spacing_x = grid.spacing_x ();
    const meters_t spacing_z = grid.spacing_z ();
    const meters_t nowhere = std::numeric_limits<float>::infinity () * u::m;
    const SurfaceElevation nowhere_level = surface_elevation_point (nowhere);
    const std::uint32_t none = terrain::no_cell;
    const proportion_t whole = 1.0f * proportion[one];

    std::vector<SurfaceElevation> ground (elevations.begin (),
                                          elevations.end ());

    // The sea and every lake are left as they are. A body that stands as
    // a sheet is a lake unless a river leaves it that is wide against it:
    // that is a pool on the river's way.
    std::vector<std::uint8_t> pool (census.domain ().size (), 0);
    for (const RiverReach& reach : rivers.reaches) {
      if (!census.domain ().contains (reach.upstream_body))
        continue;
      const WaterBody& body = census.water_body (reach.upstream_body);
      const meters_t river = river_width (reach.upstream_area);
      if (body.area < square_meters_t (parameters.lake_widths * river * river))
        pool[census.domain ().offset (reach.upstream_body)] = 1;
    }
    const auto lake = [&] (WaterBodyId body) {
      return census.domain ().contains (body) &&
             !pool[census.domain ().offset (body)] &&
             water_body_is_permanent (census.water_body (body),
                                      parameters.permanence);
    };
    std::vector<std::uint8_t> kept (count, 0);
    for (std::size_t cell = 0; cell < count; ++cell)
      kept[cell] =
        flood.ocean[cell] ||
        lake (census.body_at (CellIndex { static_cast<std::uint32_t> (cell) }));
    const auto wet = [&] (std::uint32_t cell) {
      return census.body_at (CellIndex { cell }) != LakeCensus::dry ||
             flood.ocean[cell];
    };
    const auto step = [&] (std::uint32_t from, std::uint32_t to) {
      int dx = static_cast<int> (from % width) - static_cast<int> (to % width);
      int dz = static_cast<int> (from / width) - static_cast<int> (to / width);
      if (dx > width / 2)
        dx -= width;
      if (dx < -width / 2)
        dx += width;
      if (dz > height / 2)
        dz -= height;
      if (dz < -height / 2)
        dz += height;
      return hypot (plan_displacement (static_cast<float> (dx) * spacing_x,
                                       static_cast<float> (dz) * spacing_z));
    };

    // The cells of every watercourse: each reach's own, and those it
    // crosses through hollows and pools to meet the next. A lake's sill, a
    // fall's lip and foot, and standing water are fixed points of the profile.
    std::vector<std::uint8_t> course (count, 0);
    std::vector<std::uint8_t> fixed = kept;
    std::vector<meters_t> below_sill (count, nowhere);
    std::vector<std::uint8_t> fed (rivers.reaches.size (), 0);
    for (const RiverReach& reach : rivers.reaches) {
      if (reach.downstream_reach != RiverReach::no_id)
        fed[reach.downstream_reach] = 1;
      if (reach.cells.empty ())
        continue;
      if (lake (reach.upstream_body)) {
        fixed[reach.cells.front ()] = 1;
        below_sill[reach.cells.front ()] = 0.0f * u::m;
      }
      for (const CellIndex cell : reach.cells)
        course[cell] = 1;
      std::uint32_t last = reach.cells.back ();
      std::uint32_t next = drainage.receiver[last];
      if (next != last)
        course[last = next] = 1;
      while (wet (last) && !kept[last]) {
        next = drainage.receiver[last];
        if (next == last || course[next])
          break;
        course[last = next] = 1;
      }
    }
    for (const Waterfall& waterfall : rivers.waterfalls)
      fixed[waterfall.lip_cell] = fixed[waterfall.foot_cell] = 1;

    // Sources first: water gathers downstream, so a cell's catchment is
    // greater than any that drains to it.
    std::vector<std::uint32_t> downstream;
    for (std::uint32_t cell = 0; cell < count; ++cell)
      if (course[cell])
        downstream.push_back (cell);
    const auto catchment = [&] (std::uint32_t cell) {
      return drainage.contributing_area_at (cell);
    };
    const auto bed_width_for = [&] (square_meters_t area) {
      return std::max (parameters.least_width,
                       meters_t (parameters.width_share * river_width (area)));
    };
    const auto bed_depth_for = [&] (square_meters_t area) {
      const meters_t w = bed_width_for (area);
      return std::min (
        parameters.greatest_depth,
        parameters.least_depth *
          cr::pow ((w / parameters.least_width).numerical_value_in (one),
                   parameters.depth_growth.numerical_value_in (one)));
    };
    std::ranges::sort (downstream, [&] (std::uint32_t a, std::uint32_t b) {
      const square_meters_t area_a = catchment (a);
      const square_meters_t area_b = catchment (b);
      return area_a != area_b ? area_a < area_b : a < b;
    });
    const auto receives = [&] (std::uint32_t cell) {
      const std::uint32_t next = drainage.receiver[cell];
      return next != cell && course[next] ? next : none;
    };

    // Cut through every sill, a bed is the lowest ground above it on the
    // stream; filled to every sill, the highest below it. The graded bed
    // lies between.
    std::vector<SurfaceElevation> graded = ground;
    {
      MOPPE_PROFILE_ZONE ("channels.grade");
      std::vector<SurfaceElevation> cut = ground;
      std::vector<SurfaceElevation> filled = ground;
      for (const std::uint32_t cell : downstream) {
        const std::uint32_t next = receives (cell);
        if (next == none)
          continue;
        if (!fixed[next])
          cut[next] = std::min (cut[next], cut[cell]);
        below_sill[next] =
          std::min (below_sill[next], below_sill[cell] + step (cell, next));
      }
      for (auto cell = downstream.rbegin (); cell != downstream.rend ();
           ++cell) {
        const std::uint32_t next = receives (*cell);
        if (next != none && !fixed[*cell])
          filled[*cell] =
            std::max (ground[*cell],
                      filled[next] +
                        meters_t (parameters.least_grade * step (*cell, next)));
      }
      // Where the course enters standing water, the profile meets that
      // water's surface, not its floor.
      for (const std::uint32_t cell : downstream)
        graded[cell] =
          kept[cell]
            ? flood.water_levels ()[cell]
            : filled[cell] - ElevationChange (parameters.breach *
                                              (filled[cell] - cut[cell]));

      // Ease it along the stream, each cell toward the mean of what lies
      // above (weighted by catchment so a steep hillside brook cannot lift a
      // lowland trunk river at their confluence) and below it, and let it
      // fall all the way again.
      using catchment_weight_t = quantity<u::m * u::m, double>;
      std::vector<cubic_meters_f64_t> above (count, 0.0 * u::m * u::m * u::m);
      std::vector<catchment_weight_t> above_weight (count, 0.0 * u::m * u::m);
      std::vector<SurfaceElevation> eased (count);
      for (int pass = 0; pass < count_value (parameters.easing); ++pass) {
        for (const std::uint32_t cell : downstream) {
          above[cell] = 0.0 * u::m * u::m * u::m;
          above_weight[cell] = 0.0 * u::m * u::m;
        }
        for (const std::uint32_t cell : downstream)
          if (const std::uint32_t next = receives (cell); next != none) {
            const catchment_weight_t w = std::max (
              1.0 * u::m * u::m, value_cast<double> (catchment (cell)));
            above[next] += cubic_meters_f64_t (
              w * value_cast<double> (graded[cell].delta_from_zero ()));
            above_weight[next] += w;
          }
        for (const std::uint32_t cell : downstream) {
          const std::uint32_t next = receives (cell);
          const ElevationChange here = graded[cell].delta_from_zero ();
          const ElevationChange upstream_mean =
            above_weight[cell] > 0.0 * u::m * u::m
              ? ElevationChange (
                  value_cast<float> (above[cell] / above_weight[cell]))
              : here;
          eased[cell] =
            fixed[cell]
              ? graded[cell]
              : SurfaceElevation (0.5f * here + 0.25f * upstream_mean +
                                  0.25f * (next != none
                                             ? graded[next].delta_from_zero ()
                                             : here));
        }
        for (const std::uint32_t cell : downstream)
          graded[cell] = eased[cell];
      }
      // A sill is cut only so deep; a hollow behind a higher one fills
      // the rest of the way. Larger rivers cut proportionally deeper sills
      // so the navigable trunk stays free of shallow stepped dams.
      for (const std::uint32_t cell : downstream) {
        const meters_t allowed_cut = std::max (
          parameters.deepest_cut, 1.35f * bed_depth_for (catchment (cell)));
        graded[cell] = std::max (graded[cell], ground[cell] - allowed_cut);
      }
      for (auto cell = downstream.rbegin (); cell != downstream.rend ();
           ++cell) {
        const std::uint32_t next = receives (*cell);
        if (next != none && !fixed[*cell])
          graded[*cell] =
            std::max (graded[*cell],
                      graded[next] +
                        meters_t (parameters.least_grade * step (*cell, next)));
      }
    }

    // The course cell nearest a place on an alignment, which runs within a
    // cell of the chain it was drawn from.
    const auto course_near = [&] (plan_position_t at) {
      const meters_t x_m = x_of (at);
      const meters_t z_m = z_of (at);
      const int near_x = static_cast<int> (
        std::lround ((x_m / spacing_x).numerical_value_in (one)));
      const int near_z = static_cast<int> (
        std::lround ((z_m / spacing_z).numerical_value_in (one)));
      std::uint32_t found = none;
      square_meters_t nearest = nowhere * nowhere;
      for (int dz = -2; dz <= 2; ++dz)
        for (int dx = -2; dx <= 2; ++dx) {
          const std::uint32_t cell = static_cast<std::uint32_t> (
            wrap_index (near_z + dz, height) * width +
            wrap_index (near_x + dx, width));
          if (!course[cell])
            continue;
          const meters_t from_x =
            static_cast<float> (near_x + dx) * spacing_x - x_m;
          const meters_t from_z =
            static_cast<float> (near_z + dz) * spacing_z - z_m;
          const square_meters_t distance = from_x * from_x + from_z * from_z;
          if (distance < nearest) {
            nearest = distance;
            found = cell;
          }
        }
      return found;
    };

    // The bed along each alignment, and for every cell beside one the
    // section it lies in: the nearest.
    struct Foot {
      meters_t distance = std::numeric_limits<float>::infinity () * u::m;
      SurfaceElevation bed = surface_elevation_point (0.0f * u::m);
      // The bed's reach on this cell's side of the stream, and that side's
      // bank: how far a bar shoals it (0 to 1), how far a cut steepens it,
      // the bank's slope, and how far out the river works it.
      meters_t half_width = 0.0f * u::m;
      meters_t depth = 0.0f * u::m;
      proportion_t bar = 0.0f * proportion[one];
      proportion_t cut = 0.0f * proportion[one];
      slope_t slope = 0.0f * terrain_slope[one];
      meters_t reach = 0.0f * u::m;
    };
    const auto ground_at = [&] (plan_position_t world) {
      // Lattice coordinates: how many cells along each axis.
      const float gx =
        wrap_coordinate ((x_of (world) / spacing_x).numerical_value_in (one),
                         static_cast<float> (width));
      const float gz =
        wrap_coordinate ((z_of (world) / spacing_z).numerical_value_in (one),
                         static_cast<float> (height));
      const int x0 = static_cast<int> (std::floor (gx));
      const int z0 = static_cast<int> (std::floor (gz));
      const int x1 = wrap_index (x0 + 1, width);
      const int z1 = wrap_index (z0 + 1, height);
      const proportion_t tx = (gx - x0) * proportion[one];
      const proportion_t tz = (gz - z0) * proportion[one];
      const auto at = [&] (int x, int z) {
        return ground[static_cast<std::size_t> (z) * width + x];
      };
      return moppe::lerp (moppe::lerp (at (x0, z0), at (x1, z0), tx),
                          moppe::lerp (at (x0, z1), at (x1, z1), tx),
                          tz);
    };
    std::vector<Foot> feet (count);
    const auto bank_reach_for = [] (meters_t depth) {
      return std::max (6.0f * u::m, 3.0f * depth);
    };
    const meters_t bar_reach = parameters.bar_reach;
    const scale_factor_t bar_widths = parameters.bar_widths;
    // How far a full bar runs out from a bed of this half width.
    const auto strand_reach = [&] (meters_t half_width) {
      return std::max (bar_reach, meters_t (bar_widths * 2.0f * half_width));
    };
    const meters_t bar_lowering = parameters.bar_lowering;
    // How far beyond a strand the ground is still eased toward it.
    constexpr meters_t bar_riser = 8.0f * u::m;
    constexpr meters_t section_step = 1.0f * u::m;
    const meters_t cell_reach = std::max (spacing_x, spacing_z);
    // How sharply a river turns: the angle it turns through per metre.
    using curvature_t = quantity<si::radian / si::metre, float>;
    {
      MOPPE_PROFILE_ZONE ("channels.sections");
      struct Bed {
        plan_position_t at;
        SurfaceElevation level;
        // The widest the bed may be, and how far it reaches to the left
        // and right before the valley's own sides stand over it.
        meters_t half_width;
        meters_t left;
        meters_t right;
        meters_t depth;
        // A fixed point of the profile, which easing leaves be.
        bool fixed;
        // Each side's bar and cut (Foot), left and right.
        proportion_t bar[2];
        proportion_t cut[2];
      };
      std::vector<Bed> beds;
      std::vector<meters_t> along;
      std::vector<curvature_t> turning;
      for (const RiverReach& reach : rivers.reaches) {
        const auto& points = reach.alignment.points;
        const bool springs =
          !fed[reach.id] && (points.empty () || points.front ().standing_water <
                                                  0.99f * proportion[one]);
        beds.clear ();
        for (const RiverAlignmentPoint& point : points) {
          const plan_position_t at = point.place;
          const std::uint32_t cell = course_near (at);
          if (cell == none)
            continue;
          const meters_t bed_width = bed_width_for (point.contributing_area);
          meters_t depth = bed_depth_for (point.contributing_area);
          // Below a lake the bed starts at the sill; a spring's begins as
          // nothing.
          if (below_sill[cell] != nowhere)
            depth = meters_t (
              depth * band (0.0f * u::m, 2.5f * bed_width, below_sill[cell]));
          if (springs)
            depth = meters_t (
              depth * band (0.0f * u::m, 3.0f * bed_width, point.distance));
          beds.push_back (
            { .at = at,
              .level = graded[cell],
              .half_width = 0.5f * bed_width,
              .left = 0.0f * u::m,
              .right = 0.0f * u::m,
              .depth = depth,
              .fixed = fixed[cell] != 0,
              .bar = { 0.0f * proportion[one], 0.0f * proportion[one] },
              .cut = { 0.0f * proportion[one], 0.0f * proportion[one] } });
        }
        // The chain's corners out of the alignment's levels, which still
        // only fall.
        for (int pass = 0; pass < 2; ++pass)
          for (std::size_t bed = 1; bed + 1 < beds.size (); ++bed)
            if (!beds[bed].fixed)
              beds[bed].level = SurfaceElevation (
                0.25f * beds[bed - 1].level.delta_from_zero () +
                0.5f * beds[bed].level.delta_from_zero () +
                0.25f * beds[bed + 1].level.delta_from_zero ());
        for (std::size_t bed = 1; bed < beds.size (); ++bed)
          beds[bed].level = std::min (beds[bed].level, beds[bed - 1].level);
        // How the river runs here: how steeply its bed falls, and how
        // sharply it bends, over a window of some lengths of its width.
        // A river falling gently over a mobile bed meanders and builds bars
        // of what it carries on the inside of its bends while it cuts into
        // the outside; a steep one runs straight down steps of boulders
        // between whatever banks its valley gives it.
        along.assign (beds.size (), 0.0f * u::m);
        for (std::size_t bed = 1; bed < beds.size (); ++bed)
          along[bed] = along[bed - 1] + hypot (beds[bed].at - beds[bed - 1].at);
        turning.assign (beds.size (), curvature_t::zero ());
        std::vector<grade_t> grades (beds.size (), 0.0f * grade[one]);
        {
          std::size_t first = 0;
          std::size_t last = 0;
          for (std::size_t bed = 0; bed < beds.size (); ++bed) {
            const meters_t window =
              std::max (8.0f * u::m, 3.0f * beds[bed].half_width);
            while (along[bed] - along[first] > window)
              ++first;
            while (last + 1 < beds.size () &&
                   along[last + 1] - along[bed] <= window)
              ++last;
            const meters_t run = along[last] - along[first];
            if (run < 1.0f * u::m)
              continue;
            grades[bed] =
              grade_t ((beds[first].level - beds[last].level) / run);
            const plan_displacement_t into = beds[bed].at - beds[first].at;
            const plan_displacement_t out_of = beds[last].at - beds[bed].at;
            const meters_t in_x = x_of (into);
            const meters_t in_z = z_of (into);
            const meters_t out_x = x_of (out_of);
            const meters_t out_z = z_of (out_of);
            if (hypot (into) < 0.5f * u::m || hypot (out_of) < 0.5f * u::m)
              continue;
            // The angle turned, toward the right positive, over the
            // distance it turns in: the curvature.
            const square_meters_t cross = in_x * out_z - in_z * out_x;
            const square_meters_t dot = in_x * out_x + in_z * out_z;
            const radians_t turned =
              cr::atan2 (cross.numerical_value_in (u::m * u::m),
                         dot.numerical_value_in (u::m * u::m)) *
              u::rad;
            turning[bed] = turned / (0.5f * run);
          }
          for (int pass = 0; pass < 4; ++pass)
            for (std::size_t bed = 1; bed + 1 < beds.size (); ++bed)
              turning[bed] = 0.25f * turning[bed - 1] + 0.5f * turning[bed] +
                             0.25f * turning[bed + 1];
        }
        for (std::size_t bed = 0; bed < beds.size (); ++bed) {
          Bed& here = beds[bed];
          const proportion_t calm =
            whole -
            band (parameters.calm_grade, parameters.steep_grade, grades[bed]);
          // A bend is tight when its radius is a few widths of its river.
          const meters_t width = 2.0f * here.half_width;
          const radians_t turned =
            std::max (abs (turning[bed]) * width, 1e-4f * u::rad);
          const scale_factor_t radius_widths =
            (1.0f / turned.numerical_value_in (u::rad)) * scale_factor[one];
          const proportion_t bend =
            whole -
            band (parameters.tight_bend, parameters.open_bend, radius_widths);
          // Right is the inside of a bend turning right.
          const int inside = turning[bed] > curvature_t::zero () ? 1 : 0;
          here.bar[inside] = proportion_t (
            calm * moppe::lerp (parameters.straight_bar, whole, bend));
          here.bar[1 - inside] =
            proportion_t (calm * parameters.straight_bar * (whole - bend));
          here.cut[inside] = 0.0f * proportion[one];
          here.cut[1 - inside] = proportion_t (calm * bend);
        }
        // A river is as wide as its water makes it only where the valley
        // gives it room: its bed ends where the ground already stands a
        // bank's height over it. A bar widens it on its own side.
        for (std::size_t bed = 0; bed < beds.size (); ++bed) {
          Bed& here = beds[bed];
          const Bed& before = beds[bed ? bed - 1 : bed];
          const Bed& after = beds[bed + 1 < beds.size () ? bed + 1 : bed];
          const plan_displacement_t way = after.at - before.at;
          const meters_t run = length (way);
          if (run < 1e-3f * u::m)
            continue;
          const quantity<one, float> across_x = -z_of (way) / run;
          const quantity<one, float> across_z = x_of (way) / run;
          const SurfaceElevation bank =
            here.level + parameters.bank_height (here.depth);
          for (int side = 0; side < 2; ++side) {
            const float sign = side ? 1.0f : -1.0f;
            const meters_t widest = meters_t (
              here.half_width *
              (1.0f * scale_factor[one] +
               scale_factor_t (parameters.bar_widening * here.bar[side])));
            meters_t room = section_step;
            while (room < widest &&
                   ground_at (plan_position (
                     x_of (here.at) + sign * across_x * room,
                     z_of (here.at) + sign * across_z * room)) < bank)
              room += section_step;
            (side ? here.right : here.left) = std::min (room, widest);
          }
        }
        // A bed and its banks do not change from one step to the next.
        for (int pass = 0; pass < 3; ++pass)
          for (std::size_t bed = 1; bed + 1 < beds.size (); ++bed) {
            const auto ease = [&] (auto member) {
              beds[bed].*member = 0.25f * beds[bed - 1].*member +
                                  0.5f * beds[bed].*member +
                                  0.25f * beds[bed + 1].*member;
            };
            ease (&Bed::left);
            ease (&Bed::right);
            for (int side = 0; side < 2; ++side) {
              beds[bed].bar[side] = 0.25f * beds[bed - 1].bar[side] +
                                    0.5f * beds[bed].bar[side] +
                                    0.25f * beds[bed + 1].bar[side];
              beds[bed].cut[side] = 0.25f * beds[bed - 1].cut[side] +
                                    0.5f * beds[bed].cut[side] +
                                    0.25f * beds[bed + 1].cut[side];
            }
          }

        for (std::size_t bed = 0; bed + 1 < beds.size (); ++bed) {
          const Bed& start = beds[bed];
          const Bed& end = beds[bed + 1];
          const plan_displacement_t way = end.at - start.at;
          const meters_t run_x = x_of (way);
          const meters_t run_z = z_of (way);
          const meters_t run = length (way);
          if (run < 1e-3f * u::m)
            continue;
          const meters_t start_x = x_of (start.at);
          const meters_t start_z = z_of (start.at);
          const meters_t end_x = x_of (end.at);
          const meters_t end_z = z_of (end.at);
          const meters_t widest =
            std::max ({ start.left, start.right, end.left, end.right });
          const meters_t local_bank_reach =
            bank_reach_for (std::max (start.depth, end.depth));
          const meters_t radius =
            widest + local_bank_reach + strand_reach (widest) + bar_riser;
          // The lattice's reach around the step, in cells.
          const int minimum_x = static_cast<int> (
            std::floor (((std::min (start_x, end_x) - radius) / spacing_x)
                          .numerical_value_in (one)));
          const int maximum_x = static_cast<int> (
            std::ceil (((std::max (start_x, end_x) + radius) / spacing_x)
                         .numerical_value_in (one)));
          const int minimum_z = static_cast<int> (
            std::floor (((std::min (start_z, end_z) - radius) / spacing_z)
                          .numerical_value_in (one)));
          const int maximum_z = static_cast<int> (
            std::ceil (((std::max (start_z, end_z) + radius) / spacing_z)
                         .numerical_value_in (one)));
          for (int at_z = minimum_z; at_z <= maximum_z; ++at_z)
            for (int at_x = minimum_x; at_x <= maximum_x; ++at_x) {
              const meters_t world_x = static_cast<float> (at_x) * spacing_x;
              const meters_t world_z = static_cast<float> (at_z) * spacing_z;
              const meters_t along =
                ((world_x - start_x) * run_x + (world_z - start_z) * run_z) /
                run;
              if (along < -cell_reach || along > run + cell_reach)
                continue;
              const proportion_t t = std::clamp (
                proportion_t (along / run), 0.0f * proportion[one], whole);
              const meters_t from_x = world_x - moppe::lerp (start_x, end_x, t);
              const meters_t from_z = world_z - moppe::lerp (start_z, end_z, t);
              const meters_t distance =
                length (plan_displacement (from_x, from_z));
              const meters_t side =
                from_z * (run_x / run) - from_x * (run_z / run);
              const int on = side < 0.0f * u::m ? 0 : 1;
              const meters_t half_width =
                on ? moppe::lerp (start.right, end.right, t)
                   : moppe::lerp (start.left, end.left, t);
              const proportion_t bar =
                moppe::lerp (start.bar[on], end.bar[on], t);
              const proportion_t cut =
                moppe::lerp (start.cut[on], end.cut[on], t);
              const meters_t depth_here =
                moppe::lerp (start.depth, end.depth, t);
              const meters_t reach =
                bank_reach_for (depth_here) +
                meters_t (bar * (strand_reach (half_width) + bar_riser));
              Foot& foot =
                feet[static_cast<std::size_t> (wrap_index (at_z, height)) *
                       width +
                     wrap_index (at_x, width)];
              if (distance >= half_width + reach)
                continue;
              // Inside overlapping channel beds (confluences), the deeper
              // trunk owns the bed so a shallow tributary never leaves a
              // high sill across a navigable river; outside beds, the nearest
              // section wins.
              const bool in_candidate = distance <= half_width;
              const bool in_current =
                foot.distance != nowhere && foot.distance <= foot.half_width;
              if (foot.distance != nowhere) {
                if (in_candidate && in_current) {
                  if (depth_here < foot.depth - 1e-3f * u::m ||
                      (abs (depth_here - foot.depth) <= 1e-3f * u::m &&
                       distance >= foot.distance))
                    continue;
                } else if (!in_candidate && in_current) {
                  continue;
                } else if (!in_candidate && !in_current &&
                           distance >= foot.distance) {
                  continue;
                }
              }
              foot = { .distance = distance,
                       .bed = moppe::lerp (start.level, end.level, t),
                       .half_width = half_width,
                       .depth = depth_here,
                       .bar = bar,
                       .cut = cut,
                       .slope = moppe::lerp (
                         parameters.bank_slope, parameters.cut_bank_slope, cut),
                       .reach = reach };
            }
        }
      }
    }

    const meters_t deepest_fill = parameters.deepest_fill;
    std::vector<SurfaceElevation> formed = ground;
    // What a bar planes off beside the bed, beyond the bank, and the least
    // it may leave: kept apart until it is smoothed, since the nearest
    // section changes from cell to cell where a bend's sections fan out.
    std::vector<ElevationChange> planed (count, ElevationChange::zero ());
    std::vector<SurfaceElevation> floor (count, nowhere_level);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const Foot& foot = feet[cell];
      if (foot.distance == nowhere || kept[cell])
        continue;
      // Ground far above the section is the valley's wall, and far below
      // it another place, a drop or a gully beside the stream: neither is
      // the river's to move.
      const meters_t bank = parameters.bank_height (foot.depth);
      const meters_t local_bank_reach = bank_reach_for (foot.depth);
      const meters_t local_fill = std::max (deepest_fill, 1.2f * foot.depth);
      if (foot.distance <= foot.half_width) {
        // Under a bar the bed shoals from the deep water to the strand;
        // under a cut bank it stays deep up to the bank's foot and scours
        // an outer-bend helicoidal pool.
        const proportion_t across = foot.distance / foot.half_width;
        const proportion_t shoaling =
          proportion_t ((whole - across) * (whole - across));
        const proportion_t plunging =
          whole - proportion_t (across * across * across * across);
        const proportion_t section = moppe::lerp (
          moppe::lerp (
            whole - proportion_t (across * across), shoaling, foot.bar),
          plunging,
          foot.cut);
        const meters_t scoured_depth =
          meters_t (foot.depth *
                    (1.0f * scale_factor[one] +
                     scale_factor_t (parameters.outer_bend_scour * foot.cut)));
        const SurfaceElevation bed =
          foot.bed - meters_t (scoured_depth * section);
        if (ground[cell] >= bed - local_fill &&
            ground[cell] <= foot.bed + 2.0f * bank)
          formed[cell] = bed;
        continue;
      }
      // Beside the bed the banks are cut back to a slope, and a hollow
      // there is filled level with the bed's edge, or it would stand as a
      // pocket of water the river had passed by.
      const meters_t beyond = foot.distance - foot.half_width;
      if (ground[cell] < foot.bed) {
        if (beyond < local_bank_reach && ground[cell] >= foot.bed - local_fill)
          formed[cell] = foot.bed;
        continue;
      }
      const SurfaceElevation ramp = foot.bed + meters_t (foot.slope * beyond);
      if (beyond < local_bank_reach && ground[cell] <= ramp + bank)
        formed[cell] = std::min (ground[cell], ramp);
      // A calm river swinging across its floor has planed it down beside
      // itself: a strand as far as its bar reaches, rising beyond like any
      // bank to the floor it has not yet worked, and lowered no more than
      // its bar has room to have done.
      if (foot.bar > 0.0f * proportion[one]) {
        const meters_t strand_run =
          meters_t (foot.bar * strand_reach (foot.half_width));
        const slope_t strand_slope =
          moppe::lerp (parameters.bank_slope, parameters.bar_slope, foot.bar);
        const SurfaceElevation strand =
          foot.bed + meters_t (strand_slope * std::min (beyond, strand_run)) +
          meters_t (parameters.bank_slope *
                    std::max (beyond - strand_run, 0.0f * u::m));
        const meters_t lowering = meters_t (
          bar_lowering * foot.bar *
          (whole - band (strand_run, strand_run + bar_riser, beyond)));
        planed[cell] = std::min (
          ElevationChange::zero (),
          ElevationChange (std::max (strand, ground[cell] - lowering) -
                           formed[cell]));
        floor[cell] = std::min (formed[cell], strand);
      }
    }
    {
      MOPPE_PROFILE_ZONE ("channels.planing");
      std::vector<ElevationChange> eased (count);
      for (int pass = 0; pass < 3; ++pass) {
        for (int z = 0; z < height; ++z)
          for (int x = 0; x < width; ++x) {
            const std::size_t cell = static_cast<std::size_t> (z) * width + x;
            eased[cell] = planed[cell];
            if (floor[cell] == nowhere_level)
              continue;
            // Among the planed cells only: the bed beside them is not
            // planed, and would hold the bank's foot up.
            ElevationChange sum = ElevationChange::zero ();
            int among = 0;
            for (int dz = -1; dz <= 1; ++dz)
              for (int dx = -1; dx <= 1; ++dx) {
                const std::size_t near =
                  static_cast<std::size_t> (wrap_index (z + dz, height)) *
                    width +
                  wrap_index (x + dx, width);
                if (floor[near] == nowhere_level)
                  continue;
                sum += planed[near];
                ++among;
              }
            eased[cell] = sum / static_cast<float> (among);
          }
        planed.swap (eased);
      }
      for (std::size_t cell = 0; cell < count; ++cell)
        if (floor[cell] != nowhere_level)
          formed[cell] = std::max (formed[cell] + planed[cell], floor[cell]);
    }

    // Waterfall plunge pools: falling water scours a smooth bowl just
    // downstream of each waterfall's foot, scaled by drop height and river
    // depth, while leaving the rock lip and escarpment above untouched.
    if (parameters.plunge_pool_scale > 0.0f * scale_factor[one]) {
      MOPPE_PROFILE_ZONE ("channels.plunge_pools");
      const meters_t period_x = static_cast<float> (width) * spacing_x;
      const meters_t period_z = static_cast<float> (height) * spacing_z;
      for (const Waterfall& waterfall : rivers.waterfalls) {
        const SurfaceElevation lip_y = ground[waterfall.lip_cell];
        const SurfaceElevation foot_y = ground[waterfall.foot_cell];
        const meters_t drop (lip_y - foot_y);
        if (drop < 1.5f * u::m)
          continue;
        const meters_t river_d =
          std::max (0.4f * u::m, bed_depth_for (waterfall.contributing_area));
        const meters_t pool_depth = std::clamp (
          meters_t (parameters.plunge_pool_scale * sqrt (drop * river_d)),
          0.8f * u::m,
          parameters.plunge_pool_max_depth);
        const meters_t pool_radius =
          std::clamp (0.65f * river_width (waterfall.contributing_area),
                      5.0f * u::m,
                      22.0f * u::m);
        const meters_t lip_x =
          static_cast<float> (waterfall.lip_cell % width) * spacing_x;
        const meters_t lip_z =
          static_cast<float> (waterfall.lip_cell / width) * spacing_z;
        const meters_t foot_x =
          static_cast<float> (waterfall.foot_cell % width) * spacing_x;
        const meters_t foot_z =
          static_cast<float> (waterfall.foot_cell / width) * spacing_z;
        const meters_t dx_m = minimum_image_delta (foot_x - lip_x, period_x);
        const meters_t dz_m = minimum_image_delta (foot_z - lip_z, period_z);
        const meters_t fall_run = hypot (plan_displacement (dx_m, dz_m));
        const quantity<one, float> dir_x =
          fall_run > 1e-3f * u::m ? dx_m / fall_run : 0.0f * one;
        const quantity<one, float> dir_z =
          fall_run > 1e-3f * u::m ? dz_m / fall_run : 0.0f * one;
        const meters_t center_x = foot_x + 0.35f * pool_radius * dir_x;
        const meters_t center_z = foot_z + 0.35f * pool_radius * dir_z;
        const SurfaceElevation rim =
          std::min (formed[waterfall.foot_cell], foot_y);
        const SurfaceElevation cliff_guard = lip_y - 0.5f * drop;
        const int min_x = static_cast<int> (std::floor (
          ((center_x - pool_radius) / spacing_x).numerical_value_in (one)));
        const int max_x = static_cast<int> (std::ceil (
          ((center_x + pool_radius) / spacing_x).numerical_value_in (one)));
        const int min_z = static_cast<int> (std::floor (
          ((center_z - pool_radius) / spacing_z).numerical_value_in (one)));
        const int max_z = static_cast<int> (std::ceil (
          ((center_z + pool_radius) / spacing_z).numerical_value_in (one)));
        for (int az = min_z; az <= max_z; ++az)
          for (int ax = min_x; ax <= max_x; ++ax) {
            const std::size_t cell =
              static_cast<std::size_t> (wrap_index (az, height)) * width +
              wrap_index (ax, width);
            if (flood.ocean[cell] || ground[cell] > cliff_guard)
              continue;
            const meters_t rx = static_cast<float> (ax) * spacing_x - center_x;
            const meters_t rz = static_cast<float> (az) * spacing_z - center_z;
            const meters_t r = hypot (plan_displacement (rx, rz));
            if (r >= pool_radius)
              continue;
            const proportion_t u = r / pool_radius;
            const proportion_t ring = whole - proportion_t (u * u);
            const proportion_t bowl = proportion_t (ring * ring);
            const SurfaceElevation target = rim - meters_t (pool_depth * bowl);
            formed[cell] = std::min (formed[cell], target);
          }
      }
    }

    // Submarine sea inlets: where a river meets the ocean, extend its carved
    // channel smoothly below sea level across any shallow coastal shoal so
    // boats and jet skis can run straight from open sea into the trunk river.
    {
      MOPPE_PROFILE_ZONE ("channels.estuaries");
      const meters_t period_x = static_cast<float> (width) * spacing_x;
      const meters_t period_z = static_cast<float> (height) * spacing_z;
      const auto reaches_sea = [&] (const RiverReach& reach) {
        return reach.downstream_ocean ||
               (census.domain ().contains (reach.downstream_body) &&
                census.water_body (reach.downstream_body).classification ==
                  WaterBodyClass::Sea);
      };
      for (const RiverReach& reach : rivers.reaches) {
        if (!reaches_sea (reach) || reach.cells.empty ())
          continue;
        std::uint32_t last_land = reach.cells.back ();
        std::uint32_t sea_cell = last_land;
        for (int hops = 0; hops < 8 && !flood.ocean[sea_cell]; ++hops) {
          const std::uint32_t next = drainage.receiver[sea_cell];
          if (next == sea_cell)
            break;
          last_land = sea_cell;
          sea_cell = next;
        }
        if (!flood.ocean[sea_cell])
          continue;
        const SurfaceElevation sea_level = flood.water_levels ()[sea_cell];
        meters_t run_x = 0.0f * u::m;
        meters_t run_z = 0.0f * u::m;
        const auto& points = reach.alignment.points;
        if (points.size () >= 2) {
          const plan_position_t mouth = points.back ().place;
          std::size_t back_idx = points.size () - 1;
          while (
            back_idx > 0 &&
            hypot (plan_displacement (
              minimum_image_delta (x_of (mouth) - x_of (points[back_idx].place),
                                   period_x),
              minimum_image_delta (z_of (mouth) - z_of (points[back_idx].place),
                                   period_z))) < 20.0f * u::m)
            --back_idx;
          run_x = minimum_image_delta (
            x_of (mouth) - x_of (points[back_idx].place), period_x);
          run_z = minimum_image_delta (
            z_of (mouth) - z_of (points[back_idx].place), period_z);
        }
        if (hypot (plan_displacement (run_x, run_z)) < 1e-3f * u::m &&
            last_land != sea_cell) {
          run_x = minimum_image_delta (
            static_cast<float> (static_cast<int> (sea_cell % width) -
                                static_cast<int> (last_land % width)) *
              spacing_x,
            period_x);
          run_z = minimum_image_delta (
            static_cast<float> (static_cast<int> (sea_cell / width) -
                                static_cast<int> (last_land / width)) *
              spacing_z,
            period_z);
        }
        const meters_t run = hypot (plan_displacement (run_x, run_z));
        if (run < 1e-3f * u::m)
          continue;
        const quantity<one, float> dir_x = run_x / run;
        const quantity<one, float> dir_z = run_z / run;
        const meters_t mouth_width = bed_width_for (reach.downstream_area);
        const meters_t mouth_depth = bed_depth_for (reach.downstream_area);
        const meters_t inlet_length =
          std::clamp (2.5f * mouth_width, 20.0f * u::m, 95.0f * u::m);
        // Anchor the submarine inlet right at the first coastal ocean cell so
        // it cuts cleanly across the beach shoal at any lattice resolution.
        const meters_t mouth_x =
          static_cast<float> (sea_cell % width) * spacing_x;
        const meters_t mouth_z =
          static_cast<float> (sea_cell / width) * spacing_z;
        const meters_t end_x = mouth_x + inlet_length * dir_x;
        const meters_t end_z = mouth_z + inlet_length * dir_z;
        const meters_t max_half_w = 0.85f * mouth_width;
        const int min_x = static_cast<int> (
          std::floor (((std::min (mouth_x, end_x) - max_half_w) / spacing_x)
                        .numerical_value_in (one)));
        const int max_x = static_cast<int> (
          std::ceil (((std::max (mouth_x, end_x) + max_half_w) / spacing_x)
                       .numerical_value_in (one)));
        const int min_z = static_cast<int> (
          std::floor (((std::min (mouth_z, end_z) - max_half_w) / spacing_z)
                        .numerical_value_in (one)));
        const int max_z = static_cast<int> (
          std::ceil (((std::max (mouth_z, end_z) + max_half_w) / spacing_z)
                       .numerical_value_in (one)));
        for (int az = min_z; az <= max_z; ++az)
          for (int ax = min_x; ax <= max_x; ++ax) {
            const std::size_t cell =
              static_cast<std::size_t> (wrap_index (az, height)) * width +
              wrap_index (ax, width);
            if (!flood.ocean[cell])
              continue;
            const meters_t rx = static_cast<float> (ax) * spacing_x - mouth_x;
            const meters_t rz = static_cast<float> (az) * spacing_z - mouth_z;
            const meters_t along_m = rx * dir_x + rz * dir_z;
            if (along_m < -cell_reach || along_m > inlet_length)
              continue;
            const meters_t side_m = abs (rz * dir_x - rx * dir_z);
            const proportion_t progress =
              std::clamp (proportion_t (along_m / inlet_length),
                          0.0f * proportion[one],
                          whole);
            const meters_t half_w =
              moppe::lerp (0.55f * mouth_width, max_half_w, progress);
            if (side_m >= half_w)
              continue;
            const proportion_t across = side_m / half_w;
            const proportion_t section = whole - proportion_t (across * across);
            const proportion_t taper =
              whole - band (0.4f * proportion[one], whole, progress);
            const SurfaceElevation target =
              sea_level - meters_t (mouth_depth * section * taper);
            formed[cell] = std::min (formed[cell], target);
          }
      }
    }

    // The bed as the lattice draws it. Where the course steps diagonally
    // from one cell to the next, the two cells beside the step stand over
    // it like a dam, and a surface drawn bilinearly between the course's
    // cells -- the ground's or the water's -- pinches shut there: a bed
    // narrower than a cell is a string of hollows touching at their
    // corners, and its water a string of puddles. So the lower of the two
    // is cut to the bed between, and the bed runs on from cell to
    // neighbouring cell.
    {
      MOPPE_PROFILE_ZONE ("channels.lattice");
      for (const std::uint32_t cell : downstream) {
        const std::uint32_t next = receives (cell);
        if (next == none)
          continue;
        const int x = static_cast<int> (cell % width);
        const int z = static_cast<int> (cell / width);
        const int next_x = static_cast<int> (next % width);
        const int next_z = static_cast<int> (next / width);
        if (x == next_x || z == next_z)
          continue;
        const std::uint32_t beside[2] = {
          static_cast<std::uint32_t> (z * width + next_x),
          static_cast<std::uint32_t> (next_z * width + x)
        };
        // Standing water already joins them.
        if (kept[beside[0]] || kept[beside[1]] || kept[cell] || kept[next])
          continue;
        const std::uint32_t lower =
          formed[beside[0]] <= formed[beside[1]] ? beside[0] : beside[1];
        const SurfaceElevation between =
          SurfaceElevation (0.5f * (formed[cell].delta_from_zero () +
                                    formed[next].delta_from_zero ()));
        const meters_t deepest_corner = std::max (
          parameters.deepest_corner, 2.0f * bed_depth_for (catchment (cell)));
        if (formed[lower] > between &&
            formed[lower] - between <= deepest_corner)
          formed[lower] = between;
      }
    }

    ChannelFormationResult result;
    result.heights.assign (elevations.begin (), elevations.end ());
    result.change.assign (count, ElevationChange::zero ());
    const auto cell_area =
      value_cast<double> (spacing_x) * value_cast<double> (spacing_z);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const ElevationChange change = formed[cell] - ground[cell];
      if (change == ElevationChange::zero ())
        continue;
      result.heights[cell] = formed[cell];
      result.change[cell] = change;
      (change < ElevationChange::zero () ? result.cut : result.filled) +=
        cubic_meters_f64_t (abs (change) * cell_area);
    }
    return result;
  }
}
