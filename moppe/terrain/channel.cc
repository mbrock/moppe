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
      // The bed's reach on this cell's side of the stream.
      float half_width = 0.0f;
      float depth = 0.0f;
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
      };
      std::vector<Bed> beds;
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
                            .fixed = fixed[cell] != 0 });
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
        // A river is as wide as its water makes it only where the valley
        // gives it room: its bed ends where the ground already stands a
        // bank's height over it.
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
          for (float* reach : { &here.left, &here.right }) {
            const float side = reach == &here.left ? -1.0f : 1.0f;
            float room = section_step;
            while (room < here.half_width &&
                   ground_at (here.x_m + side * across_x * room,
                              here.z_m + side * across_z * room) < bank)
              room += section_step;
            *reach = std::min (room, here.half_width);
          }
        }
        // A bed does not narrow and widen from one step to the next.
        for (int pass = 0; pass < 3; ++pass)
          for (std::size_t bed = 1; bed + 1 < beds.size (); ++bed) {
            beds[bed].left = 0.25f * beds[bed - 1].left +
                             0.5f * beds[bed].left + 0.25f * beds[bed + 1].left;
            beds[bed].right = 0.25f * beds[bed - 1].right +
                              0.5f * beds[bed].right +
                              0.25f * beds[bed + 1].right;
          }

        for (std::size_t bed = 0; bed + 1 < beds.size (); ++bed) {
          const Bed& start = beds[bed];
          const Bed& end = beds[bed + 1];
          const float run_x = end.x_m - start.x_m;
          const float run_z = end.z_m - start.z_m;
          const float run = std::sqrt (run_x * run_x + run_z * run_z);
          if (run < 1e-3f)
            continue;
          const float radius =
            std::max (start.half_width, end.half_width) + bank_reach;
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
              const float half_width =
                side < 0.0f ? std::lerp (start.left, end.left, t)
                            : std::lerp (start.right, end.right, t);
              Foot& foot =
                feet[static_cast<std::size_t> (wrap_index (at_z, height)) *
                       width +
                     wrap_index (at_x, width)];
              if (distance >= half_width + bank_reach ||
                  distance >= foot.distance)
                continue;
              foot = { .distance = distance,
                       .bed = std::lerp (start.level, end.level, t),
                       .half_width = half_width,
                       .depth = std::lerp (start.depth, end.depth, t) };
            }
        }
      }
    }

    ChannelFormationResult result;
    result.heights.assign (elevations.begin (), elevations.end ());
    result.change_m.assign (count, 0.0f);
    const float deepest_fill =
      parameters.deepest_fill.numerical_value_in (u::m);
    const double cell_area = static_cast<double> (spacing_x) * spacing_z;
    double cut_m3 = 0.0;
    double filled_m3 = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell) {
      const Foot& foot = feet[cell];
      if (foot.distance == nowhere || kept[cell])
        continue;
      // Ground far above the section is the valley's wall, and far below
      // it another place, a drop or a gully beside the stream: neither is
      // the river's to move.
      const float bank = parameters.bank_height (foot.depth);
      float formed = ground[cell];
      if (foot.distance <= foot.half_width) {
        const float across = foot.distance / foot.half_width;
        const float bed = foot.bed - foot.depth * (1.0f - across * across);
        if (ground[cell] >= bed - deepest_fill &&
            ground[cell] <= foot.bed + 2.0f * bank)
          formed = bed;
      } else {
        // Beside the bed the banks are cut back to a slope, and a hollow
        // there is filled level with the bed's edge, or it would stand as
        // a pocket of water the river had passed by.
        const float ramp =
          foot.bed + parameters.bank_slope * (foot.distance - foot.half_width);
        if (ground[cell] < foot.bed) {
          if (ground[cell] >= foot.bed - deepest_fill)
            formed = foot.bed;
        } else if (ground[cell] <= ramp + bank)
          formed = std::min (formed, ramp);
      }
      const float change = formed - ground[cell];
      if (change == 0.0f)
        continue;
      result.heights[cell] =
        SurfaceElevation (formed * surface_elevation[u::m]);
      result.change_m[cell] = change;
      (change < 0.0f ? cut_m3 : filled_m3) += std::abs (change) * cell_area;
    }
    result.cut = cut_m3 * u::m * u::m * u::m;
    result.filled = filled_m3 * u::m * u::m * u::m;
    return result;
  }
}
