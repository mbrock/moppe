#include <moppe/correct_math.hh>
#include <moppe/terrain/watercourse.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    using discharge_t = cubic_meters_per_second_t;
    using pace_t = root_metre_per_second_t;
    // The integral of depth^(3/2) across a section.
    using conveyance_t = quantity<pow<5, 2> (mp_units::si::metre), float>;

    constexpr meters_t no_length = 0.0f * u::m;
    const SurfaceElevation nowhere_still =
      surface_elevation_point (std::numeric_limits<float>::infinity () * u::m);

    // Halfway between two levels, as 0.5 (a + b): the bisections' middle.
    SurfaceElevation halfway (SurfaceElevation low, SurfaceElevation high) {
      return SurfaceElevation (
        0.5f * (low.delta_from_zero () + high.delta_from_zero ()));
    }

    // The valley across one alignment point: the ground a step apart from
    // left to right, the alignment's own sample at CENTRE and the lowest
    // near it at THALWEG. Running water may spread SPREAD samples either
    // side; the profile runs on beyond for water a pit or a sill holds
    // wider than that.
    struct Section {
      std::span<const SurfaceElevation> ground;
      // The level of the sea or lake each sample lies in; infinite on land.
      std::span<const SurfaceElevation> still;
      meters_t step = 1.0f * u::m;
      int centre = 0;
      int thalweg = 0;
      int spread = 0;
    };

    // The water a level holds in a section: what joins the thalweg without
    // crossing dry ground.
    struct Wetted {
      // How far it reaches either side of the alignment.
      meters_t left = no_length;
      meters_t right = no_length;
      // The integral of depth^(3/2) across it: by Chezy's law the discharge
      // it carries over the root of its slope and the roughness, and at a
      // given Froude number the discharge over that number's pace.
      conveyance_t conveyance = 0.0f * pow<5, 2> (u::m);
      meters_t deepest = no_length;
      // Wet as far as the water may spread.
      bool open_left = false;
      bool open_right = false;
      // Wet as far as the section goes: ground that falls away rather than
      // a bank.
      bool unbanked = false;
      // The level of the still water it runs out into, if it meets any
      // lower than itself; otherwise infinite.
      SurfaceElevation drains = nowhere_still;
    };

    Wetted wetted (const Section& section, SurfaceElevation level) {
      Wetted water;
      const int count = static_cast<int> (section.ground.size ());
      water.deepest = meters_t (level - section.ground[section.thalweg]);
      if (!(water.deepest > no_length))
        return { .deepest = no_length };
      const auto carry = [&] (int sample) {
        const meters_t depth (level - section.ground[sample]);
        water.conveyance += depth * sqrt (depth) * section.step;
      };
      int first = section.thalweg;
      int last = section.thalweg;
      carry (first);
      // Lower still water ends the river's own surface as a bank does.
      while (first > 0 && section.ground[first - 1] < level &&
             !(section.still[first - 1] < level))
        carry (--first);
      while (last + 1 < count && section.ground[last + 1] < level &&
             !(section.still[last + 1] < level))
        carry (++last);
      if (first > 0 && section.still[first - 1] < level)
        water.drains = section.still[first - 1];
      if (last + 1 < count && section.still[last + 1] < level)
        water.drains = std::min (water.drains, section.still[last + 1]);
      water.open_left = first <= section.centre - section.spread;
      water.open_right = last >= section.centre + section.spread;
      water.unbanked = first == 0 || last + 1 == count;
      water.left = std::max (
        no_length,
        (static_cast<float> (section.centre - first) + 0.5f) * section.step);
      water.right = std::max (
        no_length,
        (static_cast<float> (last - section.centre) + 0.5f) * section.step);
      return water;
    }

    // The pace the water runs at when it holds itself to the critical
    // Froude number: Fr sqrt(g).
    pace_t critical_pace (const WatercoursePaint& parameters) {
      return pace_t (parameters.critical_froude * sqrt (standard_gravity));
    }

    // The least stage that passes a discharge through a section: one that
    // carries it at no more than the critical pace, and that stands above
    // the water DOWN, RUN metres below, by the head the bed's friction
    // takes on the way. Both ask more of a higher stage, so they bisect.
    SurfaceElevation stage (const Section& section,
                            discharge_t discharge,
                            SurfaceElevation down,
                            meters_t run,
                            const WatercoursePaint& parameters) {
      const pace_t pace = critical_pace (parameters);
      SurfaceElevation low = std::max (down, section.ground[section.thalweg]);
      if (!(discharge > 0.0f * u::m * u::m * u::m / u::s))
        return low;
      SurfaceElevation high = low + 8.0f * u::m;
      for (int step = 0; step < 16; ++step) {
        const SurfaceElevation middle = halfway (low, high);
        const conveyance_t conveyance = wetted (section, middle).conveyance;
        bool passes = conveyance * pace >= discharge;
        if (passes) {
          // The friction slope that passes it, by Chezy.
          const grade_t slope (discharge / (parameters.roughness * conveyance));
          passes = middle - down >= meters_t (slope * slope * run);
        }
        (passes ? high : low) = middle;
      }
      return high;
    }

    WaveAmplitude wave_factor (WaterBodyClass classification) {
      switch (classification) {
      case WaterBodyClass::Sea:
        return 1.0f * wave_amplitude[one];
      case WaterBodyClass::Lake:
        return 0.10f * wave_amplitude[one];
      case WaterBodyClass::Pond:
        return 0.04f * wave_amplitude[one];
      case WaterBodyClass::Puddle:
        return 0.0f * wave_amplitude[one];
      }
      return 0.0f * wave_amplitude[one];
    }

    WaveAmplitude wave_factor (const FloodField& flood,
                               const LakeCensus& census,
                               std::size_t cell) {
      if (flood.ocean[cell])
        return 1.0f * wave_amplitude[one];
      const WaterBodyId body =
        census.body_at (CellIndex { static_cast<std::uint32_t> (cell) });
      if (!census.domain ().contains (body))
        return 0.0f * wave_amplitude[one];
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
    // The ground at a lattice sample.
    const auto ground_at = [&] (int x, int z) {
      return elevations[grid.offset ({ .column = static_cast<std::size_t> (x),
                                       .row = static_cast<std::size_t> (z) })];
    };

    // Standing bodies establish the first surface. Running alignments are
    // painted into this same field below; the renderer's exact cell clipper
    // turns the signed field into geometry without reach seams or dry-corner
    // flaps.
    const ElevationMap permanent =
      permanent_water_surface (flood, census, parameters.permanence);
    const auto& permanent_elevations =
      spatial::get<surface_elevation> (permanent);
    std::vector<SurfaceElevation> surface (permanent_elevations.begin (),
                                           permanent_elevations.end ());
    std::vector<WaveAmplitude> amplitude (count);
    for (std::size_t cell = 0; cell < count; ++cell)
      amplitude[cell] = wave_factor (flood, census, cell);
    // Where the sea or a sheet-drawn body stands: a river running into one
    // brings it a current, never a level of its own.
    std::vector<std::uint8_t> still (count, 0);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const WaterBodyId body =
        census.body_at (CellIndex { static_cast<std::uint32_t> (cell) });
      still[cell] =
        flood.ocean[cell] || (census.domain ().contains (body) &&
                              water_body_is_permanent (census.water_body (body),
                                                       parameters.permanence));
    }

    // The current's direction, its pace (speed over the root of depth), the
    // grade of the surface it runs down, and the falls it passes, each
    // summed under the stamps' weights: the discharge each stamp brings.
    using paced_discharge_t = decltype (discharge_t {} * pace_t {});
    using graded_discharge_t = decltype (discharge_t {} * grade_t {});
    using falling_discharge_t = decltype (discharge_t {} * proportion_t {});
    constexpr discharge_t no_discharge = 0.0f * u::m * u::m * u::m / u::s;
    std::vector<discharge_t> flow_x (count, no_discharge);
    std::vector<discharge_t> flow_z (count, no_discharge);
    std::vector<paced_discharge_t> flow_pace (count, paced_discharge_t {});
    std::vector<graded_discharge_t> flow_grade (count, graded_discharge_t {});
    std::vector<falling_discharge_t> flow_fall (count, falling_discharge_t {});
    std::vector<discharge_t> flow_weight (count, no_discharge);

    struct RunningPoint {
      plan_position_t at;
      SurfaceElevation level;
      // How far its water reaches to the left and right of the alignment.
      meters_t left;
      meters_t right;
      // The depth of the pit it stands in, if it stands in one.
      meters_t depth;
      pace_t pace;
      grade_t grade;
      discharge_t discharge;
      proportion_t waterfall;
      proportion_t source_profile;
      proportion_t standing_water;
    };

    const meters_t spacing_x = grid.spacing_x ();
    const meters_t spacing_z = grid.spacing_z ();
    const auto sample_ground = [&] (plan_position_t world) {
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
      const auto tx = (gx - x0) * one;
      const auto tz = (gz - z0) * one;
      return lerp (lerp (ground_at (x0, z0), ground_at (x1, z0), tx),
                   lerp (ground_at (x0, z1), ground_at (x1, z1), tx),
                   tz);
    };
    const auto cell_at = [&] (plan_position_t world) {
      const int x =
        wrap_index (static_cast<int> (std::lround (
                      (x_of (world) / spacing_x).numerical_value_in (one))),
                    width);
      const int z =
        wrap_index (static_cast<int> (std::lround (
                      (z_of (world) / spacing_z).numerical_value_in (one))),
                    height);
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
    const SurfaceElevation unraised = nowhere_still;
    std::vector<SurfaceElevation> raised (census.domain ().size (), unraised);
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
    // What a weir a cell wide passes per metre^(3/2) of head over it.
    const auto weir_pace =
      critical_pace (parameters) * std::min (spacing_x, spacing_z);
    using weir_head_t = quantity<pow<3, 2> (mp_units::si::metre), float>;
    // Stands BODY at the level it passes DISCHARGE at, no higher than HELD,
    // the level the river's channel would hold it to alone; paints it and
    // the shore it rises over, and returns the level. The channel is the
    // rim within CHANNEL of the body's spill.
    const auto settle = [&] (WaterBodyId body,
                             SurfaceElevation held,
                             discharge_t discharge,
                             meters_t channel) {
      const WaterBody& record = census.water_body (body);
      const SurfaceElevation spilled =
        surface_elevation_point (record.surface_level);
      const SurfaceElevation least = spilled + 0.045f * u::m;
      if (!(held > least))
        return least;
      struct Reached {
        SurfaceElevation pour;
        std::uint32_t cell;
        bool operator> (const Reached& other) const {
          return pour != other.pour ? pour > other.pour : cell > other.cell;
        }
      };
      std::vector<Reached> rising;
      std::vector<Reached> shore;
      std::vector<SurfaceElevation> weirs;
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
            const SurfaceElevation ground = ground_at (nx, nz);
            // Lower than the body ever stood and yet outside it: the far
            // side of its rim.
            if (ground < spilled - 1e-4f * u::m) {
              weir = true;
              continue;
            }
            const SurfaceElevation pour = std::max (here.pour, ground);
            if (pour >= held)
              continue;
            reached[next] = reaching;
            shore.push_back ({ pour, next });
            rising.push_back ({ pour, next });
            std::ranges::push_heap (rising, std::greater<> ());
          }
        if (weir && record.spill_cell != WaterBody::no_cell) {
          const meters_t away_x = minimum_image_delta (
            static_cast<float> (x -
                                static_cast<int> (record.spill_cell % width)) *
              spacing_x,
            static_cast<float> (width) * spacing_x);
          const meters_t away_z = minimum_image_delta (
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
      const auto passes = [&] (SurfaceElevation level) {
        const auto risen = (level - spilled) / (held - spilled);
        weir_head_t carried = 0.0f * pow<3, 2> (u::m);
        for (const SurfaceElevation crest : weirs)
          if (level > crest) {
            const meters_t over (level - crest);
            carried += over * sqrt (over);
          }
        return discharge * risen * risen + carried * weir_pace >= discharge;
      };
      SurfaceElevation stands = held;
      if (!weirs.empty ()) {
        SurfaceElevation low = spilled;
        for (int step = 0; step < 16; ++step) {
          const SurfaceElevation middle = halfway (low, stands);
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
          still[place.cell] = 1;
        }
      return stands;
    };

    std::vector<std::vector<RunningPoint>> reach_points (
      rivers.reaches.size ());
    // Each reach's cross-sections, kept until its levels are final.
    std::vector<std::vector<SurfaceElevation>> reach_ground (
      rivers.reaches.size ());
    std::vector<std::vector<SurfaceElevation>> reach_still (
      rivers.reaches.size ());
    std::vector<std::vector<Section>> reach_sections (rivers.reaches.size ());
    const meters_t section_step = std::clamp (
      0.4f * std::min (spacing_x, spacing_z), 0.5f * u::m, 1.0f * u::m);
    // Half the five metres a visible river begins at (river.hh).
    const int thread_samples =
      std::max (1,
                static_cast<int> (std::lround (
                  (2.5f * u::m / section_step).numerical_value_in (one))));
    const int thalweg_reach =
      std::max (1,
                static_cast<int> (
                  std::lround ((std::min (spacing_x, spacing_z) / section_step)
                                 .numerical_value_in (one))));
    // The spate's share a river carries, through a catchment.
    const auto carried = [&] (const RiverAlignmentPoint& point) {
      return discharge_t (
        parameters.flow *
        river_discharge (point.contributing_area_m2 * u::m * u::m));
    };
    const auto place_of = [] (const RiverAlignmentPoint& point) {
      return plan_position (point.x_m, point.z_m);
    };
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
        const meters_t source_width =
          river_width (points.front ().contributing_area_m2 * u::m * u::m);
        const meters_t source_length =
          std::max (14.0f * u::m, 3.0f * source_width);

        // The valley across each point: as far as its water may spread, and
        // as far again and more for what a pit or a sill holds beyond that.
        std::vector<std::size_t> first (length + 1, 0);
        std::vector<int> spread_samples (length);
        std::vector<int> reach_samples (length);
        for (std::size_t point = 0; point < length; ++point) {
          const meters_t river =
            river_width (points[point].contributing_area_m2 * u::m * u::m);
          const meters_t spread =
            meters_t (0.5f * parameters.spread * river) + 1.5f * u::m;
          spread_samples[point] = static_cast<int> (
            std::ceil ((spread / section_step).numerical_value_in (one)));
          reach_samples[point] = static_cast<int> (
            std::ceil (((2.0f * spread + 12.0f * u::m) / section_step)
                         .numerical_value_in (one)));
          first[point + 1] = first[point] + 2 * reach_samples[point] + 1;
        }
        auto& ground_samples = reach_ground[reach.id];
        auto& sections = reach_sections[reach.id];
        ground_samples.resize (first[length]);
        auto& still_samples = reach_still[reach.id];
        still_samples.assign (first[length], unraised);
        sections.resize (length);
        std::vector<discharge_t> discharge (length);
        std::vector<SurfaceElevation> bed (length);
        std::vector<SurfaceElevation> thread (length);
        std::vector<WaterBodyId> body (length, no_water_body);
        for (std::size_t point = 0; point < length; ++point) {
          const RiverAlignmentPoint& center = points[point];
          const RiverAlignmentPoint& before = points[point ? point - 1 : point];
          const RiverAlignmentPoint& after =
            points[point + 1 < length ? point + 1 : point];
          const plan_displacement_t run_way =
            place_of (after) - place_of (before);
          const meters_t run = hypot (run_way);
          const plan_direction_t tangent =
            run > 1e-5f * u::m
              ? plan_unit_direction (plan_value (run_way) / meters_value (run))
              : plan_unit_direction (Vec2 (0.0f, 1.0f));
          const plan_direction_t across_way = plan_unit_direction (
            Vec2 (-plan_value (tangent).z (), plan_value (tangent).x ()));
          const int reach_n = reach_samples[point];
          const std::span<SurfaceElevation> across (
            ground_samples.data () + first[point], 2 * reach_n + 1);
          for (int sample = -reach_n; sample <= reach_n; ++sample) {
            const meters_t offset = static_cast<float> (sample) * section_step;
            const plan_position_t world =
              place_of (center) + along (across_way, offset);
            across[sample + reach_n] = sample_ground (world);
            const std::size_t cell = cell_at (world);
            if (still[cell])
              still_samples[first[point] + sample + reach_n] = surface[cell];
          }
          int thalweg = reach_n;
          const int search = std::min (thalweg_reach, spread_samples[point]);
          for (int sample = reach_n - search; sample <= reach_n + search;
               ++sample)
            if (across[sample] < across[thalweg])
              thalweg = sample;
          sections[point] = {
            .ground = across,
            .still = std::span<const SurfaceElevation> (still_samples)
                       .subspan (first[point], 2 * reach_n + 1),
            .step = section_step,
            .centre = reach_n,
            .thalweg = thalweg,
            .spread = spread_samples[point]
          };
          bed[point] = across[thalweg];
          // A river is at least a thread wide enough to see: the stage that
          // wets the visible width about its thalweg, though in a cleft too
          // narrow for that no deeper than a wader's knee.
          thread[point] = std::clamp (
            std::min (
              across[std::max (0, thalweg - thread_samples)],
              across[std::min (2 * reach_n, thalweg + thread_samples)]) -
              0.08f * u::m,
            bed[point] + 0.035f * u::m,
            bed[point] + 0.35f * u::m);
          discharge[point] = carried (center);
          if (center.standing_water > 0.0f || center.pooled > 0.25f)
            body[point] = census.body_at (CellIndex {
              static_cast<std::uint32_t> (cell_at (place_of (center))) });
        }

        // The levels, from the mouth upward. Each stands at the stage its
        // discharge needs over the water below; then a floor too flat to
        // hold that lets it go, a pit keeps its fill, the bed stays wet, and
        // water never stands lower than the water it runs into. The sea,
        // and a lake the river ends in, it meets at their own level. The
        // lake it leaves, and a pool on its way, stand where it holds them
        // until their rims have had their say; then it meets those too, and
        // below one starts no higher than it stands.
        std::vector<SurfaceElevation> level (length);
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
            const SurfaceElevation stands =
              sheet ? raised[census.domain ().offset (body[point])] : unraised;
            const bool leaving = point < leaving_points;
            // The level the alignment found standing water at.
            const SurfaceElevation found =
              surface_elevation_point (center.water_level_m * u::m);
            SurfaceElevation still_level = found + 0.045f * u::m;
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
                reach_points[reach.downstream_reach].front ().level;
              continue;
            }
            const Section& section = sections[point];
            SurfaceElevation stood =
              last ? stage (section,
                            discharge[point],
                            section.ground[section.thalweg],
                            no_length,
                            parameters)
                   : stage (section,
                            discharge[point],
                            level[point + 1],
                            std::max (0.25f * u::m,
                                      points[point + 1].distance_m * u::m -
                                        center.distance_m * u::m),
                            parameters);
            if (!sheet && !leaving) {
              const Wetted water = wetted (section, stood);
              // Water that reaches lower still water across its section
              // has no bank there to stand against: it runs off into it.
              if (water.drains < stood)
                stood = water.drains + 0.045f * u::m;
              if (water.open_left)
                stood =
                  std::min (stood,
                            section.ground[section.centre - section.spread] -
                              0.08f * u::m);
              if (water.open_right)
                stood =
                  std::min (stood,
                            section.ground[section.centre + section.spread] -
                              0.08f * u::m);
            }
            if (center.pooled > 0.25f || leaving)
              stood = std::max (stood, found + 0.045f * u::m);
            stood = std::max (stood, thread[point]);
            if (!last)
              stood = std::max (stood, level[point + 1]);
            level[point] =
              leaving ? stood
                      : lerp (stood,
                              still_level,
                              smoothstep (0.0f, 1.0f, center.standing_water) *
                                blend_weight[one]);
          }
          for (std::size_t point = 1; point < length; ++point)
            if (met[point - 1] && !met[point] &&
                level[point] > level[point - 1]) {
              level[point] =
                std::max (level[point - 1], bed[point] + 0.035f * u::m);
              met[point] = level[point] <= level[point - 1];
            }
        };
        sweep ();
        bool settled = false;
        for (std::size_t point = length; point-- > 0;) {
          if (!holds (point))
            continue;
          SurfaceElevation& stands =
            raised[census.domain ().offset (body[point])];
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
              reach_points[reach.downstream_reach].front ().level) {
          const SurfaceElevation ceiling = level[length - 1];
          bool higher = true;
          for (RiverReachId next = reach.downstream_reach;
               higher && next != RiverReach::no_id;
               next = rivers.reaches[next].downstream_reach) {
            auto& below = reach_points[next];
            const auto& ground = reach_sections[next];
            for (std::size_t point = 0; higher && point < below.size ();
                 ++point) {
              higher = below[point].level > ceiling;
              if (higher)
                below[point].level = std::max (
                  ceiling,
                  ground[point].ground[ground[point].thalweg] + 0.035f * u::m);
            }
          }
        }

        prepared.reserve (length);
        for (std::size_t point = 0; point < length; ++point) {
          const RiverAlignmentPoint& center = points[point];
          const SurfaceElevation ground = bed[point];
          const proportion_t source_profile =
            fade_source
              ? band (0.0f * u::m, source_length, center.distance_m * u::m)
              : 1.0f * proportion[one];
          prepared.push_back (
            { .at = place_of (center),
              .level =
                ground + 0.01f * u::m +
                meters_t (source_profile *
                          meters_t (level[point] - ground - 0.01f * u::m)),
              .left = no_length,
              .right = no_length,
              .depth = no_length,
              .pace = pace_t {},
              .grade = 0.0f * grade[one],
              .discharge = discharge[point],
              .waterfall = center.waterfall * proportion[one],
              .source_profile = source_profile,
              .standing_water = center.standing_water * proportion[one] });
        }
        // The running surface is a monotone downstream profile. Raise the
        // upstream sample over a shoulder; never lower downstream water into
        // the terrain to manufacture that monotonicity.
        for (std::size_t point = prepared.size (); point > 1; --point)
          prepared[point - 2].level =
            std::max (prepared[point - 2].level, prepared[point - 1].level);
      }
    }

    // A confluence owns one level. Tributaries and the trunk meet at the
    // highest of their independently sampled sections, avoiding a crack
    // without assigning a junction polygon to any reach.
    for (const RiverReach& downstream : rivers.reaches) {
      if (reach_points[downstream.id].empty () ||
          upstream_reaches[downstream.id].empty ())
        continue;
      SurfaceElevation junction_level =
        reach_points[downstream.id].front ().level;
      for (RiverReachId upstream : upstream_reaches[downstream.id])
        if (!reach_points[upstream].empty ())
          junction_level =
            std::max (junction_level, reach_points[upstream].back ().level);
      reach_points[downstream.id].front ().level = junction_level;
      for (RiverReachId upstream : upstream_reaches[downstream.id])
        if (!reach_points[upstream].empty ())
          reach_points[upstream].back ().level = junction_level;
    }

    // With the levels final, read what each section holds: how far the
    // water reaches, the pace that passes the discharge through it, and the
    // grade its surface falls at.
    {
      MOPPE_PROFILE_ZONE ("watercourse.read_running_sections");
      const pace_t steepest = critical_pace (parameters);
      for (const RiverReach& reach : rivers.reaches) {
        auto& prepared = reach_points[reach.id];
        const auto& points = reach.alignment.points;
        const auto& sections = reach_sections[reach.id];
        const std::size_t length = prepared.size ();
        for (std::size_t point = 0; point < length; ++point) {
          RunningPoint& running = prepared[point];
          Wetted water = wetted (sections[point], running.level);
          // Water with no bank to hold it is not standing there: it is the
          // river only as far as a river spreads.
          if (water.unbanked) {
            Section within = sections[point];
            const int margin = within.centre - within.spread;
            within.ground =
              within.ground.subspan (margin, 2 * within.spread + 1);
            within.still = within.still.subspan (margin, 2 * within.spread + 1);
            within.centre -= margin;
            within.thalweg -= margin;
            water = wetted (within, running.level);
          }
          const discharge_t discharge = carried (points[point]);
          running.left = water.left;
          running.right = water.right;
          // In a lake or the sea the river is a current, not a surface: it
          // keeps the width it came with.
          if (points[point].standing_water >= 0.5f) {
            const meters_t spread =
              static_cast<float> (sections[point].spread) *
              sections[point].step;
            running.left = std::min (running.left, spread);
            running.right = std::min (running.right, spread);
          }
          // Only a pit may hold water deeper than a running surface is let
          // stand over the ground.
          running.depth =
            points[point].pooled > 0.25f ? water.deepest : no_length;
          running.pace =
            water.conveyance > 0.0f * pow<5, 2> (u::m)
              ? std::min (pace_t (discharge / water.conveyance), steepest)
              : steepest;
          const std::size_t above = point >= 2 ? point - 2 : 0;
          const std::size_t below = std::min (point + 2, length - 1);
          const meters_t run =
            points[below].distance_m * u::m - points[above].distance_m * u::m;
          running.grade = run > 0.25f * u::m
                            ? std::clamp (grade_t ((prepared[above].level -
                                                    prepared[below].level) /
                                                   run),
                                          0.0f * grade[one],
                                          2.0f * grade[one])
                            : 0.0f * grade[one];
        }
        // A stamp covers the widest water near it, so a section that opens
        // into a hollow is covered from the narrower ones beside it too.
        std::vector<std::array<meters_t, 2>> wet (length);
        for (std::size_t point = 0; point < length; ++point)
          wet[point] = { prepared[point].left, prepared[point].right };
        for (std::size_t point = 0; point < length; ++point) {
          std::array<meters_t, 2> widest { no_length, no_length };
          for (std::size_t other = point >= 3 ? point - 3 : 0;
               other < std::min (point + 4, length);
               ++other) {
            widest[0] = std::max (widest[0], wet[other][0]);
            widest[1] = std::max (widest[1], wet[other][1]);
          }
          prepared[point].left =
            meters_t (widest[0] * prepared[point].source_profile);
          prepared[point].right =
            meters_t (widest[1] * prepared[point].source_profile);
        }
      }
    }

    // The running surface at a cell is the stage of the section it lies in:
    // the nearest of each reach that covers it. Where reaches lie alongside,
    // on the inside of a bend or at a confluence, it is a level between
    // theirs, each weighed by its nearness, since taking either's would
    // leave a step.
    const meters_t cell_reach = std::max (spacing_x, spacing_z);
    // The distance over which one reach's say gives way to another's.
    constexpr meters_t blend_reach = 2.5f * u::m;
    // A stamp's say at a distance: the square of its nearness.
    const auto say = [&] (meters_t distance) {
      const blend_weight_t near (
        blend_reach * blend_reach /
        (distance * distance + blend_reach * blend_reach));
      return blend_weight_t (near * near);
    };
    std::vector<blend_weight_t> running_weight (count,
                                                0.0f * blend_weight[one]);
    // The levels summed under their says.
    std::vector<ElevationChange> running_level (count,
                                                0.0f * surface_elevation[u::m]);
    struct Nearest {
      meters_t distance;
      SurfaceElevation level;
      RiverReachId reach = RiverReach::no_id;
    };
    std::vector<Nearest> nearest (count);
    const auto settle_nearest = [&] (std::size_t cell) {
      Nearest& found = nearest[cell];
      if (found.reach == RiverReach::no_id)
        return;
      const blend_weight_t weight = say (found.distance);
      running_level[cell] +=
        ElevationChange (weight * found.level.delta_from_zero ());
      running_weight[cell] += weight;
      found.reach = RiverReach::no_id;
    };
    {
      MOPPE_PROFILE_ZONE ("watercourse.paint_running_surface");
      const meters_t margin = parameters.bank_margin;
      const meters_t depth_limit = parameters.depth_limit;
      for (const RiverReach& reach : rivers.reaches) {
        const auto& points = reach_points[reach.id];
        for (std::size_t point = 0; point + 1 < points.size (); ++point) {
          const RunningPoint& start = points[point];
          const RunningPoint& end = points[point + 1];
          const meters_t start_x = x_of (start.at);
          const meters_t start_z = z_of (start.at);
          const meters_t end_x = x_of (end.at);
          const meters_t end_z = z_of (end.at);
          const meters_t run_x = end_x - start_x;
          const meters_t run_z = end_z - start_z;
          const square_meters_t run2 = run_x * run_x + run_z * run_z;
          if (run2 < 1e-6f * u::m * u::m)
            continue;
          const meters_t run = sqrt (run2);
          const auto direction_x = run_x / run;
          const auto direction_z = run_z / run;
          const bool falling =
            std::max (start.waterfall, end.waterfall) > 0.5f * proportion[one];
          const meters_t radius =
            std::max (std::max (start.left, start.right) +
                        meters_t (margin * start.source_profile),
                      std::max (end.left, end.right) +
                        meters_t (margin * end.source_profile));
          if (radius <= 0.01f * u::m)
            continue;
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
          for (int unwrapped_z = minimum_z; unwrapped_z <= maximum_z;
               ++unwrapped_z)
            for (int unwrapped_x = minimum_x; unwrapped_x <= maximum_x;
                 ++unwrapped_x) {
              const meters_t world_x =
                static_cast<float> (unwrapped_x) * spacing_x;
              const meters_t world_z =
                static_cast<float> (unwrapped_z) * spacing_z;
              // The cell's place in the segment's own frame: how far
              // along it, and how far to its left (negative) or right. A
              // segment owns the strip beside itself and a cell's reach
              // past its ends, where its neighbours' strips part at a bend.
              const meters_t along_run =
                ((world_x - start_x) * run_x + (world_z - start_z) * run_z) /
                run;
              if (along_run < -cell_reach || along_run > run + cell_reach)
                continue;
              const blend_weight_t t =
                std::clamp (
                  (along_run / run).numerical_value_in (one), 0.0f, 1.0f) *
                blend_weight[one];
              const meters_t center_x = lerp (start_x, end_x, t);
              const meters_t center_z = lerp (start_z, end_z, t);
              const meters_t from_x = world_x - center_x;
              const meters_t from_z = world_z - center_z;
              const meters_t side = from_z * direction_x - from_x * direction_z;
              const proportion_t profile =
                lerp (start.source_profile, end.source_profile, t);
              const meters_t reach_m =
                (side < no_length ? lerp (start.left, end.left, t)
                                  : lerp (start.right, end.right, t)) +
                meters_t (margin * profile);
              if (abs (side) >= reach_m)
                continue;
              const meters_t distance =
                sqrt (from_x * from_x + from_z * from_z);
              const int x = wrap_index (unwrapped_x, width);
              const int z = wrap_index (unwrapped_z, height);
              const std::size_t cell = static_cast<std::size_t> (z) * width + x;
              const blend_weight_t weight = say (distance);
              // Where currents meet they mix by the water each brings: a
              // brook does not turn the river it joins.
              const discharge_t flowing (
                weight * lerp (start.discharge, end.discharge, t));
              flow_x[cell] += discharge_t (flowing * direction_x);
              flow_z[cell] += discharge_t (flowing * direction_z);
              flow_pace[cell] += flowing * lerp (start.pace, end.pace, t);
              flow_grade[cell] += flowing * lerp (start.grade, end.grade, t);
              flow_fall[cell] +=
                flowing * lerp (start.waterfall, end.waterfall, t);
              flow_weight[cell] += flowing;

              const proportion_t standing =
                lerp (start.standing_water, end.standing_water, t);
              // A height field cannot own the vertical part of a waterfall.
              // Keep its current for the adjoining lip and plunge pool, but
              // leave the falling span to the explicit nickpoint curtain.
              if (falling || standing >= 0.99f * proportion[one])
                continue;
              const SurfaceElevation ground = ground_at (x, z);
              const SurfaceElevation candidate =
                std::min (lerp (start.level, end.level, t),
                          ground + std::max (depth_limit,
                                             lerp (start.depth, end.depth, t) +
                                               0.5f * u::m));
              Nearest& found = nearest[cell];
              if (found.reach != reach.id) {
                settle_nearest (cell);
                found = { distance, candidate, reach.id };
              } else if (distance < found.distance) {
                found.distance = distance;
                found.level = candidate;
              }
            }
        }
      }
      for (std::size_t cell = 0; cell < count; ++cell)
        settle_nearest (cell);
    }

    constexpr meters_t wet_margin = 0.005f * u::m;
    for (std::size_t cell = 0; cell < count; ++cell) {
      if (!(running_weight[cell] > 0.0f * blend_weight[one]))
        continue;
      // A current through still water calms its swell.
      if (still[cell]) {
        amplitude[cell] = 0.0f * wave_amplitude[one];
        continue;
      }
      const SurfaceElevation stands = surface_elevation_point (
        meters_t (running_level[cell] / running_weight[cell]));
      if (stands > elevations[cell] + wet_margin) {
        surface[cell] = std::max (surface[cell], stands);
        amplitude[cell] = 0.0f * wave_amplitude[one];
      }
    }

    // Beside still water a river's surface draws down to it: within a few
    // cells of a lake or the sea it stands no higher over that level than
    // an easy fall from there allows, so the two meet on a slope, not a
    // step.
    {
      MOPPE_PROFILE_ZONE ("watercourse.draw_down");
      constexpr grade_t fall = 0.12f * grade[one];
      constexpr std::uint8_t reach = 4;
      const meters_t stride = std::min (spacing_x, spacing_z);
      const auto wet = [&] (std::size_t cell) {
        return surface[cell] > elevations[cell] + wet_margin;
      };
      std::vector<std::uint8_t> steps (count, 0);
      std::vector<SurfaceElevation> beside (
        count, surface_elevation_point (0.0f * u::m));
      std::vector<std::uint32_t> front;
      for (std::size_t cell = 0; cell < count; ++cell)
        if (still[cell] && wet (cell)) {
          beside[cell] = surface[cell];
          front.push_back (static_cast<std::uint32_t> (cell));
        }
      for (std::size_t next = 0; next < front.size (); ++next) {
        const std::uint32_t cell = front[next];
        if (steps[cell] == reach)
          continue;
        const int x = static_cast<int> (cell % width);
        const int z = static_cast<int> (cell / width);
        for (int dz = -1; dz <= 1; ++dz)
          for (int dx = -1; dx <= 1; ++dx) {
            const std::size_t near =
              static_cast<std::size_t> (wrap_index (z + dz, height)) * width +
              wrap_index (x + dx, width);
            if (still[near] || steps[near] || !wet (near))
              continue;
            steps[near] = steps[cell] + 1;
            beside[near] = beside[cell];
            front.push_back (static_cast<std::uint32_t> (near));
            // Over ground that stands above the still water it is the
            // depth that eases off, so a cascade keeps its bed wet down
            // to the shore.
            const SurfaceElevation ground = elevations[near];
            surface[near] = std::min (surface[near],
                                      std::max (beside[near], ground) +
                                        meters_t (fall * stride) *
                                          static_cast<float> (steps[near]));
          }
      }
    }

    // The upward part of a velocity.
    const auto rising_of = [] (const velocity_t& velocity) {
      return speed_t (velocity_value (velocity)[1] * (u::m / u::s));
    };
    // A velocity from its three parts.
    const auto velocity_of = [] (speed_t x, speed_t y, speed_t z) {
      return velocity (
        Vec3 (speed_value (x), speed_value (y), speed_value (z)));
    };
    const velocity_t still_water = velocity (Vec3 (0.0f, 0.0f, 0.0f));
    // Whether a velocity moves across the plan at all.
    const auto crosses = [] (const velocity_t& velocity) {
      const Vec3& value = velocity_value (velocity);
      return value[0] != 0.0f || value[2] != 0.0f;
    };

    // The current: along the stamps' blended direction at the pace times
    // the root of the depth, quicker past a fall, and descending as fast as
    // its surface's grade takes it.
    std::vector<velocity_t> flow (count, still_water);
    {
      const speed_t fall_speed = parameters.waterfall_speed;
      for (std::size_t cell = 0; cell < count; ++cell) {
        const meters_t depth (surface[cell] - elevations[cell]);
        if (!(flow_weight[cell] > no_discharge) || !(depth > wet_margin))
          continue;
        const auto share = 1.0f / flow_weight[cell];
        const speed_t speed = speed_t (flow_pace[cell] * share * sqrt (depth)) +
                              speed_t (fall_speed * flow_fall[cell] * share);
        flow[cell] = velocity_of (speed_t (flow_x[cell] * share * speed),
                                  speed_t (-flow_grade[cell] * share * speed),
                                  speed_t (flow_z[cell] * share * speed));
      }
    }

    // Where still water stands a step above the water next to it, over a
    // lake's rim or off a perched pool, it spills: it runs toward the lower
    // water as fast as the drop makes it, and sheds that drop.
    {
      MOPPE_PROFILE_ZONE ("watercourse.spills");
      constexpr meters_t least_drop = 0.12f * u::m;
      const auto wet = [&] (std::size_t cell) {
        return surface[cell] > elevations[cell] + wet_margin;
      };
      std::vector<velocity_t> spills (count, still_water);
      for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x) {
          const std::size_t cell = static_cast<std::size_t> (z) * width + x;
          // Running water already descends its own grade; this is for
          // water with no current to say so.
          if (!wet (cell) || crosses (flow[cell]))
            continue;
          meters_t drop = least_drop;
          int toward_x = 0;
          int toward_z = 0;
          for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
              const std::size_t near =
                static_cast<std::size_t> (wrap_index (z + dz, height)) * width +
                wrap_index (x + dx, width);
              if (near == cell || !wet (near) ||
                  !(surface[cell] - surface[near] > drop))
                continue;
              drop = meters_t (surface[cell] - surface[near]);
              toward_x = dx;
              toward_z = dz;
            }
          if (toward_x == 0 && toward_z == 0)
            continue;
          const meters_t run_x = static_cast<float> (toward_x) * spacing_x;
          const meters_t run_z = static_cast<float> (toward_z) * spacing_z;
          const meters_t run = sqrt (run_x * run_x + run_z * run_z);
          const speed_t speed =
            std::min (speed_t (sqrt (2.0f * standard_gravity * drop)),
                      5.0f * u::m / u::s);
          const velocity_t spill =
            velocity_of (speed_t (speed * run_x / run),
                         speed_t (-speed * std::min (grade_t (drop / run),
                                                     1.0f * grade[one])),
                         speed_t (speed * run_z / run));
          // The lip and the water it lands in both.
          spills[cell] = spill;
          const std::size_t below =
            static_cast<std::size_t> (wrap_index (z + toward_z, height)) *
              width +
            wrap_index (x + toward_x, width);
          const speed_t landing = rising_of (spills[below]);
          const speed_t falling = rising_of (spill);
          if (landing > 0.5f * falling)
            spills[below] = spill * 0.5f;
        }
      for (std::size_t cell = 0; cell < count; ++cell) {
        const velocity_t& spill = spills[cell];
        const speed_t falling = rising_of (spill);
        if (falling >= 0.0f * u::m / u::s)
          continue;
        // A current already there keeps its heading and takes the fall.
        const velocity_t current = flow[cell];
        if (crosses (current)) {
          const Vec3& across = velocity_value (current);
          flow[cell] = velocity_of (across[0] * (u::m / u::s),
                                    std::min (rising_of (current), falling),
                                    across[2] * (u::m / u::s));
        } else
          flow[cell] = spill;
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
      constexpr meters_t dry_margin = 1e-6f * u::m;
      std::vector<SurfaceElevation> signed_surface = surface;
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const std::size_t cell = static_cast<std::size_t> (y) * width + x;
          const SurfaceElevation ground = ground_at (x, y);
          if (surface[cell] - ground > dry_margin)
            continue;
          SurfaceElevation lowest_wet = nowhere_still;
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              if (dx == 0 && dy == 0)
                continue;
              const int nx = wrap_index (x + dx, width);
              const int ny = wrap_index (y + dy, height);
              const std::size_t neighbor =
                static_cast<std::size_t> (ny) * width + nx;
              if (surface[neighbor] - ground_at (nx, ny) > dry_margin)
                lowest_wet = std::min (lowest_wet, surface[neighbor]);
            }
          if (std::isfinite (surface_elevation_value (lowest_wet)))
            signed_surface[cell] = std::min (lowest_wet, ground - dry_margin);
        }
      // Running water that steps diagonally from one wet cell to the next
      // passes between two dry ones, and read from their levels below their
      // banks the bilinear field pinches shut at the corner: a stream no
      // wider than a cell would be a string of pools. There both banks
      // stand their water just under their own ground instead, and the
      // stream crosses the corner as a thin sheet.
      const auto wet = [&] (std::size_t cell) {
        return surface[cell] - elevations[cell] > dry_margin;
      };
      const auto running = [&] (std::size_t cell) {
        return wet (cell) && flow_weight[cell] > no_discharge && !still[cell];
      };
      for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x) {
          const int right = wrap_index (x + 1, width);
          const int below = wrap_index (z + 1, height);
          const std::size_t corner[4] = {
            static_cast<std::size_t> (z) * width + x,
            static_cast<std::size_t> (z) * width + right,
            static_cast<std::size_t> (below) * width + right,
            static_cast<std::size_t> (below) * width + x
          };
          for (int diagonal = 0; diagonal < 2; ++diagonal) {
            const std::size_t a = corner[diagonal];
            const std::size_t b = corner[diagonal + 2];
            const std::size_t c = corner[diagonal + 1];
            const std::size_t d = corner[(diagonal + 3) % 4];
            if (!running (a) || !running (b) || wet (c) || wet (d))
              continue;
            for (const std::size_t bank : { c, d })
              signed_surface[bank] =
                std::max (signed_surface[bank], elevations[bank] - dry_margin);
          }
        }
      surface = std::move (signed_surface);
    }

    WaterSheets sheets (grid);
    auto& sheet_elevations = spatial::get<surface_elevation> (sheets);
    auto& amplitudes = spatial::get<wave_amplitude> (sheets);
    auto& velocities = spatial::get<water_velocity> (sheets);
    for (std::size_t cell = 0; cell < count; ++cell) {
      sheet_elevations[cell] = surface[cell];
      amplitudes[cell] = amplitude[cell];
      velocities[cell] =
        velocity_value (flow[cell]) * water_velocity[u::m / u::s];
    }
    return sheets;
  }
}
