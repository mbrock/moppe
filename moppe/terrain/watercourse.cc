#include <moppe/correct_math.hh>
#include <moppe/terrain/watercourse.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    constexpr float gravity = 9.81f;

    // The valley across one alignment point: the ground a step apart from
    // left to right, the alignment's own sample at CENTRE and the lowest
    // near it at THALWEG. Running water may spread SPREAD samples either
    // side; the profile runs on beyond for water a pit or a sill holds
    // wider than that.
    struct Section {
      std::span<const float> ground;
      float step = 1.0f;
      int centre = 0;
      int thalweg = 0;
      int spread = 0;
    };

    // The water a level holds in a section: what joins the thalweg without
    // crossing dry ground.
    struct Wetted {
      // How far it reaches either side of the alignment, in metres.
      float left = 0.0f;
      float right = 0.0f;
      // The integral of depth^(3/2) across it: by Chezy's law the discharge
      // it carries over the root of its slope and the roughness, and at a
      // given Froude number the discharge over that number's pace.
      float conveyance = 0.0f;
      float deepest = 0.0f;
      // Wet as far as the water may spread.
      bool open_left = false;
      bool open_right = false;
      // Wet as far as the section goes: ground that falls away rather than
      // a bank.
      bool unbanked = false;
    };

    Wetted wetted (const Section& section, float level) {
      Wetted water;
      const int count = static_cast<int> (section.ground.size ());
      water.deepest = level - section.ground[section.thalweg];
      if (!(water.deepest > 0.0f))
        return { .deepest = 0.0f };
      const auto carry = [&] (int sample) {
        const float depth = level - section.ground[sample];
        water.conveyance += depth * std::sqrt (depth) * section.step;
      };
      int first = section.thalweg;
      int last = section.thalweg;
      carry (first);
      while (first > 0 && section.ground[first - 1] < level)
        carry (--first);
      while (last + 1 < count && section.ground[last + 1] < level)
        carry (++last);
      water.open_left = first <= section.centre - section.spread;
      water.open_right = last >= section.centre + section.spread;
      water.unbanked = first == 0 || last + 1 == count;
      water.left = std::max (
        0.0f,
        (static_cast<float> (section.centre - first) + 0.5f) * section.step);
      water.right = std::max (
        0.0f,
        (static_cast<float> (last - section.centre) + 0.5f) * section.step);
      return water;
    }

    // The least stage that passes a discharge through a section: one that
    // carries it at no more than the critical pace, and that stands above
    // the water DOWN, RUN metres below, by the head the bed's friction
    // takes on the way. Both ask more of a higher stage, so they bisect.
    float stage (const Section& section,
                 float discharge,
                 float down,
                 float run,
                 const WatercoursePaint& parameters) {
      const float pace = parameters.critical_froude * std::sqrt (gravity);
      float low = std::max (down, section.ground[section.thalweg]);
      if (!(discharge > 0.0f))
        return low;
      float high = low + 8.0f;
      for (int step = 0; step < 16; ++step) {
        const float middle = 0.5f * (low + high);
        const float conveyance = wetted (section, middle).conveyance;
        bool passes = conveyance * pace >= discharge;
        if (passes) {
          const float slope = discharge / (parameters.roughness * conveyance);
          passes = middle - down >= slope * slope * run;
        }
        (passes ? high : low) = middle;
      }
      return high;
    }

    float wave_factor (WaterBodyClass classification) {
      switch (classification) {
      case WaterBodyClass::Sea:
        return 1.0f;
      case WaterBodyClass::Lake:
        return 0.10f;
      case WaterBodyClass::Pond:
        return 0.04f;
      case WaterBodyClass::Puddle:
        return 0.0f;
      }
      return 0.0f;
    }

    float wave_factor (const FloodField& flood,
                       const LakeCensus& census,
                       std::size_t cell) {
      if (flood.ocean[cell])
        return 1.0f;
      const WaterBodyId body =
        census.body_at (CellIndex { static_cast<std::uint32_t> (cell) });
      if (!census.domain ().contains (body))
        return 0.0f;
      return wave_factor (census.water_body (body).classification);
    }
  }

  WaterSheets
  detail::paint_watercourses (const TerrainDomain& terrain_domain,
                              std::span<const SurfaceElevation> elevations,
                              const FloodField& flood,
                              const LakeCensus& census,
                              const DrainageGraph& drainage,
                              const RiverNetwork& rivers,
                              const WatercoursePaint& parameters) {
    MOPPE_PROFILE_ZONE ("paint_watercourses");
    const TerrainDomain& grid = flood.domain ();
    if (terrain_domain != grid)
      throw std::invalid_argument (
        "watercourse terrain does not match hydrology");
    const int width = static_cast<int> (grid.width ());
    const int height = static_cast<int> (grid.height ());
    const std::size_t count = grid.width () * grid.height ();
    if (census.cell_count () != count || drainage.receiver.size () != count)
      throw std::invalid_argument (
        "watercourse painting inputs do not share a grid");

    // Standing bodies establish the first surface. Running alignments are
    // painted into this same field below; the renderer's exact cell clipper
    // turns the signed field into geometry without reach seams or dry-corner
    // flaps.
    const ElevationMap permanent =
      permanent_water_surface (flood, census, parameters.permanence);
    const auto& permanent_elevations =
      spatial::get<surface_elevation> (permanent);
    std::vector<float> surface;
    surface.reserve (count);
    for (SurfaceElevation elevation : permanent_elevations)
      surface.push_back (surface_elevation_value (elevation));
    std::vector<float> amplitude (count);
    for (std::size_t cell = 0; cell < count; ++cell)
      amplitude[cell] = wave_factor (flood, census, cell);

    // The current's direction, its pace (speed over the root of depth), the
    // grade of the surface it runs down, and the falls it passes, each
    // summed under the stamps' weights.
    std::vector<float> flow_x (count, 0.0f);
    std::vector<float> flow_z (count, 0.0f);
    std::vector<float> flow_pace (count, 0.0f);
    std::vector<float> flow_grade (count, 0.0f);
    std::vector<float> flow_fall (count, 0.0f);
    std::vector<float> flow_weight (count, 0.0f);

    struct RunningPoint {
      float x_m;
      float z_m;
      float level_m;
      float half_width_m;
      // The depth of the pit it stands in, if it stands in one.
      float depth_m;
      float pace;
      float grade;
      float discharge;
      float waterfall;
      float source_profile;
      float standing_water;
    };

    const float spacing_x = (grid.spacing_x ()).numerical_value_in (u::m);
    const float spacing_z = (grid.spacing_z ()).numerical_value_in (u::m);
    const auto sample_ground = [&] (float world_x, float world_z) {
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
      const float h00 = elevation_at (grid, elevations, x0, z0);
      const float h10 = elevation_at (grid, elevations, x1, z0);
      const float h01 = elevation_at (grid, elevations, x0, z1);
      const float h11 = elevation_at (grid, elevations, x1, z1);
      return std::lerp (std::lerp (h00, h10, tx), std::lerp (h01, h11, tx), tz);
    };
    const auto cell_at = [&] (float world_x, float world_z) {
      const int x = wrap_index (
        static_cast<int> (std::lround (world_x / spacing_x)), width);
      const int z = wrap_index (
        static_cast<int> (std::lround (world_z / spacing_z)), height);
      return static_cast<std::size_t> (z) * width + x;
    };
    // A sheet-drawn body: its level is drawn to its whole shore, beyond any
    // stamp, so a river through it must raise all of it or none.
    const auto sheet_drawn = [&] (WaterBodyId body) {
      return census.domain ().contains (body) &&
             water_body_is_permanent (census.water_body (body),
                                      parameters.permanence);
    };

    std::vector<std::vector<RiverReachId>> upstream_reaches (
      rivers.reaches.size ());
    for (const RiverReach& reach : rivers.reaches)
      if (reach.downstream_reach != RiverReach::no_id)
        upstream_reaches[reach.downstream_reach].push_back (reach.id);
    // The reach a lake spills into.
    std::vector<RiverReachId> outlet (census.domain ().size (),
                                      RiverReach::no_id);
    for (const RiverReach& reach : rivers.reaches)
      if (sheet_drawn (reach.upstream_body) &&
          outlet[census.domain ().offset (reach.upstream_body)] ==
            RiverReach::no_id)
        outlet[census.domain ().offset (reach.upstream_body)] = reach.id;

    // A river's surface is found from its mouth upward, so each reach
    // follows the one it runs into, or the one its lake spills into.
    std::vector<RiverReachId> mouth_first;
    mouth_first.reserve (rivers.reaches.size ());
    {
      const auto below = [&] (const RiverReach& reach) {
        if (reach.downstream_reach != RiverReach::no_id)
          return reach.downstream_reach;
        if (!reach.downstream_ocean && sheet_drawn (reach.downstream_body))
          return outlet[census.domain ().offset (reach.downstream_body)];
        return RiverReach::no_id;
      };
      std::vector<std::uint8_t> listed (rivers.reaches.size (), 0);
      std::vector<RiverReachId> chain;
      for (const RiverReach& reach : rivers.reaches) {
        chain.clear ();
        for (RiverReachId id = reach.id; id != RiverReach::no_id && !listed[id];
             id = below (rivers.reaches[id])) {
          listed[id] = 1;
          chain.push_back (id);
        }
        mouth_first.insert (mouth_first.end (), chain.rbegin (), chain.rend ());
      }
    }

    // Where a river leaves a lake or runs through a pool, the body stands
    // above the level it would only just spill at, by the head its outflow
    // needs. The river's own profile says how high its channel would hold
    // the body; but a rim may be low elsewhere too, and water that high
    // would pour over it there as well. So the rising water is followed
    // out over the shore, a pour height for every place it reaches, and
    // wherever such a place away from the river's channel looks down on
    // ground beyond the body it is a weir, passing water at the critical
    // pace. The body stands at the level where the channel and the weirs
    // together pass the outflow.
    constexpr float unraised = std::numeric_limits<float>::infinity ();
    std::vector<float> raised (census.domain ().size (), unraised);
    std::vector<std::vector<std::uint32_t>> members (census.domain ().size ());
    for (std::size_t cell = 0; cell < count; ++cell) {
      const WaterBodyId body =
        census.body_at (CellIndex { static_cast<std::uint32_t> (cell) });
      if (!flood.ocean[cell] && sheet_drawn (body))
        members[census.domain ().offset (body)].push_back (
          static_cast<std::uint32_t> (cell));
    }
    std::vector<std::uint32_t> reached (count, 0);
    std::uint32_t reaching = 0;
    const float weir_pace = parameters.critical_froude * std::sqrt (gravity) *
                            std::min (spacing_x, spacing_z);
    // Stands BODY at the level it passes DISCHARGE at, no higher than HELD,
    // the level the river's channel would hold it to alone; paints it and
    // the shore it rises over, and returns the level. The channel is the
    // rim within CHANNEL metres of the body's spill.
    const auto settle = [&] (WaterBodyId body,
                             float held,
                             float discharge,
                             float channel) {
      const WaterBody& record = census.water_body (body);
      const float spilled = record.surface_level.numerical_value_in (u::m);
      const float least = spilled + 0.045f;
      if (!(held > least))
        return least;
      struct Reached {
        float pour;
        std::uint32_t cell;
        bool operator> (const Reached& other) const {
          return pour != other.pour ? pour > other.pour : cell > other.cell;
        }
      };
      std::vector<Reached> rising;
      std::vector<Reached> shore;
      std::vector<float> weirs;
      ++reaching;
      for (const std::uint32_t cell : members[census.domain ().offset (body)]) {
        reached[cell] = reaching;
        rising.push_back ({ spilled, cell });
      }
      std::ranges::make_heap (rising, std::greater<> ());
      while (!rising.empty ()) {
        std::ranges::pop_heap (rising, std::greater<> ());
        const Reached here = rising.back ();
        rising.pop_back ();
        const int x = static_cast<int> (here.cell % width);
        const int z = static_cast<int> (here.cell / width);
        bool weir = false;
        for (int dz = -1; dz <= 1; ++dz)
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = wrap_index (x + dx, width);
            const int nz = wrap_index (z + dz, height);
            const std::uint32_t next =
              static_cast<std::uint32_t> (nz * width + nx);
            if (reached[next] == reaching)
              continue;
            const float ground = elevation_at (grid, elevations, nx, nz);
            // Lower than the body ever stood and yet outside it: the far
            // side of its rim.
            if (ground < spilled - 1e-4f) {
              weir = true;
              continue;
            }
            const float pour = std::max (here.pour, ground);
            if (pour >= held)
              continue;
            reached[next] = reaching;
            shore.push_back ({ pour, next });
            rising.push_back ({ pour, next });
            std::ranges::push_heap (rising, std::greater<> ());
          }
        if (weir && record.spill_cell != WaterBody::no_cell) {
          const float away_x = std::remainder (
            static_cast<float> (x -
                                static_cast<int> (record.spill_cell % width)) *
              spacing_x,
            static_cast<float> (width) * spacing_x);
          const float away_z = std::remainder (
            static_cast<float> (z -
                                static_cast<int> (record.spill_cell / width)) *
              spacing_z,
            static_cast<float> (height) * spacing_z);
          weir = away_x * away_x + away_z * away_z > channel * channel;
        }
        if (weir)
          weirs.push_back (here.pour);
      }
      // The channel's share falls off as the square of its stage, between
      // a notch's and a broad sill's.
      const auto passes = [&] (float level) {
        const float risen = (level - spilled) / (held - spilled);
        float carried = 0.0f;
        for (const float crest : weirs)
          if (level > crest)
            carried += (level - crest) * std::sqrt (level - crest);
        return discharge * risen * risen + carried * weir_pace >= discharge;
      };
      float stands = held;
      if (!weirs.empty ()) {
        float low = spilled;
        for (int step = 0; step < 16; ++step) {
          const float middle = 0.5f * (low + stands);
          (passes (middle) ? stands : low) = middle;
        }
      }
      stands = std::max (stands, least);
      for (const std::uint32_t cell : members[census.domain ().offset (body)])
        surface[cell] = std::max (surface[cell], stands);
      for (const Reached& place : shore)
        if (place.pour < stands && surface[place.cell] < stands) {
          surface[place.cell] = stands;
          amplitude[place.cell] = wave_factor (record.classification);
        }
      return stands;
    };

    std::vector<std::vector<RunningPoint>> reach_points (
      rivers.reaches.size ());
    // Each reach's cross-sections, kept until its levels are final.
    std::vector<std::vector<float>> reach_ground (rivers.reaches.size ());
    std::vector<std::vector<Section>> reach_sections (rivers.reaches.size ());
    const float section_step =
      std::clamp (0.4f * std::min (spacing_x, spacing_z), 0.5f, 1.0f);
    // Half the five metres a visible river begins at (river.hh).
    const int thread_samples =
      std::max (1, static_cast<int> (std::lround (2.5f / section_step)));
    const int thalweg_reach =
      std::max (1,
                static_cast<int> (std::lround (std::min (spacing_x, spacing_z) /
                                               section_step)));
    {
      MOPPE_PROFILE_ZONE ("watercourse.prepare_running_surface");
      for (const RiverReachId id : mouth_first) {
        const RiverReach& reach = rivers.reaches[id];
        const auto& points = reach.alignment.points;
        const std::size_t length = points.size ();
        auto& prepared = reach_points[reach.id];
        if (length == 0)
          continue;
        const bool fade_source = upstream_reaches[reach.id].empty () &&
                                 points.front ().standing_water < 0.99f;
        const float source_width =
          river_width (points.front ().contributing_area_m2 * u::m * u::m)
            .numerical_value_in (u::m);
        const float source_length = std::max (14.0f, 3.0f * source_width);

        // The valley across each point: as far as its water may spread, and
        // as far again and more for what a pit or a sill holds beyond that.
        std::vector<std::size_t> first (length + 1, 0);
        std::vector<int> spread_samples (length);
        std::vector<int> reach_samples (length);
        for (std::size_t point = 0; point < length; ++point) {
          const float width_m =
            river_width (points[point].contributing_area_m2 * u::m * u::m)
              .numerical_value_in (u::m);
          const float spread_m = 0.5f * parameters.spread * width_m + 1.5f;
          spread_samples[point] =
            static_cast<int> (std::ceil (spread_m / section_step));
          reach_samples[point] = static_cast<int> (
            std::ceil ((2.0f * spread_m + 12.0f) / section_step));
          first[point + 1] = first[point] + 2 * reach_samples[point] + 1;
        }
        auto& ground_samples = reach_ground[reach.id];
        auto& sections = reach_sections[reach.id];
        ground_samples.resize (first[length]);
        sections.resize (length);
        std::vector<float> discharge (length);
        std::vector<float> bed (length);
        std::vector<float> thread (length);
        std::vector<WaterBodyId> body (length, no_water_body);
        for (std::size_t point = 0; point < length; ++point) {
          const RiverAlignmentPoint& center = points[point];
          const RiverAlignmentPoint& before = points[point ? point - 1 : point];
          const RiverAlignmentPoint& after =
            points[point + 1 < length ? point + 1 : point];
          const float run_x = after.x_m - before.x_m;
          const float run_z = after.z_m - before.z_m;
          const float run = cr::hypot (run_x, run_z);
          const float tangent_x = run > 1e-5f ? run_x / run : 0.0f;
          const float tangent_z = run > 1e-5f ? run_z / run : 1.0f;
          const float across_x = -tangent_z;
          const float across_z = tangent_x;
          const int reach_n = reach_samples[point];
          const std::span<float> across (ground_samples.data () + first[point],
                                         2 * reach_n + 1);
          for (int sample = -reach_n; sample <= reach_n; ++sample) {
            const float offset = static_cast<float> (sample) * section_step;
            across[sample + reach_n] = sample_ground (
              center.x_m + across_x * offset, center.z_m + across_z * offset);
          }
          int thalweg = reach_n;
          const int search = std::min (thalweg_reach, spread_samples[point]);
          for (int sample = reach_n - search; sample <= reach_n + search;
               ++sample)
            if (across[sample] < across[thalweg])
              thalweg = sample;
          sections[point] = { .ground = across,
                              .step = section_step,
                              .centre = reach_n,
                              .thalweg = thalweg,
                              .spread = spread_samples[point] };
          bed[point] = across[thalweg];
          // A river is at least a thread wide enough to see: the stage that
          // wets the visible width about its thalweg, though in a cleft too
          // narrow for that no deeper than a wader's knee.
          thread[point] = std::clamp (
            std::min (
              across[std::max (0, thalweg - thread_samples)],
              across[std::min (2 * reach_n, thalweg + thread_samples)]) -
              0.08f,
            bed[point] + 0.035f,
            bed[point] + 0.35f);
          discharge[point] =
            river_discharge (center.contributing_area_m2 * u::m * u::m)
              .numerical_value_in (u::m * u::m * u::m / u::s);
          if (center.standing_water > 0.0f || center.pooled > 0.25f)
            body[point] = census.body_at (CellIndex {
              static_cast<std::uint32_t> (cell_at (center.x_m, center.z_m)) });
        }

        // The levels, from the mouth upward. Each stands at the stage its
        // discharge needs over the water below; then a floor too flat to
        // hold that lets it go, a pit keeps its fill, the bed stays wet, and
        // water never stands lower than the water it runs into. The sea,
        // and a lake the river ends in, it meets at their own level. The
        // lake it leaves, and a pool on its way, stand where it holds them
        // until their rims have had their say; then it meets those too, and
        // below one starts no higher than it stands.
        std::vector<float> level (length);
        std::vector<std::uint8_t> met (length);
        const bool joins = reach.downstream_reach != RiverReach::no_id &&
                           !reach_points[reach.downstream_reach].empty ();
        // A reach born at a lake begins in it: the points before its first
        // wholly dry one.
        std::size_t leaving_points = 0;
        if (sheet_drawn (reach.upstream_body))
          while (leaving_points < length &&
                 points[leaving_points].standing_water > 0.0f)
            ++leaving_points;
        const auto holds = [&] (std::size_t point) {
          return sheet_drawn (body[point]) &&
                 (points[point].pooled > 0.25f ||
                  (point < leaving_points &&
                   body[point] == reach.upstream_body));
        };
        const auto sweep = [&] {
          std::ranges::fill (met, 0);
          for (std::size_t point = length; point-- > 0;) {
            const RiverAlignmentPoint& center = points[point];
            const bool sheet = sheet_drawn (body[point]);
            const float stands =
              sheet ? raised[census.domain ().offset (body[point])] : unraised;
            const bool leaving = point < leaving_points;
            float still_level = center.water_level_m + 0.045f;
            if (stands != unraised)
              still_level = std::max (still_level, stands);
            if (stands != unraised
                  ? holds (point) || center.standing_water >= 0.5f
                  : center.standing_water >= 0.5f && !leaving) {
              level[point] = still_level;
              met[point] = 1;
              continue;
            }
            const bool last = point + 1 == length;
            if (last && joins) {
              level[point] =
                reach_points[reach.downstream_reach].front ().level_m;
              continue;
            }
            const Section& section = sections[point];
            float stood = last ? stage (section,
                                        discharge[point],
                                        section.ground[section.thalweg],
                                        0.0f,
                                        parameters)
                               : stage (section,
                                        discharge[point],
                                        level[point + 1],
                                        std::max (0.25f,
                                                  points[point + 1].distance_m -
                                                    center.distance_m),
                                        parameters);
            if (!sheet && !leaving) {
              const Wetted water = wetted (section, stood);
              if (water.open_left)
                stood = std::min (
                  stood,
                  section.ground[section.centre - section.spread] - 0.08f);
              if (water.open_right)
                stood = std::min (
                  stood,
                  section.ground[section.centre + section.spread] - 0.08f);
            }
            if (center.pooled > 0.25f || leaving)
              stood = std::max (stood, center.water_level_m + 0.045f);
            stood = std::max (stood, thread[point]);
            if (!last)
              stood = std::max (stood, level[point + 1]);
            level[point] =
              leaving
                ? stood
                : std::lerp (stood,
                             still_level,
                             smoothstep (0.0f, 1.0f, center.standing_water));
          }
          for (std::size_t point = 1; point < length; ++point)
            if (met[point - 1] && !met[point] &&
                level[point] > level[point - 1]) {
              level[point] = std::max (level[point - 1], bed[point] + 0.035f);
              met[point] = level[point] <= level[point - 1];
            }
        };
        sweep ();
        bool settled = false;
        for (std::size_t point = length; point-- > 0;) {
          if (!holds (point))
            continue;
          float& stands = raised[census.domain ().offset (body[point])];
          if (stands != unraised)
            continue;
          // The river holds the body to its lowest level across it, which
          // is where it leaves: the first of its points from below.
          stands = settle (body[point],
                           level[point],
                           discharge[point],
                           1.5f * static_cast<float> (spread_samples[point]) *
                             section_step);
          settled = true;
        }
        if (settled)
          sweep ();
        // A body whose rim holds it lower than the river's channel would
        // sends the rest of its water another way, and the channel below
        // stands no higher than the body: on down the river until its own
        // profile has fallen under that.
        if (joins && met[length - 1] &&
            level[length - 1] <
              reach_points[reach.downstream_reach].front ().level_m) {
          const float ceiling = level[length - 1];
          bool higher = true;
          for (RiverReachId next = reach.downstream_reach;
               higher && next != RiverReach::no_id;
               next = rivers.reaches[next].downstream_reach) {
            auto& below = reach_points[next];
            const auto& ground = reach_sections[next];
            for (std::size_t point = 0; higher && point < below.size ();
                 ++point) {
              higher = below[point].level_m > ceiling;
              if (higher)
                below[point].level_m = std::max (
                  ceiling,
                  ground[point].ground[ground[point].thalweg] + 0.035f);
            }
          }
        }

        prepared.reserve (length);
        for (std::size_t point = 0; point < length; ++point) {
          const RiverAlignmentPoint& center = points[point];
          const float ground = bed[point];
          const float source_profile =
            fade_source ? smoothstep (0.0f, source_length, center.distance_m)
                        : 1.0f;
          prepared.push_back (
            { .x_m = center.x_m,
              .z_m = center.z_m,
              .level_m = ground + 0.01f +
                         source_profile * (level[point] - ground - 0.01f),
              .half_width_m = 0.0f,
              .depth_m = 0.0f,
              .pace = 0.0f,
              .grade = 0.0f,
              .discharge = discharge[point],
              .waterfall = center.waterfall,
              .source_profile = source_profile,
              .standing_water = center.standing_water });
        }
        // The running surface is a monotone downstream profile. Raise the
        // upstream sample over a shoulder; never lower downstream water into
        // the terrain to manufacture that monotonicity.
        for (std::size_t point = prepared.size (); point > 1; --point)
          prepared[point - 2].level_m =
            std::max (prepared[point - 2].level_m, prepared[point - 1].level_m);
      }
    }

    // A confluence owns one level. Tributaries and the trunk meet at the
    // highest of their independently sampled sections, avoiding a crack
    // without assigning a junction polygon to any reach.
    for (const RiverReach& downstream : rivers.reaches) {
      if (reach_points[downstream.id].empty () ||
          upstream_reaches[downstream.id].empty ())
        continue;
      float junction_level = reach_points[downstream.id].front ().level_m;
      for (RiverReachId upstream : upstream_reaches[downstream.id])
        if (!reach_points[upstream].empty ())
          junction_level =
            std::max (junction_level, reach_points[upstream].back ().level_m);
      reach_points[downstream.id].front ().level_m = junction_level;
      for (RiverReachId upstream : upstream_reaches[downstream.id])
        if (!reach_points[upstream].empty ())
          reach_points[upstream].back ().level_m = junction_level;
    }

    // With the levels final, read what each section holds: how far the
    // water reaches, the pace that passes the discharge through it, and the
    // grade its surface falls at.
    {
      MOPPE_PROFILE_ZONE ("watercourse.read_running_sections");
      const float critical_pace =
        parameters.critical_froude * std::sqrt (gravity);
      for (const RiverReach& reach : rivers.reaches) {
        auto& prepared = reach_points[reach.id];
        const auto& points = reach.alignment.points;
        const auto& sections = reach_sections[reach.id];
        const std::size_t length = prepared.size ();
        for (std::size_t point = 0; point < length; ++point) {
          RunningPoint& running = prepared[point];
          Wetted water = wetted (sections[point], running.level_m);
          // Water with no bank to hold it is not standing there: it is the
          // river only as far as a river spreads.
          if (water.unbanked) {
            Section within = sections[point];
            const int margin = within.centre - within.spread;
            within.ground =
              within.ground.subspan (margin, 2 * within.spread + 1);
            within.centre -= margin;
            within.thalweg -= margin;
            water = wetted (within, running.level_m);
          }
          const float discharge =
            river_discharge (points[point].contributing_area_m2 * u::m * u::m)
              .numerical_value_in (u::m * u::m * u::m / u::s);
          running.half_width_m = std::max (water.left, water.right);
          // In a lake or the sea the river is a current, not a surface: it
          // keeps the width it came with.
          if (points[point].standing_water >= 0.5f)
            running.half_width_m =
              std::min (running.half_width_m,
                        static_cast<float> (sections[point].spread) *
                          sections[point].step);
          // Only a pit may hold water deeper than a running surface is let
          // stand over the ground.
          running.depth_m = points[point].pooled > 0.25f ? water.deepest : 0.0f;
          running.pace =
            water.conveyance > 0.0f
              ? std::min (discharge / water.conveyance, critical_pace)
              : critical_pace;
          const std::size_t above = point >= 2 ? point - 2 : 0;
          const std::size_t below = std::min (point + 2, length - 1);
          const float run = points[below].distance_m - points[above].distance_m;
          running.grade =
            run > 0.25f
              ? std::clamp (
                  (prepared[above].level_m - prepared[below].level_m) / run,
                  0.0f,
                  2.0f)
              : 0.0f;
        }
        // A stamp covers the widest water near it, so a section that opens
        // into a hollow is covered from the narrower ones beside it too.
        std::vector<float> wet (length);
        for (std::size_t point = 0; point < length; ++point)
          wet[point] = prepared[point].half_width_m;
        for (std::size_t point = 0; point < length; ++point) {
          float widest = 0.0f;
          for (std::size_t other = point >= 3 ? point - 3 : 0;
               other < std::min (point + 4, length);
               ++other)
            widest = std::max (widest, wet[other]);
          prepared[point].half_width_m =
            widest * prepared[point].source_profile;
        }
      }
    }

    std::vector<float> running_score (count,
                                      std::numeric_limits<float>::infinity ());
    std::vector<float> running_level (count, 0.0f);
    {
      MOPPE_PROFILE_ZONE ("watercourse.paint_running_surface");
      const float margin = parameters.bank_margin.numerical_value_in (u::m);
      const float depth_limit =
        parameters.depth_limit.numerical_value_in (u::m);
      for (const RiverReach& reach : rivers.reaches) {
        const auto& points = reach_points[reach.id];
        for (std::size_t point = 0; point + 1 < points.size (); ++point) {
          const RunningPoint& start = points[point];
          const RunningPoint& end = points[point + 1];
          const float run_x = end.x_m - start.x_m;
          const float run_z = end.z_m - start.z_m;
          const float run2 = run_x * run_x + run_z * run_z;
          if (run2 < 1e-6f)
            continue;
          const float run = std::sqrt (run2);
          const float direction_x = run_x / run;
          const float direction_z = run_z / run;
          const bool falling = std::max (start.waterfall, end.waterfall) > 0.5f;
          const float radius =
            std::max (start.half_width_m + margin * start.source_profile,
                      end.half_width_m + margin * end.source_profile);
          if (radius <= 0.01f)
            continue;
          const int minimum_x = static_cast<int> (
            std::floor ((std::min (start.x_m, end.x_m) - radius) / spacing_x));
          const int maximum_x = static_cast<int> (
            std::ceil ((std::max (start.x_m, end.x_m) + radius) / spacing_x));
          const int minimum_z = static_cast<int> (
            std::floor ((std::min (start.z_m, end.z_m) - radius) / spacing_z));
          const int maximum_z = static_cast<int> (
            std::ceil ((std::max (start.z_m, end.z_m) + radius) / spacing_z));
          for (int unwrapped_z = minimum_z; unwrapped_z <= maximum_z;
               ++unwrapped_z)
            for (int unwrapped_x = minimum_x; unwrapped_x <= maximum_x;
                 ++unwrapped_x) {
              const float world_x = unwrapped_x * spacing_x;
              const float world_z = unwrapped_z * spacing_z;
              const float t = std::clamp (((world_x - start.x_m) * run_x +
                                           (world_z - start.z_m) * run_z) /
                                            run2,
                                          0.0f,
                                          1.0f);
              const float center_x = std::lerp (start.x_m, end.x_m, t);
              const float center_z = std::lerp (start.z_m, end.z_m, t);
              const float from_x = world_x - center_x;
              const float from_z = world_z - center_z;
              const float distance =
                std::sqrt (from_x * from_x + from_z * from_z);
              const float half_width =
                std::lerp (start.half_width_m, end.half_width_m, t);
              const float profile =
                std::lerp (start.source_profile, end.source_profile, t);
              const float local_radius = half_width + margin * profile;
              if (distance >= local_radius || local_radius <= 1e-4f)
                continue;
              const int x = wrap_index (unwrapped_x, width);
              const int z = wrap_index (unwrapped_z, height);
              const std::size_t cell = static_cast<std::size_t> (z) * width + x;
              const float nearness = 1.0f - distance / local_radius;
              const float weight = nearness * nearness;
              // Where currents meet they mix by the water each brings: a
              // brook does not turn the river it joins.
              const float flowing =
                weight * std::lerp (start.discharge, end.discharge, t);
              flow_x[cell] += flowing * direction_x;
              flow_z[cell] += flowing * direction_z;
              flow_pace[cell] += flowing * std::lerp (start.pace, end.pace, t);
              flow_grade[cell] +=
                flowing * std::lerp (start.grade, end.grade, t);
              flow_fall[cell] +=
                flowing * std::lerp (start.waterfall, end.waterfall, t);
              flow_weight[cell] += flowing;

              const float standing =
                std::lerp (start.standing_water, end.standing_water, t);
              // A height field cannot own the vertical part of a waterfall.
              // Keep its current for the adjoining lip and plunge pool, but
              // leave the falling span to the explicit nickpoint curtain.
              if (falling || standing >= 0.99f)
                continue;
              const float ground = elevation_at (grid, elevations, x, z);
              const float candidate = std::min (
                std::lerp (start.level_m, end.level_m, t),
                ground +
                  std::max (depth_limit,
                            std::lerp (start.depth_m, end.depth_m, t) + 0.5f));
              // The nearest section's level: the stage is level across a
              // section, and each cell lies in the one it is nearest.
              const float score = distance;
              if (candidate > ground + 0.005f && score < running_score[cell]) {
                running_score[cell] = score;
                running_level[cell] = candidate;
              }
            }
        }
      }
    }

    for (std::size_t cell = 0; cell < count; ++cell) {
      if (std::isfinite (running_score[cell])) {
        surface[cell] = std::max (surface[cell], running_level[cell]);
        amplitude[cell] = 0.0f;
      }
    }

    // The current: along the stamps' blended direction at the pace times
    // the root of the depth, quicker past a fall, and descending as fast as
    // its surface's grade takes it.
    std::vector<Vec3> flow (count, Vec3 (0.0f, 0.0f, 0.0f));
    {
      const float fall_speed =
        parameters.waterfall_speed.numerical_value_in (u::m / u::s);
      for (std::size_t cell = 0; cell < count; ++cell) {
        const float depth =
          surface[cell] - surface_elevation_value (elevations[cell]);
        if (!(flow_weight[cell] > 0.0f) || !(depth > 0.005f))
          continue;
        const float share = 1.0f / flow_weight[cell];
        const float speed = flow_pace[cell] * share * std::sqrt (depth) +
                            fall_speed * flow_fall[cell] * share;
        flow[cell] = Vec3 (flow_x[cell] * share * speed,
                           -flow_grade[cell] * share * speed,
                           flow_z[cell] * share * speed);
      }
    }

    // Sign the still-water shoreline. A dry cell beside water stores a
    // neighboring level just below its ground so bilinear consumers find a
    // true sub-cell zero crossing instead of a cell-edge staircase. It
    // stores the LOWEST wet neighbor: on an ordinary shore every neighbor
    // shares one body level, while on a sill between terraced bodies the
    // higher plate then ends at its own waterline instead of extending
    // across the sill and hanging in the air over the lower body.
    {
      constexpr float dry_margin = 1e-6f;
      std::vector<float> signed_surface = surface;
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const std::size_t cell = static_cast<std::size_t> (y) * width + x;
          const float ground = elevation_at (grid, elevations, x, y);
          if (surface[cell] - ground > dry_margin)
            continue;
          float lowest_wet = std::numeric_limits<float>::infinity ();
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              if (dx == 0 && dy == 0)
                continue;
              const int nx = wrap_index (x + dx, width);
              const int ny = wrap_index (y + dy, height);
              const std::size_t neighbor =
                static_cast<std::size_t> (ny) * width + nx;
              if (surface[neighbor] - elevation_at (grid, elevations, nx, ny) >
                  dry_margin)
                lowest_wet = std::min (lowest_wet, surface[neighbor]);
            }
          if (std::isfinite (lowest_wet))
            signed_surface[cell] = std::min (lowest_wet, ground - dry_margin);
        }
      surface = std::move (signed_surface);
    }

    WaterSheets sheets (grid);
    auto& sheet_elevations = spatial::get<surface_elevation> (sheets);
    auto& amplitudes = spatial::get<wave_amplitude> (sheets);
    auto& velocities = spatial::get<water_velocity> (sheets);
    for (std::size_t cell = 0; cell < count; ++cell) {
      sheet_elevations[cell] =
        SurfaceElevation (surface[cell] * surface_elevation[u::m]);
      amplitudes[cell] = amplitude[cell] * wave_amplitude[one];
      velocities[cell] = flow[cell] * water_velocity[u::m / u::s];
    }
    return sheets;
  }
}
