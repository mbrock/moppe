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
    const float spacing_x = grid.spacing_x ().numerical_value_in (u::m);
    const float spacing_z = grid.spacing_z ().numerical_value_in (u::m);
    constexpr float nowhere = std::numeric_limits<float>::infinity ();
    const std::uint32_t none = terrain::no_cell;

    std::vector<float> ground (count);
    for (std::size_t cell = 0; cell < count; ++cell)
      ground[cell] = surface_elevation_value (elevations[cell]);

    // The sea and every lake are left as they are. A body that stands as
    // a sheet is a lake unless a river leaves it that is wide against it:
    // that is a pool on the river's way.
    std::vector<std::uint8_t> pool (census.domain ().size (), 0);
    for (const RiverReach& reach : rivers.reaches) {
      if (!census.domain ().contains (reach.upstream_body))
        continue;
      const WaterBody& body = census.water_body (reach.upstream_body);
      const float river =
        river_width (reach.upstream_area).numerical_value_in (u::m);
      if (body.area.numerical_value_in (u::m * u::m) <
          parameters.lake_widths * river * river)
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
      return cr::hypot (static_cast<float> (dx) * spacing_x,
                        static_cast<float> (dz) * spacing_z);
    };

    // The cells of every watercourse: each reach's own, and those it
    // crosses through hollows and pools to meet the next. A lake's sill, a
    // fall's lip and foot, and standing water are fixed points of the profile.
    std::vector<std::uint8_t> course (count, 0);
    std::vector<std::uint8_t> fixed = kept;
    std::vector<float> below_sill (count, nowhere);
    std::vector<std::uint8_t> fed (rivers.reaches.size (), 0);
    for (const RiverReach& reach : rivers.reaches) {
      if (reach.downstream_reach != RiverReach::no_id)
        fed[reach.downstream_reach] = 1;
      if (reach.cells.empty ())
        continue;
      if (lake (reach.upstream_body)) {
        fixed[reach.cells.front ()] = 1;
        below_sill[reach.cells.front ()] = 0.0f;
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
      return drainage.contributing_area_at (cell).numerical_value_in (u::m *
                                                                      u::m);
    };
    std::ranges::sort (downstream, [&] (std::uint32_t a, std::uint32_t b) {
      const float area_a = catchment (a);
      const float area_b = catchment (b);
      return area_a != area_b ? area_a < area_b : a < b;
    });
    const auto receives = [&] (std::uint32_t cell) {
      const std::uint32_t next = drainage.receiver[cell];
      return next != cell && course[next] ? next : none;
    };

    // Cut through every sill, a bed is the lowest ground above it on the
    // stream; filled to every sill, the highest below it. The graded bed
    // lies between.
    std::vector<float> graded = ground;
    {
      MOPPE_PROFILE_ZONE ("channels.grade");
      std::vector<float> cut = ground;
      std::vector<float> filled = ground;
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
          filled[*cell] = std::max (ground[*cell],
                                    filled[next] + parameters.least_grade *
                                                     step (*cell, next));
      }
      // Where the course enters standing water, the profile meets that
      // water's surface, not its floor.
      for (const std::uint32_t cell : downstream)
        graded[cell] = kept[cell] ? flood.water_level_m (cell)
                                  : filled[cell] - parameters.breach *
                                                     (filled[cell] - cut[cell]);

      // Ease it along the stream, each cell toward the mean of what lies
      // above and below it, and let it fall all the way again.
      std::vector<float> above (count, 0.0f);
      std::vector<std::uint16_t> feeders (count, 0);
      std::vector<float> eased (count, 0.0f);
      for (int pass = 0; pass < parameters.easing; ++pass) {
        for (const std::uint32_t cell : downstream) {
          above[cell] = 0.0f;
          feeders[cell] = 0;
        }
        for (const std::uint32_t cell : downstream)
          if (const std::uint32_t next = receives (cell); next != none) {
            above[next] += graded[cell];
            ++feeders[next];
          }
        for (const std::uint32_t cell : downstream) {
          const std::uint32_t next = receives (cell);
          eased[cell] =
            fixed[cell]
              ? graded[cell]
              : 0.5f * graded[cell] +
                  0.25f * (feeders[cell] ? above[cell] / feeders[cell]
                                         : graded[cell]) +
                  0.25f * (next != none ? graded[next] : graded[cell]);
        }
        for (const std::uint32_t cell : downstream)
          graded[cell] = eased[cell];
      }
      // A sill is cut only so deep; a hollow behind a higher one fills
      // the rest of the way.
      const float deepest_cut =
        parameters.deepest_cut.numerical_value_in (u::m);
      for (const std::uint32_t cell : downstream)
        graded[cell] = std::max (graded[cell], ground[cell] - deepest_cut);
      for (auto cell = downstream.rbegin (); cell != downstream.rend ();
           ++cell) {
        const std::uint32_t next = receives (*cell);
        if (next != none && !fixed[*cell])
          graded[*cell] = std::max (graded[*cell],
                                    graded[next] + parameters.least_grade *
                                                     step (*cell, next));
      }
    }

    // The course cell nearest a place on an alignment, which runs within a
    // cell of the chain it was drawn from.
    const auto course_near = [&] (float x_m, float z_m) {
      const int near_x = static_cast<int> (std::lround (x_m / spacing_x));
      const int near_z = static_cast<int> (std::lround (z_m / spacing_z));
      std::uint32_t found = none;
      float nearest = nowhere;
      for (int dz = -2; dz <= 2; ++dz)
        for (int dx = -2; dx <= 2; ++dx) {
          const std::uint32_t cell = static_cast<std::uint32_t> (
            wrap_index (near_z + dz, height) * width +
            wrap_index (near_x + dx, width));
          if (!course[cell])
            continue;
          const float from_x =
            static_cast<float> (near_x + dx) * spacing_x - x_m;
          const float from_z =
            static_cast<float> (near_z + dz) * spacing_z - z_m;
          const float distance = from_x * from_x + from_z * from_z;
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
      float distance = std::numeric_limits<float>::infinity ();
      float bed = 0.0f;
      // The bed's reach on this cell's side of the stream, and that side's
      // bank: how far a bar shoals it (0 to 1), how far a cut steepens it,
      // the bank's slope, and how far out the river works it.
      float half_width = 0.0f;
      float depth = 0.0f;
      float bar = 0.0f;
      float cut = 0.0f;
      float slope = 0.0f;
      float reach = 0.0f;
    };
    const auto ground_at = [&] (float world_x, float world_z) {
      const float gx =
        wrap_coordinate (world_x / spacing_x, static_cast<float> (width));
      const float gz =
        wrap_coordinate (world_z / spacing_z, static_cast<float> (height));
      const int x0 = static_cast<int> (std::floor (gx));
      const int z0 = static_cast<int> (std::floor (gz));
      const int x1 = wrap_index (x0 + 1, width);
      const int z1 = wrap_index (z0 + 1, height);
      const float tx = gx - x0;
      const float tz = gz - z0;
      const auto at = [&] (int x, int z) {
        return ground[static_cast<std::size_t> (z) * width + x];
      };
      return std::lerp (std::lerp (at (x0, z0), at (x1, z0), tx),
                        std::lerp (at (x0, z1), at (x1, z1), tx),
                        tz);
    };
    std::vector<Foot> feet (count);
    constexpr float bank_reach = 6.0f;
    const float bar_reach = parameters.bar_reach.numerical_value_in (u::m);
    const float bar_widths = parameters.bar_widths;
    // How far a full bar runs out from a bed of this half width.
    const auto strand_reach = [&] (float half_width) {
      return std::max (bar_reach, bar_widths * 2.0f * half_width);
    };
    const float bar_lowering =
      parameters.bar_lowering.numerical_value_in (u::m);
    // How far beyond a strand the ground is still eased toward it.
    constexpr float bar_riser = 8.0f;
    constexpr float section_step = 1.0f;
    const float cell_reach = std::max (spacing_x, spacing_z);
    const float least_width = parameters.least_width.numerical_value_in (u::m);
    const float least_depth = parameters.least_depth.numerical_value_in (u::m);
    {
      MOPPE_PROFILE_ZONE ("channels.sections");
      struct Bed {
        float x_m;
        float z_m;
        float level;
        // The widest the bed may be, and how far it reaches to the left
        // and right before the valley's own sides stand over it.
        float half_width;
        float left;
        float right;
        float depth;
        // A fixed point of the profile, which easing leaves be.
        bool fixed;
        // Each side's bar and cut (Foot), left and right.
        float bar[2];
        float cut[2];
      };
      std::vector<Bed> beds;
      std::vector<float> along;
      std::vector<float> turning;
      for (const RiverReach& reach : rivers.reaches) {
        const auto& points = reach.alignment.points;
        const bool springs =
          !fed[reach.id] &&
          (points.empty () || points.front ().standing_water < 0.99f);
        beds.clear ();
        for (const RiverAlignmentPoint& point : points) {
          const std::uint32_t cell = course_near (point.x_m, point.z_m);
          if (cell == none)
            continue;
          const float bed_width =
            std::max (least_width,
                      parameters.width_share *
                        river_width (point.contributing_area_m2 * u::m * u::m)
                          .numerical_value_in (u::m));
          float depth = least_depth * cr::pow (bed_width / least_width,
                                               parameters.depth_growth);
          // Below a lake the bed starts at the sill; a spring's begins as
          // nothing.
          if (below_sill[cell] != nowhere)
            depth *= smoothstep (0.0f, 2.5f * bed_width, below_sill[cell]);
          if (springs)
            depth *= smoothstep (0.0f, 3.0f * bed_width, point.distance_m);
          beds.push_back ({ .x_m = point.x_m,
                            .z_m = point.z_m,
                            .level = graded[cell],
                            .half_width = 0.5f * bed_width,
                            .left = 0.0f,
                            .right = 0.0f,
                            .depth = depth,
                            .fixed = fixed[cell] != 0,
                            .bar = { 0.0f, 0.0f },
                            .cut = { 0.0f, 0.0f } });
        }
        // The chain's corners out of the alignment's levels, which still
        // only fall.
        for (int pass = 0; pass < 2; ++pass)
          for (std::size_t bed = 1; bed + 1 < beds.size (); ++bed)
            if (!beds[bed].fixed)
              beds[bed].level = 0.25f * beds[bed - 1].level +
                                0.5f * beds[bed].level +
                                0.25f * beds[bed + 1].level;
        for (std::size_t bed = 1; bed < beds.size (); ++bed)
          beds[bed].level = std::min (beds[bed].level, beds[bed - 1].level);
        // How the river runs here: how steeply its bed falls, and how
        // sharply it bends, over a window of some lengths of its width.
        // A river falling gently over a mobile bed meanders and builds bars
        // of what it carries on the inside of its bends while it cuts into
        // the outside; a steep one runs straight down steps of boulders
        // between whatever banks its valley gives it.
        along.assign (beds.size (), 0.0f);
        for (std::size_t bed = 1; bed < beds.size (); ++bed)
          along[bed] =
            along[bed - 1] + cr::hypot (beds[bed].x_m - beds[bed - 1].x_m,
                                        beds[bed].z_m - beds[bed - 1].z_m);
        turning.assign (beds.size (), 0.0f);
        std::vector<float> grades (beds.size (), 0.0f);
        {
          std::size_t first = 0;
          std::size_t last = 0;
          for (std::size_t bed = 0; bed < beds.size (); ++bed) {
            const float window = std::max (8.0f, 3.0f * beds[bed].half_width);
            while (along[bed] - along[first] > window)
              ++first;
            while (last + 1 < beds.size () &&
                   along[last + 1] - along[bed] <= window)
              ++last;
            const float run = along[last] - along[first];
            if (run < 1.0f)
              continue;
            grades[bed] = (beds[first].level - beds[last].level) / run;
            const float in_x = beds[bed].x_m - beds[first].x_m;
            const float in_z = beds[bed].z_m - beds[first].z_m;
            const float out_x = beds[last].x_m - beds[bed].x_m;
            const float out_z = beds[last].z_m - beds[bed].z_m;
            const float in = cr::hypot (in_x, in_z);
            const float out = cr::hypot (out_x, out_z);
            if (in < 0.5f || out < 0.5f)
              continue;
            // The angle turned, toward the right positive, over the
            // distance it turns in: the curvature.
            const float turned = std::atan2 (in_x * out_z - in_z * out_x,
                                             in_x * out_x + in_z * out_z);
            turning[bed] = turned / (0.5f * run);
          }
          for (int pass = 0; pass < 4; ++pass)
            for (std::size_t bed = 1; bed + 1 < beds.size (); ++bed)
              turning[bed] = 0.25f * turning[bed - 1] + 0.5f * turning[bed] +
                             0.25f * turning[bed + 1];
        }
        for (std::size_t bed = 0; bed < beds.size (); ++bed) {
          Bed& here = beds[bed];
          const float calm = 1.0f - smoothstep (parameters.calm_grade,
                                                parameters.steep_grade,
                                                grades[bed]);
          // A bend is tight when its radius is a few widths of its river.
          const float width = 2.0f * here.half_width;
          const float bend =
            1.0f - smoothstep (
                     parameters.tight_bend,
                     parameters.open_bend,
                     1.0f / std::max (std::abs (turning[bed]) * width, 1e-4f));
          // Right is the inside of a bend turning right.
          const int inside = turning[bed] > 0.0f ? 1 : 0;
          here.bar[inside] =
            calm * std::lerp (parameters.straight_bar, 1.0f, bend);
          here.bar[1 - inside] = calm * parameters.straight_bar * (1.0f - bend);
          here.cut[inside] = 0.0f;
          here.cut[1 - inside] = calm * bend;
        }
        // A river is as wide as its water makes it only where the valley
        // gives it room: its bed ends where the ground already stands a
        // bank's height over it. A bar widens it on its own side.
        for (std::size_t bed = 0; bed < beds.size (); ++bed) {
          Bed& here = beds[bed];
          const Bed& before = beds[bed ? bed - 1 : bed];
          const Bed& after = beds[bed + 1 < beds.size () ? bed + 1 : bed];
          const float run_x = after.x_m - before.x_m;
          const float run_z = after.z_m - before.z_m;
          const float run = std::sqrt (run_x * run_x + run_z * run_z);
          if (run < 1e-3f)
            continue;
          const float across_x = -run_z / run;
          const float across_z = run_x / run;
          const float bank = here.level + parameters.bank_height (here.depth);
          for (int side = 0; side < 2; ++side) {
            const float sign = side ? 1.0f : -1.0f;
            const float widest =
              here.half_width *
              (1.0f + parameters.bar_widening * here.bar[side]);
            float room = section_step;
            while (room < widest &&
                   ground_at (here.x_m + sign * across_x * room,
                              here.z_m + sign * across_z * room) < bank)
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
          const float run_x = end.x_m - start.x_m;
          const float run_z = end.z_m - start.z_m;
          const float run = std::sqrt (run_x * run_x + run_z * run_z);
          if (run < 1e-3f)
            continue;
          const float widest =
            std::max ({ start.left, start.right, end.left, end.right });
          const float radius =
            widest + bank_reach + strand_reach (widest) + bar_riser;
          const int minimum_x = static_cast<int> (
            std::floor ((std::min (start.x_m, end.x_m) - radius) / spacing_x));
          const int maximum_x = static_cast<int> (
            std::ceil ((std::max (start.x_m, end.x_m) + radius) / spacing_x));
          const int minimum_z = static_cast<int> (
            std::floor ((std::min (start.z_m, end.z_m) - radius) / spacing_z));
          const int maximum_z = static_cast<int> (
            std::ceil ((std::max (start.z_m, end.z_m) + radius) / spacing_z));
          for (int at_z = minimum_z; at_z <= maximum_z; ++at_z)
            for (int at_x = minimum_x; at_x <= maximum_x; ++at_x) {
              const float world_x = static_cast<float> (at_x) * spacing_x;
              const float world_z = static_cast<float> (at_z) * spacing_z;
              const float along = ((world_x - start.x_m) * run_x +
                                   (world_z - start.z_m) * run_z) /
                                  run;
              if (along < -cell_reach || along > run + cell_reach)
                continue;
              const float t = std::clamp (along / run, 0.0f, 1.0f);
              const float from_x = world_x - std::lerp (start.x_m, end.x_m, t);
              const float from_z = world_z - std::lerp (start.z_m, end.z_m, t);
              const float distance =
                std::sqrt (from_x * from_x + from_z * from_z);
              const float side =
                from_z * (run_x / run) - from_x * (run_z / run);
              const int on = side < 0.0f ? 0 : 1;
              const float half_width = on
                                         ? std::lerp (start.right, end.right, t)
                                         : std::lerp (start.left, end.left, t);
              const float bar = std::lerp (start.bar[on], end.bar[on], t);
              const float cut = std::lerp (start.cut[on], end.cut[on], t);
              const float reach =
                bank_reach + bar * (strand_reach (half_width) + bar_riser);
              Foot& foot =
                feet[static_cast<std::size_t> (wrap_index (at_z, height)) *
                       width +
                     wrap_index (at_x, width)];
              if (distance >= half_width + reach || distance >= foot.distance)
                continue;
              foot = { .distance = distance,
                       .bed = std::lerp (start.level, end.level, t),
                       .half_width = half_width,
                       .depth = std::lerp (start.depth, end.depth, t),
                       .bar = bar,
                       .cut = cut,
                       .slope = std::lerp (
                         parameters.bank_slope, parameters.cut_bank_slope, cut),
                       .reach = reach };
            }
        }
      }
    }

    const float deepest_fill =
      parameters.deepest_fill.numerical_value_in (u::m);
    std::vector<float> formed = ground;
    // What a bar planes off beside the bed, beyond the bank, and the least
    // it may leave: kept apart until it is smoothed, since the nearest
    // section changes from cell to cell where a bend's sections fan out.
    std::vector<float> planed (count, 0.0f);
    std::vector<float> floor (count, nowhere);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const Foot& foot = feet[cell];
      if (foot.distance == nowhere || kept[cell])
        continue;
      // Ground far above the section is the valley's wall, and far below
      // it another place, a drop or a gully beside the stream: neither is
      // the river's to move.
      const float bank = parameters.bank_height (foot.depth);
      if (foot.distance <= foot.half_width) {
        // Under a bar the bed shoals from the deep water to the strand;
        // under a cut bank it stays deep up to the bank's foot.
        const float across = foot.distance / foot.half_width;
        const float shoaling = (1.0f - across) * (1.0f - across);
        const float plunging = 1.0f - across * across * across * across;
        const float section =
          std::lerp (std::lerp (1.0f - across * across, shoaling, foot.bar),
                     plunging,
                     foot.cut);
        const float bed = foot.bed - foot.depth * section;
        if (ground[cell] >= bed - deepest_fill &&
            ground[cell] <= foot.bed + 2.0f * bank)
          formed[cell] = bed;
        continue;
      }
      // Beside the bed the banks are cut back to a slope, and a hollow
      // there is filled level with the bed's edge, or it would stand as a
      // pocket of water the river had passed by.
      const float beyond = foot.distance - foot.half_width;
      if (ground[cell] < foot.bed) {
        if (beyond < bank_reach && ground[cell] >= foot.bed - deepest_fill)
          formed[cell] = foot.bed;
        continue;
      }
      const float ramp = foot.bed + foot.slope * beyond;
      if (beyond < bank_reach && ground[cell] <= ramp + bank)
        formed[cell] = std::min (ground[cell], ramp);
      // A calm river swinging across its floor has planed it down beside
      // itself: a strand as far as its bar reaches, rising beyond like any
      // bank to the floor it has not yet worked, and lowered no more than
      // its bar has room to have done.
      if (foot.bar > 0.0f) {
        const float strand_run = foot.bar * strand_reach (foot.half_width);
        const float strand_slope =
          std::lerp (parameters.bank_slope, parameters.bar_slope, foot.bar);
        const float strand =
          foot.bed + strand_slope * std::min (beyond, strand_run) +
          parameters.bank_slope * std::max (beyond - strand_run, 0.0f);
        const float lowering =
          bar_lowering * foot.bar *
          (1.0f - smoothstep (strand_run, strand_run + bar_riser, beyond));
        planed[cell] = std::min (
          0.0f, std::max (strand, ground[cell] - lowering) - formed[cell]);
        floor[cell] = std::min (formed[cell], strand);
      }
    }
    {
      MOPPE_PROFILE_ZONE ("channels.planing");
      std::vector<float> eased (count);
      for (int pass = 0; pass < 3; ++pass) {
        for (int z = 0; z < height; ++z)
          for (int x = 0; x < width; ++x) {
            const std::size_t cell = static_cast<std::size_t> (z) * width + x;
            eased[cell] = planed[cell];
            if (floor[cell] == nowhere)
              continue;
            // Among the planed cells only: the bed beside them is not
            // planed, and would hold the bank's foot up.
            float sum = 0.0f;
            int among = 0;
            for (int dz = -1; dz <= 1; ++dz)
              for (int dx = -1; dx <= 1; ++dx) {
                const std::size_t near =
                  static_cast<std::size_t> (wrap_index (z + dz, height)) *
                    width +
                  wrap_index (x + dx, width);
                if (floor[near] == nowhere)
                  continue;
                sum += planed[near];
                ++among;
              }
            eased[cell] = sum / static_cast<float> (among);
          }
        planed.swap (eased);
      }
      for (std::size_t cell = 0; cell < count; ++cell)
        if (floor[cell] != nowhere)
          formed[cell] = std::max (formed[cell] + planed[cell], floor[cell]);
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
      const float deepest_corner =
        parameters.deepest_corner.numerical_value_in (u::m);
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
        const float between = 0.5f * (formed[cell] + formed[next]);
        if (formed[lower] > between &&
            formed[lower] - between <= deepest_corner)
          formed[lower] = between;
      }
    }

    ChannelFormationResult result;
    result.heights.assign (elevations.begin (), elevations.end ());
    result.change_m.assign (count, 0.0f);
    const double cell_area = static_cast<double> (spacing_x) * spacing_z;
    double cut_m3 = 0.0;
    double filled_m3 = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell) {
      const float change = formed[cell] - ground[cell];
      if (change == 0.0f)
        continue;
      result.heights[cell] =
        SurfaceElevation (formed[cell] * surface_elevation[u::m]);
      result.change_m[cell] = change;
      (change < 0.0f ? cut_m3 : filled_m3) += std::abs (change) * cell_area;
    }
    result.cut = cut_m3 * u::m * u::m * u::m;
    result.filled = filled_m3 * u::m * u::m * u::m;
    return result;
  }
}
