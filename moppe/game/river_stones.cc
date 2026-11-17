#include <moppe/correct_math.hh>
#include <moppe/game/river_stones.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

namespace moppe::game {
  namespace {
    using shields_number_t = quantity<shields_number[one], float>;
    using relative_density_t = quantity<isq::relative_mass_density[one], float>;
    // How many stones lie on a square metre.
    constexpr auto per_square_metre = one / (u::m * u::m);
    using stone_density_t = quantity<per_square_metre, float>;

    // Rock is 2.65 times as dense as water.
    constexpr relative_density_t river_submerged_density =
      1.65f * isq::relative_mass_density[one];
    // The river is walked a metre at a time.
    constexpr meters_t river_step = 1.0f * u::m;
    constexpr radians_t river_tau = 6.2831853f * u::rad;
    // The tread a rider follows, as the boulder planner keeps it clear.
    constexpr terrain::TrailInfluence river_trail_tread =
      0.75f * map::trail_influence[one];
    // The top of a settled stone stands this many radii above the ground it
    // settled on: the body's height less its burial.
    constexpr scale_factor_t river_settled_top =
      (2.0f * boulder_squash - boulder_burial).numerical_value_in (one) *
      scale_factor[one];
    // A stone in water broad enough to stand on is solid; so is every stone
    // the boulder planner would make so.
    constexpr meters_t river_foothold_radius = 0.3f * u::m;

    // Stepping stones: a stride apart, centre to centre, so a walker's
    // feet find one stone before they leave the last, with their tops a
    // hand's breadth or two clear of the water.
    constexpr meters_t stepping_pitch = 0.70f * u::m;
    constexpr meters_t stepping_least_radius = 0.40f * u::m;
    constexpr meters_t stepping_greatest_radius = 0.75f * u::m;
    constexpr terrain::ElevationChange stepping_deepest =
      0.55f * terrain::surface_elevation[u::m];
    constexpr meters_t stepping_widest = 14.0f * u::m;
    constexpr meters_t stepping_spacing = 250.0f * u::m; // apart, at least

    // The sheet holds the water of an ordinary day, a share of the spate
    // that shaped the bed and sorted its stones (WatercoursePaint::flow).
    // A flow's depth goes as its discharge to the two thirds, in a wide
    // channel as over a sill, so the spate stood this many times deeper.
    scale_factor_t spate_depth_ratio () {
      return cr::pow (
               terrain::WatercoursePaint {}.flow.numerical_value_in (one),
               -2.0f / 3.0f) *
             scale_factor[one];
    }

    position_t river_probe (const plan_position_t& at) {
      return at_height (at, 0.0f * u::m);
    }

    // How far the current turns from one heading to another: the sine of
    // the angle between them, positive turning toward +normal.
    magnitude_t river_cross (const plan_direction_t& a,
                             const plan_direction_t& b) {
      return (plan_value (a).x () * plan_value (b).z () -
              plan_value (a).z () * plan_value (b).x ()) *
             one;
    }

    // Roughly normal, from three independent lanes: mean 0, deviation 1.
    noise_signal_t river_gaussian (std::uint32_t identity, std::uint32_t lane) {
      return 2.0f *
             (hash_lane (identity, lane) + hash_lane (identity, lane + 1) +
              hash_lane (identity, lane + 2) - 1.5f) *
             noise_signal[one];
    }

    // How fast the water runs over the plan, and how fast it descends.
    speed_t plan_speed (const terrain::WaterVelocity& current) {
      return cr::hypot (current.numerical_value_ref_in (u::m / u::s)[0],
                        current.numerical_value_ref_in (u::m / u::s)[2]) *
             (u::m / u::s);
    }
    speed_t descent (const terrain::WaterVelocity& current) {
      return -current.numerical_value_ref_in (u::m / u::s)[1] * (u::m / u::s);
    }

    // The ground and the water over it, read at any place.
    class RiverGround {
    public:
      RiverGround (const map::SurfaceGeometry& surface,
                   const map::SurfaceReadings& readings,
                   const terrain::WaterSheets& water)
          : m_surface (surface), m_readings (readings), m_water (water),
            m_period (plan_extent (surface.domain ().period_x (),
                                   surface.domain ().period_z ())) {}

      terrain::SurfaceElevation ground (const plan_position_t& at) const {
        return spatial::sample<terrain::surface_elevation> (m_surface,
                                                            river_probe (at));
      }
      terrain::SurfaceElevation level (const plan_position_t& at) const {
        return spatial::sample<terrain::surface_elevation> (m_water,
                                                            river_probe (at));
      }
      terrain::ElevationChange depth (const plan_position_t& at) const {
        return level (at) - ground (at);
      }
      speed_t speed (const plan_position_t& at) const {
        return plan_speed (
          spatial::sample<terrain::water_velocity> (m_water, river_probe (at)));
      }
      terrain::TerrainNormal normal (const plan_position_t& at) const {
        return spatial::sample<terrain::terrain_normal> (m_surface,
                                                         river_probe (at));
      }
      terrain::TrailInfluence trail (const plan_position_t& at) const {
        return spatial::sample<map::trail_influence> (m_readings,
                                                      river_probe (at));
      }
      terrain::HomeBaseInfluence home (const plan_position_t& at) const {
        return spatial::sample<map::home_base_influence> (m_readings,
                                                          river_probe (at));
      }
      plan_position_t wrap (const plan_position_t& at) const {
        return terrain::wrap (at, m_period);
      }
      // The shortest way between two places on the torus.
      plan_displacement_t apart (const plan_displacement_t& d) const {
        return terrain::minimum_image (d, m_period);
      }

    private:
      const map::SurfaceGeometry& m_surface;
      const map::SurfaceReadings& m_readings;
      const terrain::WaterSheets& m_water;
      plan_extent_t m_period;
    };

    // A metre of river: where its centreline runs and what the water there
    // is doing, read across the whole wetted section.
    struct RiverSection {
      // The centreline, unwrapped as the alignment is.
      plan_position_t centreline {};
      plan_direction_t downstream = plan_unit_direction (Vec2 (1.0f, 0.0f));
      meters_t regime_width = 0.0f * u::m;
      terrain::SurfaceElevation level {};
      terrain::ElevationChange depth {};    // at the centreline
      terrain::ElevationChange deepest {};  // anywhere across
      speed_t speed = 0.0f * (u::m / u::s); // of the current at the centreline
      // How far the water reaches along the normal, each way.
      meters_t bank[2] = { 0.0f * u::m, 0.0f * u::m };
      bool bounded = false; // dry ground found on both sides
      bool running = false;
      bool falling = false; // over a waterfall
      // Over the reach around it.
      grade_t slope = 0.0f * grade[one];
      meters_t competence = 0.0f * u::m;
      per_metre_t curvature = 0.0f / u::m; // positive turning toward +normal
      // Nothing else is placed where stepping stones cross.
      bool crossing = false;

      // Across the stream: downstream turned a quarter toward +z from +x.
      plan_direction_t normal () const {
        return plan_unit_direction (
          Vec2 (-plan_value (downstream).z (), plan_value (downstream).x ()));
      }
      meters_t wet_width () const {
        return bank[0] + bank[1];
      }
      // The place `across` metres along the normal and `forward` downstream.
      plan_position_t at (meters_t across, meters_t forward) const {
        return centreline + moppe::along (normal (), across) +
               moppe::along (downstream, forward);
      }
    };

    float river_mix (float a, float b, blend_weight_t t) {
      return a + (b - a) * t.numerical_value_in (one);
    }

    // Walks a reach's alignment a metre at a time and reads each section.
    std::vector<RiverSection> survey_reach (const terrain::RiverReach& reach,
                                            const RiverGround& ground) {
      const auto& points = reach.alignment.points;
      std::vector<RiverSection> sections;
      if (points.size () < 2)
        return sections;
      std::vector<meters_t> along (points.size (), 0.0f * u::m);
      for (std::size_t i = 1; i < points.size (); ++i)
        along[i] =
          along[i - 1] + cr::hypot (points[i].x_m - points[i - 1].x_m,
                                    points[i].z_m - points[i - 1].z_m) *
                           u::m;
      const meters_t length = along.back ();
      if (length < 4.0f * river_step)
        return sections;
      const std::size_t count =
        static_cast<std::size_t> (
          std::floor ((length / river_step).numerical_value_in (one))) +
        1;
      sections.resize (count);
      std::vector<square_meters_t> area (count);
      // Running on its own, not through a pool or into standing water.
      std::vector<bool> unpooled (count);
      std::size_t segment = 0;
      for (std::size_t k = 0; k < count; ++k) {
        const meters_t s =
          std::min (static_cast<float> (k) * river_step, length);
        while (segment + 2 < points.size () && along[segment + 1] < s)
          ++segment;
        const auto& a = points[segment];
        const auto& b = points[segment + 1];
        const meters_t span = along[segment + 1] - along[segment];
        const blend_weight_t t =
          span > 0.0f * u::m
            ? std::clamp (
                ((s - along[segment]) / span).numerical_value_in (one),
                0.0f,
                1.0f) *
                blend_weight[one]
            : 0.0f * blend_weight[one];
        RiverSection& section = sections[k];
        section.centreline = plan_position (river_mix (a.x_m, b.x_m, t),
                                            river_mix (a.z_m, b.z_m, t));
        area[k] =
          river_mix (a.contributing_area_m2, b.contributing_area_m2, t) * u::m *
          u::m;
        unpooled[k] =
          std::max (
            { a.pooled, b.pooled, a.standing_water, b.standing_water }) < 0.5f;
        section.falling = std::max (a.waterfall, b.waterfall) > 0.0f;
      }

      for (std::size_t k = 0; k < count; ++k) {
        RiverSection& section = sections[k];
        section.regime_width = terrain::river_width (area[k]);
        // The tangent over about half the channel's width, so the meanders
        // the alignment draws show and the cells it steps between do not.
        const auto reach_back = static_cast<std::ptrdiff_t> (
          (std::clamp (0.5f * section.regime_width, 1.5f * u::m, 8.0f * u::m) /
           river_step)
            .numerical_value_in (one));
        const std::size_t before =
          static_cast<std::size_t> (std::max<std::ptrdiff_t> (
            0, static_cast<std::ptrdiff_t> (k) - reach_back));
        const std::size_t after = std::min (count - 1, k + reach_back);
        const plan_displacement_t chord =
          sections[after].centreline - sections[before].centreline;
        const meters_t run = hypot (chord);
        if (run > 1e-4f * u::m)
          section.downstream =
            plan_unit_direction (plan_value (chord) / meters_value (run));
        section.level = ground.level (section.centreline);
        section.depth = section.level - ground.ground (section.centreline);
        section.speed = ground.speed (section.centreline);
        section.running = section.depth > 0.02f * u::m &&
                          section.speed > 0.03f * (u::m / u::s) && unpooled[k];
        if (!(section.depth > 0.0f * u::m))
          continue;
        // Out each way across the section until the ground stands dry.
        const meters_t limit = 1.6f * section.regime_width + 6.0f * u::m;
        section.bounded = true;
        section.deepest = section.depth;
        for (int side = 0; side < 2; ++side) {
          meters_t reached = limit;
          terrain::ElevationChange wet = section.depth;
          for (meters_t across = 0.5f * u::m; across <= limit;
               across += 0.5f * u::m) {
            const terrain::ElevationChange d = ground.depth (
              section.at (side == 0 ? across : -across, 0.0f * u::m));
            if (!(d > 0.0f * u::m)) {
              // The waterline lies between the last wet probe and this one.
              reached =
                across -
                0.5f * u::m * d /
                  std::min (d - wet, -1e-4f * terrain::surface_elevation[u::m]);
              break;
            }
            wet = d;
            section.deepest = std::max (section.deepest, d);
          }
          if (reached >= limit)
            section.bounded = false;
          section.bank[side] = std::max (0.0f * u::m, reached);
        }
      }

      // The grade the water surface falls over a few widths, the depth that
      // drives it, and how sharply the river turns there.
      for (std::size_t k = 0; k < count; ++k) {
        RiverSection& section = sections[k];
        const auto window = static_cast<std::ptrdiff_t> (
          (std::clamp (1.5f * section.regime_width, 3.0f * u::m, 30.0f * u::m) /
           river_step)
            .numerical_value_in (one));
        const auto first = static_cast<std::size_t> (std::max<std::ptrdiff_t> (
          0, static_cast<std::ptrdiff_t> (k) - window));
        const std::size_t last = std::min (count - 1, k + window);
        if (last > first)
          section.slope =
            std::max (0.0f * grade[one],
                      ((sections[first].level - sections[last].level) /
                       (static_cast<float> (last - first) * river_step))
                          .numerical_value_in (one) *
                        grade[one]);
        terrain::ElevationChange depth {};
        int wet = 0;
        for (std::size_t i = first; i <= last; ++i)
          if (sections[i].running) {
            depth += sections[i].depth;
            ++wet;
          }
        // A section's mean depth is about two thirds of its deepest.
        if (section.running && wet)
          section.competence = bed_competence (
            quantity_cast<isq::length> (0.67f * spate_depth_ratio () * depth /
                                        static_cast<float> (wet)),
            section.slope);
        const auto turn = static_cast<std::ptrdiff_t> (
          (std::clamp (section.regime_width, 2.0f * u::m, 20.0f * u::m) /
           river_step)
            .numerical_value_in (one));
        const std::size_t back = static_cast<std::size_t> (
          std::max<std::ptrdiff_t> (0, static_cast<std::ptrdiff_t> (k) - turn));
        const std::size_t ahead = std::min (count - 1, k + turn);
        if (ahead > back)
          section.curvature = river_cross (sections[back].downstream,
                                           sections[ahead].downstream) /
                              (static_cast<float> (ahead - back) * river_step);
      }
      return sections;
    }

    // Lays stones for one reach, in a stable order.
    class StoneLayer {
    public:
      StoneLayer (const RiverGround& ground, RiverStones& stones)
          : m_ground (ground), m_sites (stones.sites),
            m_crossings (stones.crossings) {}

      // Settles a stone of `radius` at a place as the boulder planner does:
      // halfway between the lowest ground under its rim and the mean. False
      // where it would enter the trail's tread or the home base.
      bool settle (const plan_position_t& at,
                   meters_t radius,
                   radians_t turn,
                   terrain::SurfaceElevation& settled) const {
        if (m_ground.home (at) > 0.02f * map::home_base_influence[one])
          return false;
        const terrain::SurfaceElevation centre = m_ground.ground (at);
        terrain::SurfaceElevation lowest = centre;
        terrain::ElevationChange sum = centre.delta_from_zero ();
        for (int k = 0; k < 4; ++k) {
          const radians_t angle =
            turn + 0.25f * river_tau * static_cast<float> (k);
          const plan_position_t rim =
            at + plan_displacement (0.85f * radius * moppe::cos (angle),
                                    0.85f * radius * moppe::sin (angle));
          if (m_ground.trail (rim) > river_trail_tread)
            return false;
          const terrain::SurfaceElevation height = m_ground.ground (rim);
          lowest = std::min (lowest, height);
          sum += height.delta_from_zero ();
        }
        settled = terrain::SurfaceElevation (
          0.5f * (lowest.delta_from_zero () + 0.2f * sum));
        return true;
      }

      // A loose stone where it falls: kept only if it shows, its top at or
      // just under the surface. `least_top` is how far above the water its
      // top must stand.
      void loose (const plan_position_t& at,
                  meters_t radius,
                  std::uint32_t identity,
                  meters_t least_top = -0.04f * u::m) {
        terrain::SurfaceElevation settled {};
        if (!settle (at, radius, river_tau * hash_lane (identity, 3), settled))
          return;
        const terrain::SurfaceElevation level = m_ground.level (at);
        const meters_t stands =
          quantity_cast<isq::length> (river_settled_top * radius);
        const terrain::SurfaceElevation top = settled + stands;
        const bool wet = level > settled;
        if (wet && top < level + least_top)
          return;
        emit (
          at,
          settled +
            (boulder_squash - boulder_burial).numerical_value_in (one) * radius,
          radius,
          identity,
          wet && radius >= river_foothold_radius && top > level + 0.04f * u::m,
          wet);
      }

      void emit (const plan_position_t& at,
                 terrain::SurfaceElevation centre,
                 meters_t radius,
                 std::uint32_t identity,
                 bool solid,
                 bool wet) {
        m_sites.push_back ({
          .centre = terrain::at_elevation (m_ground.wrap (at), centre),
          .normal = m_ground.normal (at),
          .radius = radius,
          .moisture = (wet ? 1.0f : 0.75f) * map::surface_moisture[one],
          .seed = identity,
          .solid = solid,
        });
      }

      void cross (const plan_position_t& from, const plan_position_t& to) {
        const auto bank = [&] (const plan_position_t& at) {
          return terrain::at_elevation (m_ground.wrap (at),
                                        m_ground.ground (at));
        };
        m_crossings.push_back ({ .from = bank (from), .to = bank (to) });
      }

      const RiverGround& ground () const {
        return m_ground;
      }
      std::size_t size () const {
        return m_sites.size ();
      }
      void truncate (std::size_t size) {
        m_sites.resize (size);
      }

    private:
      const RiverGround& m_ground;
      std::vector<BoulderSite>& m_sites;
      std::vector<StoneCrossing>& m_crossings;
    };

    // A row of stepping stones across one section, bank to bank; false,
    // and nothing laid, where the water is too deep for one somewhere or
    // the row would cross the trail's tread.
    bool lay_stepping_stones (StoneLayer& layer,
                              const RiverSection& section,
                              std::uint32_t identity) {
      const RiverGround& ground = layer.ground ();
      const std::size_t start = layer.size ();
      const meters_t from = -(section.bank[1] + 0.3f * u::m);
      const meters_t to = section.bank[0] + 0.3f * u::m;
      int stones = 0;
      std::uint32_t k = 0;
      for (meters_t across = from; across <= to; ++k) {
        const std::uint32_t stone = lattice_hash (identity, k, 0x57e9u);
        const meters_t along =
          0.08f * u::m * (2.0f * hash_lane (stone, 0) - 1.0f);
        const plan_position_t at = section.at (across, along);
        across +=
          stepping_pitch + 0.04f * u::m * (2.0f * hash_lane (stone, 1) - 1.0f);
        // Wherever the feet would be wet between this stone's neighbours;
        // where they would be dry the walker steps on the ground itself.
        terrain::ElevationChange wettest = -1.0f * u::m;
        // In pitches across the stream, either side of the stone.
        for (float reach = -0.5f; reach <= 0.5f; reach += 0.25f)
          wettest = std::max (
            wettest,
            ground.depth (
              at + plan_displacement (plan_value (section.normal ()) * reach *
                                      meters_value (stepping_pitch))));
        if (!(wettest > 0.02f * u::m))
          continue;
        const terrain::SurfaceElevation level =
          std::max (ground.level (at), ground.ground (at));
        // The bed under the stone's footprint, at its lowest.
        terrain::SurfaceElevation bed = ground.ground (at);
        for (int rim = 0; rim < 4; ++rim) {
          const radians_t angle = 0.25f * river_tau * static_cast<float> (rim);
          bed =
            std::min (bed,
                      ground.ground (at + plan_displacement (
                                            0.4f * u::m * moppe::cos (angle),
                                            0.4f * u::m * moppe::sin (angle))));
        }
        const terrain::SurfaceElevation top =
          level + 0.09f * u::m + 0.09f * u::m * hash_lane (stone, 2);
        // Bedded a few centimetres below the lowest of the bed under it, so
        // it stands on the bottom rather than floating over a hollow.
        const meters_t radius = std::max (
          stepping_least_radius + 0.06f * u::m * hash_lane (stone, 3),
          meters_t ((top - bed + 0.06f * u::m) /
                    (2.0f * boulder_squash.numerical_value_in (one))));
        terrain::SurfaceElevation settled {};
        if (radius > stepping_greatest_radius ||
            !layer.settle (at, radius, 0.0f * u::rad, settled)) {
          layer.truncate (start);
          return false;
        }
        layer.emit (at,
                    top - boulder_squash.numerical_value_in (one) * radius,
                    radius,
                    stone,
                    true,
                    true);
        ++stones;
      }
      if (stones == 0)
        return false;
      layer.cross (section.at (from - 0.5f * u::m, 0.0f * u::m),
                   section.at (to + 0.5f * u::m, 0.0f * u::m));
      return true;
    }

    // A place a reach could be crossed on stones, and how good a one.
    struct CrossingSite {
      scale_factor_t score = 0.0f * scale_factor[one];
      std::size_t reach = 0;
      std::size_t index = 0;
    };

    // Where along a reach it could be crossed on stones: shallow, narrow,
    // not a cascade, with dry ground on both banks to step off onto; better
    // on a riffle, where real ones are set, and best near a trail.
    void find_crossings (const RiverGround& ground,
                         const std::vector<RiverSection>& sections,
                         std::size_t reach,
                         std::uint32_t reach_identity,
                         std::vector<CrossingSite>& sites) {
      for (std::size_t k = 0; k < sections.size (); ++k) {
        const RiverSection& section = sections[k];
        if (!section.running || !section.bounded || section.falling ||
            section.deepest > stepping_deepest ||
            section.speed > 2.4f * (u::m / u::s) ||
            section.slope > 0.06f * grade[one] ||
            section.wet_width () < 1.5f * u::m ||
            section.wet_width () > stepping_widest)
          continue;
        // Both banks a step's height or less above the water.
        bool banks = true;
        for (int side = 0; side < 2 && banks; ++side) {
          const meters_t out = section.bank[side] + 0.8f * u::m;
          const terrain::SurfaceElevation bank =
            ground.ground (section.at (side == 0 ? out : -out, 0.0f * u::m));
          banks = bank < section.level + 0.4f * u::m;
        }
        if (!banks)
          continue;
        terrain::TrailInfluence trail = 0.0f * map::trail_influence[one];
        for (meters_t along = -12.0f * u::m; along <= 12.0f * u::m;
             along += 3.0f * u::m)
          trail =
            std::max (trail, ground.trail (section.at (0.0f * u::m, along)));
        const std::uint32_t identity = lattice_hash (
          reach_identity, static_cast<std::uint32_t> (k), 0xc705u);
        const proportion_t riffle =
          band (0.02f * u::m, 0.20f * u::m, section.competence);
        const proportion_t trodden = band (0.15f * map::trail_influence[one],
                                           0.55f * map::trail_influence[one],
                                           trail);
        sites.push_back (
          { .score = (0.6f * riffle + 1.2f * trodden +
                      0.4f * hash_lane (identity, 0) * proportion[one])
                       .numerical_value_in (one) *
                     scale_factor[one],
            .reach = reach,
            .index = k });
      }
    }

    // Steps of keystones jammed across a steep, competent bed, each pool
    // below one stepping down about the step's own height: their spacing is
    // about one and a half times the step's height over the grade
    // (Abrahams, Li and Atkinson 1995). The keystones are the coarsest
    // stones there, which the flood that moves the rest leaves standing. A
    // step stands on a sill the water's own profile shows, the shallowest
    // section for a third of a spacing either way; where the water runs
    // without sills, at the spacing.
    void lay_steps (StoneLayer& layer,
                    const std::vector<RiverSection>& sections,
                    std::uint32_t reach_identity) {
      meters_t last = -1e9f * u::m;
      for (std::size_t k = 0; k < sections.size (); ++k) {
        const RiverSection& section = sections[k];
        if (!section.running || section.slope < 0.035f * grade[one] ||
            section.competence < 0.1f * u::m || section.crossing)
          continue;
        const meters_t s = static_cast<float> (k) * river_step;
        const meters_t keystone =
          std::clamp (2.5f * section.competence, 0.5f * u::m, 1.6f * u::m);
        const meters_t spacing =
          std::clamp (meters_t (quantity_cast<isq::length> (1.5f * keystone /
                                                            section.slope)),
                      3.0f * u::m + section.wet_width (),
                      40.0f * u::m);
        if (s - last < 0.5f * spacing)
          continue;
        const auto reach = std::max<std::size_t> (
          2,
          static_cast<std::size_t> (
            (spacing / (3.0f * river_step)).numerical_value_in (one)));
        bool sill = true;
        for (std::size_t j = k > reach ? k - reach : 0;
             sill && j <= std::min (sections.size () - 1, k + reach);
             ++j)
          sill = !(sections[j].running && sections[j].depth < section.depth);
        if (!sill && s - last < spacing)
          continue;
        last = s;
        const std::uint32_t row = lattice_hash (
          reach_identity, static_cast<std::uint32_t> (k), 0x57e95u);
        meters_t across = -(section.bank[1] + 0.2f * u::m);
        meters_t previous = 0.0f * u::m;
        for (std::uint32_t j = 0; across <= section.bank[0] + 0.2f * u::m;
             ++j) {
          const std::uint32_t stone = lattice_hash (row, j, 0x6e75u);
          const meters_t radius =
            0.5f * keystone * (0.75f + 0.5f * hash_lane (stone, 0));
          across += 0.9f * (previous + radius);
          previous = radius;
          const meters_t along =
            meters_t (0.2f * radius * river_gaussian (stone, 1));
          layer.loose (
            section.at (across, along), radius, stone, -0.08f * u::m);
        }
      }
    }

    // The bed's own stones where they show: coarse in competent water, with
    // a tail of larger ones the flood leaves behind as lag.
    void lay_bed (StoneLayer& layer,
                  const std::vector<RiverSection>& sections,
                  std::uint32_t reach_identity) {
      for (std::size_t k = 0; k < sections.size (); ++k) {
        const RiverSection& section = sections[k];
        if (!section.running || section.crossing ||
            section.competence < 0.04f * u::m)
          continue;
        const meters_t span = section.wet_width () + 2.0f * u::m;
        const stone_density_t density =
          (0.05f * proportion[one] +
           0.25f * band (0.04f * u::m, 0.4f * u::m, section.competence))
            .numerical_value_in (one) *
          per_square_metre;
        const std::uint32_t metre =
          lattice_hash (reach_identity, static_cast<std::uint32_t> (k), 0xbedu);
        const int count = static_cast<int> (
          std::min (60.0f,
                    (density * span * river_step).numerical_value_in (one) +
                      hash_lane (metre, 0)));
        for (int j = 0; j < count; ++j) {
          const std::uint32_t stone =
            lattice_hash (metre, static_cast<std::uint32_t> (j), 0xc0bu);
          const meters_t across =
            -(section.bank[1] + 1.0f * u::m) + span * hash_lane (stone, 0);
          const meters_t along = river_step * (hash_lane (stone, 1) - 0.5f);
          meters_t radius =
            0.5f * section.competence *
            cr::exp (
              (0.6f * river_gaussian (stone, 4)).numerical_value_in (one));
          // Now and then a lag stone, two to four times the rest.
          const probability_t lag = hash_lane (stone, 2) * probability[one];
          if (lag < 0.15f * probability[one])
            radius *= 2.0f + 2.0f * hash_lane (stone, 3);
          layer.loose (section.at (across, along),
                       std::clamp (radius, 0.06f * u::m, 1.2f * u::m),
                       stone);
        }
      }
    }

    // A strip of cobbles and gravel along one bank: from a little way out
    // into the shallows onto the low ground beside it, `reach` metres past
    // the water's edge.
    void lay_bar (StoneLayer& layer,
                  const RiverSection& section,
                  int side,
                  stone_density_t strength,
                  meters_t grain,
                  meters_t reach,
                  std::uint32_t identity) {
      const RiverGround& ground = layer.ground ();
      const meters_t inner = section.bank[side] - 0.3f * section.wet_width ();
      const meters_t outer = section.bank[side] + reach;
      const stone_density_t density = strength;
      const int count = static_cast<int> (std::min (
        60.0f,
        (density * (outer - inner) * river_step).numerical_value_in (one) +
          hash_lane (identity, 0)));
      for (int j = 0; j < count; ++j) {
        const std::uint32_t stone =
          lattice_hash (identity, static_cast<std::uint32_t> (j), 0xba5u);
        const meters_t out = inner + (outer - inner) * hash_lane (stone, 0);
        const meters_t across = side == 0 ? out : -out;
        const meters_t along = river_step * (hash_lane (stone, 1) - 0.5f);
        const plan_position_t at = section.at (across, along);
        // A bar is low ground the flood covers, not the valley side.
        if (ground.ground (at) > section.level + 1.0f * u::m)
          continue;
        // What shows on a bar is its coarse armour, a few times the grain
        // the flood moves over it.
        const meters_t radius =
          0.75f * grain *
          cr::exp ((0.5f * river_gaussian (stone, 4)).numerical_value_in (one));
        layer.loose (at,
                     std::clamp (radius, 0.05f * u::m, 0.3f * u::m),
                     stone,
                     -0.03f * u::m);
      }
    }

    // How far a strand runs out from the water's edge on one side: low
    // ground rising gently away from the water, which a flood spreads over
    // and leaves what it carried on. Zero where the bank rises at once.
    meters_t strand_reach (const RiverGround& ground,
                           const RiverSection& section,
                           int side) {
      const meters_t farthest =
        std::clamp (1.1f * section.regime_width, 3.0f * u::m, 25.0f * u::m);
      meters_t reached = 0.0f * u::m;
      for (meters_t out = 0.5f * u::m; out <= farthest; out += 0.5f * u::m) {
        const meters_t beyond = section.bank[side] + out;
        const terrain::ElevationChange rise =
          ground.ground (
            section.at (side == 0 ? beyond : -beyond, 0.0f * u::m)) -
          section.level;
        if (rise > 0.15f * u::m + 0.1f * out)
          break;
        reached = out;
      }
      return reached;
    }

    // Bars: where the river bends the current crowds the outer bank and
    // slackens on the inner, and drops there what it carries; along a
    // straight reach it lays them on whichever bank has a strand, a little
    // less of one.
    void lay_bars (StoneLayer& layer,
                   const std::vector<RiverSection>& sections,
                   std::uint32_t reach_identity) {
      for (std::size_t k = 0; k < sections.size (); ++k) {
        const RiverSection& section = sections[k];
        if (!section.running || !section.bounded || section.crossing ||
            section.competence < 0.01f * u::m ||
            section.wet_width () < 2.0f * u::m)
          continue;
        // How far the river turns across its own width.
        const auto bend = abs (section.curvature) * section.wet_width ();
        const int inside = section.curvature > 0.0f / u::m ? 0 : 1;
        for (int side = 0; side < 2; ++side) {
          const meters_t reach = strand_reach (layer.ground (), section, side);
          const stone_density_t strength = std::max (
            side == inside
              ? 0.5f *
                  band (0.1f * one, 0.4f * one, bend).numerical_value_in (one) *
                  per_square_metre
              : 0.0f * per_square_metre,
            reach >= 2.0f * u::m ? 0.06f * per_square_metre
                                 : 0.0f * per_square_metre);
          if (strength <= 0.0f * per_square_metre)
            continue;
          lay_bar (layer,
                   section,
                   side,
                   strength,
                   std::min (section.competence, 0.4f * u::m),
                   std::max (reach, 1.5f * u::m),
                   lattice_hash (reach_identity,
                                 static_cast<std::uint32_t> (2 * k + side),
                                 0xbabu));
        }
      }
    }

    // Below a confluence the tributary's coarser load, brought into slower
    // water, settles in a bar along the side it comes in on.
    void lay_confluence_bar (StoneLayer& layer,
                             const std::vector<RiverSection>& tributary,
                             const std::vector<RiverSection>& main,
                             std::uint32_t reach_identity) {
      if (tributary.empty () || main.empty ())
        return;
      const RiverGround& ground = layer.ground ();
      const RiverSection& mouth = tributary.back ();
      std::size_t nearest = 0;
      square_meters_t closest =
        std::numeric_limits<float>::infinity () * (u::m * u::m);
      for (std::size_t k = 0; k < main.size (); ++k) {
        const plan_displacement_t d =
          ground.apart (mouth.centreline - main[k].centreline);
        const square_meters_t d2 = x_of (d) * x_of (d) + z_of (d) * z_of (d);
        if (d2 < closest) {
          closest = d2;
          nearest = k;
        }
      }
      const RiverSection& junction = main[nearest];
      if (!junction.running || closest > (30.0f * u::m) * (30.0f * u::m))
        return;
      // The tributary's own reading upstream of the mixing, where it last
      // ran on its own grade.
      meters_t carried = 0.0f * u::m;
      const std::size_t reach_back = std::min<std::size_t> (
        tributary.size (),
        static_cast<std::size_t> (
          (30.0f * u::m / river_step).numerical_value_in (one)));
      for (std::size_t k = tributary.size () - reach_back;
           k < tributary.size ();
           ++k)
        carried = std::max (carried, tributary[k].competence);
      const meters_t grain = std::max (junction.competence, 0.6f * carried);
      if (grain < 0.05f * u::m)
        return;
      const plan_displacement_t d =
        ground.apart (mouth.centreline - junction.centreline);
      const Vec2& normal = plan_value (junction.normal ());
      const int side =
        x_of (d) * normal.x () + z_of (d) * normal.z () > 0.0f * u::m ? 0 : 1;
      const auto length = static_cast<std::size_t> (
        (std::clamp (3.0f * junction.wet_width (), 6.0f * u::m, 90.0f * u::m) /
         river_step)
          .numerical_value_in (one));
      for (std::size_t i = 0; i < length && nearest + i < main.size (); ++i) {
        const RiverSection& section = main[nearest + i];
        if (!section.running || !section.bounded || section.crossing)
          continue;
        const stone_density_t fade =
          0.6f * (1.0f - static_cast<float> (i) / static_cast<float> (length)) *
          per_square_metre;
        lay_bar (layer,
                 section,
                 side,
                 fade,
                 std::min (grain, 0.4f * u::m),
                 1.5f * u::m,
                 lattice_hash (
                   reach_identity, static_cast<std::uint32_t> (i), 0xc0f1u));
      }
    }

    // Now and then a great rock no flood here could move, left by the ice
    // or fallen from the valley side, in the channel or on its bank.
    void lay_great_rocks (StoneLayer& layer,
                          const std::vector<RiverSection>& sections,
                          std::uint32_t reach_identity) {
      for (std::size_t k = 0; k < sections.size (); ++k) {
        const RiverSection& section = sections[k];
        if (!section.running || section.crossing)
          continue;
        const std::uint32_t rock = lattice_hash (
          reach_identity, static_cast<std::uint32_t> (k), 0x6eaf7u);
        // About one to every 110 metres of river.
        if (hash_lane (rock, 0) * 110.0f * u::m >= river_step)
          continue;
        const meters_t radius =
          0.7f * u::m + 0.9f * u::m * cr::pow (hash_lane (rock, 1), 1.5f);
        const meters_t span = section.wet_width () + 5.0f * u::m;
        const meters_t across =
          -(section.bank[1] + 2.5f * u::m) + span * hash_lane (rock, 2);
        layer.loose (
          section.at (across, 0.0f * u::m), radius, rock, 0.15f * u::m);
      }
    }
  }

  meters_t bed_competence (meters_t depth, grade_t slope) noexcept {
    const meters_t d = std::max (0.0f * u::m, depth);
    const grade_t s = std::max (0.0f * grade[one], slope);
    if (!(d > 0.0f * u::m) || !(s > 0.0f * grade[one]))
      return 0.0f * u::m;
    const shields_number_t shields =
      std::max (0.03f * shields_number[one],
                0.15f * cr::pow (s.numerical_value_in (one), 0.25f) *
                  shields_number[one]);
    return quantity_cast<isq::length> (d * s /
                                       (river_submerged_density * shields));
  }

  BedFineness bed_fineness (meters_t competence) noexcept {
    constexpr meters_t sand = 0.002f * u::m;
    constexpr meters_t cobble = 0.064f * u::m;
    const meters_t grain = std::max (competence, 1e-5f * u::m);
    return std::clamp ((cr::log (meters_value (cobble)) -
                        cr::log (meters_value (grain))) /
                         (cr::log (meters_value (cobble)) -
                          cr::log (meters_value (sand))),
                       0.0f,
                       1.0f) *
           terrain::bed_grain_fineness[one];
  }

  meters_t sheet_competence (const map::SurfaceGeometry& surface,
                             const terrain::WaterSheets& water,
                             std::size_t cell) noexcept {
    const terrain::ElevationChange depth =
      spatial::get<terrain::surface_elevation> (water)[cell] -
      spatial::get<terrain::surface_elevation> (surface)[cell];
    const terrain::WaterVelocity current =
      spatial::get<terrain::water_velocity> (water)[cell];
    const speed_t speed = plan_speed (current);
    if (!(depth > 0.0f * u::m) || !(speed > 0.03f * (u::m / u::s)))
      return 0.0f * u::m;
    // The current descends the surface's grade as fast as it runs along it.
    return bed_competence (
      quantity_cast<isq::length> (0.67f * spate_depth_ratio () * depth),
      std::min ((descent (current) / speed).numerical_value_in (one) *
                  grade[one],
                0.5f * grade[one]));
  }

  RiverBedReading read_river_bed (const map::SurfaceGeometry& surface,
                                  const terrain::WaterSheets& water) {
    MOPPE_PROFILE_ZONE ("read_river_bed");
    const terrain::TerrainDomain& domain = surface.domain ();
    const int width = static_cast<int> (domain.width ());
    const int height = static_cast<int> (domain.height ());
    const auto& ground = spatial::get<terrain::surface_elevation> (surface);
    const auto& normal = spatial::get<terrain::terrain_normal> (surface);
    const auto& level = spatial::get<terrain::surface_elevation> (water);
    constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max ();
    RiverBedReading bed {
      .fineness = std::vector<BedFineness> (
        domain.size (), -1.0f * terrain::bed_grain_fineness[one]),
      .strand = std::vector<StrandHeight> (domain.size (),
                                           -1.0f * terrain::strand_height[one]),
    };
    std::vector<meters_t> competence (domain.size (), 0.0f * u::m);
    // Each cell the running water nearest it, found outward from the water
    // a ring of cells at a time, the first to arrive keeping it.
    std::vector<std::uint32_t> source (domain.size (), none);
    std::vector<std::uint32_t> front;
    for (std::size_t cell = 0; cell < domain.size (); ++cell) {
      competence[cell] = sheet_competence (surface, water, cell);
      if (competence[cell] > 0.0f * u::m) {
        source[cell] = static_cast<std::uint32_t> (cell);
        bed.fineness[cell] = bed_fineness (competence[cell]);
        front.push_back (static_cast<std::uint32_t> (cell));
      }
    }
    const int rings = static_cast<int> (std::ceil (
      (12.0f * u::m / domain.spacing_x ()).numerical_value_in (one)));
    std::vector<std::uint32_t> next;
    for (int ring = 0; ring < rings && !front.empty (); ++ring) {
      next.clear ();
      for (const std::uint32_t cell : front) {
        const int x = static_cast<int> (cell % width);
        const int z = static_cast<int> (cell / width);
        for (int dz = -1; dz <= 1; ++dz)
          for (int dx = -1; dx <= 1; ++dx) {
            const auto near = static_cast<std::uint32_t> (
              terrain::wrap_index (z + dz, height) * width +
              terrain::wrap_index (x + dx, width));
            // Other water, still or running, has its own shore.
            if (source[near] != none || level[near] > ground[near])
              continue;
            source[near] = source[cell];
            next.push_back (near);
          }
      }
      front.swap (next);
    }
    const scale_factor_t spate =
      spate_depth_ratio () - 1.0f * scale_factor[one];
    for (std::size_t cell = 0; cell < domain.size (); ++cell) {
      const std::uint32_t from = source[cell];
      if (from == none || from == cell)
        continue;
      // A strand: level ground the spate stands over, which rises above
      // today's water by the share of the depth the day's flow lacks.
      const terrain::ElevationChange rise = ground[cell] - level[from];
      const terrain::ElevationChange flood =
        quantity_cast<terrain::surface_elevation> (
          spate * (level[from] - ground[from]));
      if (!(rise < flood) || normal[cell].numerical_value_in (one)[1] < 0.9f)
        continue;
      bed.fineness[cell] = bed_fineness (competence[from]);
      bed.strand[cell] =
        (std::max (0.0f * terrain::surface_elevation[u::m], rise) / flood)
          .numerical_value_in (one) *
        terrain::strand_height[one];
    }
    return bed;
  }

  RiverStones plan_river_stones (const map::SurfaceGeometry& surface,
                                 const map::SurfaceReadings& readings,
                                 const terrain::WaterSheets& water,
                                 const terrain::RiverNetwork& rivers,
                                 std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("plan_river_stones");
    const RiverGround ground (surface, readings, water);
    const std::size_t reach_count = rivers.reaches.size ();
    std::vector<std::vector<RiverSection>> surveys (reach_count);
    std::vector<RiverStones> laid (reach_count);

    // Reaches are independent, so bands of them run in parallel and join
    // in reach order: the plan is the same whatever the thread count. The
    // stones of a confluence bar read the survey of the reach below, so
    // every reach is surveyed before any is laid.
    const std::size_t band_count = std::max<std::size_t> (
      1,
      std::min<std::size_t> (
        reach_count, std::max (1u, std::thread::hardware_concurrency ())));
    const auto in_bands = [&] (const auto& work) {
      const auto band = [&] (std::size_t b) {
        for (std::size_t r = b * reach_count / band_count;
             r < (b + 1) * reach_count / band_count;
             ++r)
          work (r);
      };
      std::vector<std::jthread> workers;
      workers.reserve (band_count - 1);
      for (std::size_t b = 1; b < band_count; ++b)
        workers.emplace_back (band, b);
      band (0);
    };
    in_bands ([&] (std::size_t r) {
      surveys[r] = survey_reach (rivers.reaches[r], ground);
    });
    const auto reach_identity = [seed] (std::size_t r) {
      return lattice_hash (static_cast<std::uint32_t> (r), 0x7e1u, seed);
    };

    // The crossings first, so nothing else is strewn across them. They are
    // chosen over the whole network at once, the best first, and none
    // within a short walk of another.
    {
      std::vector<std::vector<CrossingSite>> found (reach_count);
      in_bands ([&] (std::size_t r) {
        find_crossings (ground, surveys[r], r, reach_identity (r), found[r]);
      });
      std::vector<CrossingSite> candidates;
      for (const std::vector<CrossingSite>& reach : found)
        candidates.insert (candidates.end (), reach.begin (), reach.end ());
      std::ranges::stable_sort (candidates, [] (const auto& a, const auto& b) {
        return a.score > b.score;
      });
      std::vector<plan_position_t> crossed;
      for (const CrossingSite& candidate : candidates) {
        std::vector<RiverSection>& sections = surveys[candidate.reach];
        const RiverSection& section = sections[candidate.index];
        const bool near =
          std::ranges::any_of (crossed, [&] (const plan_position_t& at) {
            const plan_displacement_t d =
              ground.apart (section.centreline - at);
            return x_of (d) * x_of (d) + z_of (d) * z_of (d) <
                   stepping_spacing * stepping_spacing;
          });
        if (near)
          continue;
        StoneLayer layer (ground, laid[candidate.reach]);
        if (!lay_stepping_stones (
              layer,
              section,
              lattice_hash (reach_identity (candidate.reach),
                            static_cast<std::uint32_t> (candidate.index),
                            0x5e9u)))
          continue;
        crossed.push_back (section.centreline);
        // Keep the way clear for a couple of metres up and down stream.
        const auto clear = static_cast<std::size_t> (
          (2.0f * u::m / river_step).numerical_value_in (one));
        for (std::size_t i = candidate.index > clear ? candidate.index - clear
                                                     : 0;
             i <= std::min (sections.size () - 1, candidate.index + clear);
             ++i)
          sections[i].crossing = true;
      }
    }

    in_bands ([&] (std::size_t r) {
      StoneLayer layer (ground, laid[r]);
      const std::vector<RiverSection>& sections = surveys[r];
      lay_steps (layer, sections, reach_identity (r));
      lay_bed (layer, sections, reach_identity (r));
      lay_bars (layer, sections, reach_identity (r));
      lay_great_rocks (layer, sections, reach_identity (r));
      const terrain::RiverReachId below = rivers.reaches[r].downstream_reach;
      if (below != terrain::RiverReach::no_id && below.value < reach_count)
        lay_confluence_bar (
          layer, sections, surveys[below.value], reach_identity (r));
    });

    RiverStones stones;
    for (const RiverStones& reach : laid) {
      stones.sites.insert (
        stones.sites.end (), reach.sites.begin (), reach.sites.end ());
      stones.crossings.insert (stones.crossings.end (),
                               reach.crossings.begin (),
                               reach.crossings.end ());
    }
    return stones;
  }
}
