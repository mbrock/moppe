#include <moppe/correct_math.hh>
#include <moppe/game/river_stones.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

namespace moppe::game {
  namespace {
    // Rock is 2.65 times as dense as water.
    constexpr float river_submerged_density = 1.65f;
    // The river is walked a metre at a time.
    constexpr float river_step = 1.0f;
    constexpr float river_tau = 6.2831853f;
    // The tread a rider follows, as the boulder planner keeps it clear.
    constexpr float river_trail_tread = 0.75f;
    // The top of a settled stone stands this many radii above the ground it
    // settled on: the body's height less its burial.
    constexpr float river_settled_top = 2.0f * boulder_squash - boulder_burial;
    // A stone in water broad enough to stand on is solid; so is every stone
    // the boulder planner would make so.
    constexpr float river_foothold_radius = 0.3f;

    // Stepping stones: a stride apart, centre to centre, so a walker's
    // feet find one stone before they leave the last, with their tops a
    // hand's breadth or two clear of the water.
    constexpr float stepping_pitch = 0.70f;
    constexpr float stepping_least_radius = 0.40f;
    constexpr float stepping_greatest_radius = 0.75f;
    constexpr float stepping_deepest = 0.55f;
    constexpr float stepping_widest = 14.0f;
    constexpr float stepping_spacing = 250.0f; // metres apart, at least

    // The sheet holds the water of an ordinary day, a share of the spate
    // that shaped the bed and sorted its stones (WatercoursePaint::flow).
    // A flow's depth goes as its discharge to the two thirds, in a wide
    // channel as over a sill, so the spate stood this many times deeper.
    float spate_depth_ratio () {
      return cr::pow (terrain::WatercoursePaint {}.flow, -2.0f / 3.0f);
    }

    position_t river_probe (float x, float z) {
      return position (Vec3 (x, 0.0f, z));
    }

    float river_cross (float ax, float az, float bx, float bz) {
      return ax * bz - az * bx;
    }

    // Roughly normal, from three independent lanes: mean 0, deviation 1.
    float river_gaussian (std::uint32_t identity, std::uint32_t lane) {
      return 2.0f *
             (hash_lane (identity, lane) + hash_lane (identity, lane + 1) +
              hash_lane (identity, lane + 2) - 1.5f);
    }

    // The ground and the water over it, read at any place.
    class RiverGround {
    public:
      RiverGround (const map::SurfaceGeometry& surface,
                   const map::SurfaceReadings& readings,
                   const terrain::WaterSheets& water)
          : m_surface (surface), m_readings (readings), m_water (water),
            m_period_x (
              surface.domain ().period_x ().numerical_value_in (u::m)),
            m_period_z (
              surface.domain ().period_z ().numerical_value_in (u::m)) {}

      float ground (float x, float z) const {
        return terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (m_surface,
                                                       river_probe (x, z)));
      }
      float level (float x, float z) const {
        return terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (m_water,
                                                       river_probe (x, z)));
      }
      float depth (float x, float z) const {
        return level (x, z) - ground (x, z);
      }
      float speed (float x, float z) const {
        const Vec3 current =
          spatial::sample<terrain::water_velocity> (m_water, river_probe (x, z))
            .numerical_value_in (u::m / u::s);
        return cr::hypot (current[0], current[2]);
      }
      Vec3 normal (float x, float z) const {
        return spatial::sample<terrain::terrain_normal> (m_surface,
                                                         river_probe (x, z))
          .numerical_value_in (one);
      }
      float trail (float x, float z) const {
        return spatial::sample<map::trail_influence> (m_readings,
                                                      river_probe (x, z))
          .numerical_value_in (one);
      }
      float home (float x, float z) const {
        return spatial::sample<map::home_base_influence> (m_readings,
                                                          river_probe (x, z))
          .numerical_value_in (one);
      }
      float wrap_x (float x) const {
        return terrain::wrap_coordinate (x, m_period_x);
      }
      float wrap_z (float z) const {
        return terrain::wrap_coordinate (z, m_period_z);
      }
      // The shortest way between two places on the torus.
      float apart_x (float dx) const {
        return dx - m_period_x * std::round (dx / m_period_x);
      }
      float apart_z (float dz) const {
        return dz - m_period_z * std::round (dz / m_period_z);
      }

    private:
      const map::SurfaceGeometry& m_surface;
      const map::SurfaceReadings& m_readings;
      const terrain::WaterSheets& m_water;
      float m_period_x;
      float m_period_z;
    };

    // A metre of river: where its centreline runs and what the water there
    // is doing, read across the whole wetted section.
    struct RiverSection {
      float x = 0.0f; // the centreline, unwrapped as the alignment is
      float z = 0.0f;
      float tx = 1.0f; // downstream
      float tz = 0.0f;
      float regime_width = 0.0f;
      float level = 0.0f;
      float depth = 0.0f;   // at the centreline
      float deepest = 0.0f; // anywhere across
      float speed = 0.0f;   // of the current at the centreline
      // How far the water reaches along the normal (-tz, tx), each way.
      float bank[2] = { 0.0f, 0.0f };
      bool bounded = false; // dry ground found on both sides
      bool running = false;
      bool falling = false; // over a waterfall
      // Over the reach around it.
      float slope = 0.0f;
      float competence = 0.0f; // metres
      float curvature = 0.0f;  // 1/m, positive turning toward +normal
      // Nothing else is placed where stepping stones cross.
      bool crossing = false;

      float nx () const {
        return -tz;
      }
      float nz () const {
        return tx;
      }
      float wet_width () const {
        return bank[0] + bank[1];
      }
      // The place `across` metres along the normal and `along` downstream.
      float at_x (float across, float along) const {
        return x + nx () * across + tx * along;
      }
      float at_z (float across, float along) const {
        return z + nz () * across + tz * along;
      }
    };

    float river_mix (float a, float b, float t) {
      return a + (b - a) * t;
    }

    // Walks a reach's alignment a metre at a time and reads each section.
    std::vector<RiverSection> survey_reach (const terrain::RiverReach& reach,
                                            const RiverGround& ground) {
      const auto& points = reach.alignment.points;
      std::vector<RiverSection> sections;
      if (points.size () < 2)
        return sections;
      std::vector<float> along (points.size (), 0.0f);
      for (std::size_t i = 1; i < points.size (); ++i)
        along[i] = along[i - 1] + cr::hypot (points[i].x_m - points[i - 1].x_m,
                                             points[i].z_m - points[i - 1].z_m);
      const float length = along.back ();
      if (length < 4.0f * river_step)
        return sections;
      const std::size_t count =
        static_cast<std::size_t> (std::floor (length / river_step)) + 1;
      sections.resize (count);
      std::vector<float> area (count);
      std::vector<float> pooled (count);
      std::size_t segment = 0;
      for (std::size_t k = 0; k < count; ++k) {
        const float s = std::min (static_cast<float> (k) * river_step, length);
        while (segment + 2 < points.size () && along[segment + 1] < s)
          ++segment;
        const auto& a = points[segment];
        const auto& b = points[segment + 1];
        const float span = along[segment + 1] - along[segment];
        const float t = span > 0.0f
                          ? std::clamp ((s - along[segment]) / span, 0.0f, 1.0f)
                          : 0.0f;
        RiverSection& section = sections[k];
        section.x = river_mix (a.x_m, b.x_m, t);
        section.z = river_mix (a.z_m, b.z_m, t);
        area[k] = river_mix (a.contributing_area_m2, b.contributing_area_m2, t);
        pooled[k] =
          std::max ({ a.pooled, b.pooled, a.standing_water, b.standing_water });
        section.falling = std::max (a.waterfall, b.waterfall) > 0.0f;
      }

      for (std::size_t k = 0; k < count; ++k) {
        RiverSection& section = sections[k];
        section.regime_width = terrain::river_width (area[k] * u::m * u::m)
                                 .numerical_value_in (u::m);
        // The tangent over about half the channel's width, so the meanders
        // the alignment draws show and the cells it steps between do not.
        const auto reach_back = static_cast<std::ptrdiff_t> (
          std::clamp (0.5f * section.regime_width, 1.5f, 8.0f) / river_step);
        const std::size_t before =
          static_cast<std::size_t> (std::max<std::ptrdiff_t> (
            0, static_cast<std::ptrdiff_t> (k) - reach_back));
        const std::size_t after = std::min (count - 1, k + reach_back);
        const float dx = sections[after].x - sections[before].x;
        const float dz = sections[after].z - sections[before].z;
        const float run = cr::hypot (dx, dz);
        if (run > 1e-4f) {
          section.tx = dx / run;
          section.tz = dz / run;
        }
        section.level = ground.level (section.x, section.z);
        section.depth = section.level - ground.ground (section.x, section.z);
        section.speed = ground.speed (section.x, section.z);
        section.running =
          section.depth > 0.02f && section.speed > 0.03f && pooled[k] < 0.5f;
        if (!(section.depth > 0.0f))
          continue;
        // Out each way across the section until the ground stands dry.
        const float limit = 1.6f * section.regime_width + 6.0f;
        section.bounded = true;
        section.deepest = section.depth;
        for (int side = 0; side < 2; ++side) {
          const float sign = side == 0 ? 1.0f : -1.0f;
          float reached = limit;
          float wet = section.depth;
          for (float across = 0.5f; across <= limit; across += 0.5f) {
            const float d = ground.depth (section.at_x (sign * across, 0.0f),
                                          section.at_z (sign * across, 0.0f));
            if (!(d > 0.0f)) {
              // The waterline lies between the last wet probe and this one.
              reached = across - 0.5f * d / std::min (d - wet, -1e-4f);
              break;
            }
            wet = d;
            section.deepest = std::max (section.deepest, d);
          }
          if (reached >= limit)
            section.bounded = false;
          section.bank[side] = std::max (0.0f, reached);
        }
      }

      // The grade the water surface falls over a few widths, the depth that
      // drives it, and how sharply the river turns there.
      for (std::size_t k = 0; k < count; ++k) {
        RiverSection& section = sections[k];
        const auto window = static_cast<std::ptrdiff_t> (
          std::clamp (1.5f * section.regime_width, 3.0f, 30.0f) / river_step);
        const auto first = static_cast<std::size_t> (std::max<std::ptrdiff_t> (
          0, static_cast<std::ptrdiff_t> (k) - window));
        const std::size_t last = std::min (count - 1, k + window);
        if (last > first)
          section.slope =
            std::max (0.0f,
                      (sections[first].level - sections[last].level) /
                        (static_cast<float> (last - first) * river_step));
        float depth = 0.0f;
        int wet = 0;
        for (std::size_t i = first; i <= last; ++i)
          if (sections[i].running) {
            depth += sections[i].depth;
            ++wet;
          }
        // A section's mean depth is about two thirds of its deepest.
        if (section.running && wet)
          section.competence =
            bed_competence (0.67f * spate_depth_ratio () * depth /
                              static_cast<float> (wet) * u::m,
                            section.slope)
              .numerical_value_in (u::m);
        const auto turn = static_cast<std::ptrdiff_t> (
          std::clamp (section.regime_width, 2.0f, 20.0f) / river_step);
        const std::size_t back = static_cast<std::size_t> (
          std::max<std::ptrdiff_t> (0, static_cast<std::ptrdiff_t> (k) - turn));
        const std::size_t ahead = std::min (count - 1, k + turn);
        if (ahead > back)
          section.curvature = river_cross (sections[back].tx,
                                           sections[back].tz,
                                           sections[ahead].tx,
                                           sections[ahead].tz) /
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
      bool settle (
        float x, float z, float radius, float turn, float& settled) const {
        if (m_ground.home (x, z) > 0.02f)
          return false;
        const float centre = m_ground.ground (x, z);
        float lowest = centre;
        float sum = centre;
        for (int k = 0; k < 4; ++k) {
          const float angle = turn + 0.25f * river_tau * static_cast<float> (k);
          const float px = x + 0.85f * radius * cr::cos (angle);
          const float pz = z + 0.85f * radius * cr::sin (angle);
          if (m_ground.trail (px, pz) > river_trail_tread)
            return false;
          const float height = m_ground.ground (px, pz);
          lowest = std::min (lowest, height);
          sum += height;
        }
        settled = 0.5f * (lowest + 0.2f * sum);
        return true;
      }

      // A loose stone where it falls: kept only if it shows, its top at or
      // just under the surface. `least_top` is how far above the water its
      // top must stand.
      void loose (float x,
                  float z,
                  float radius,
                  std::uint32_t identity,
                  float least_top = -0.04f) {
        float settled = 0.0f;
        if (!settle (
              x, z, radius, river_tau * hash_lane (identity, 3), settled))
          return;
        const float level = m_ground.level (x, z);
        const float top = settled + river_settled_top * radius;
        const bool wet = level > settled;
        if (wet && top < level + least_top)
          return;
        emit (x,
              settled + (boulder_squash - boulder_burial) * radius,
              z,
              radius,
              identity,
              wet && radius >= river_foothold_radius && top > level + 0.04f,
              wet);
      }

      void emit (float x,
                 float centre_y,
                 float z,
                 float radius,
                 std::uint32_t identity,
                 bool solid,
                 bool wet) {
        m_sites.push_back ({
          .centre = position (
            Vec3 (m_ground.wrap_x (x), centre_y, m_ground.wrap_z (z))),
          .normal = m_ground.normal (x, z) * terrain::terrain_normal[one],
          .radius = radius * u::m,
          .moisture = (wet ? 1.0f : 0.75f) * map::surface_moisture[one],
          .seed = identity,
          .solid = solid,
        });
      }

      void cross (float from_x, float from_z, float to_x, float to_z) {
        const auto bank = [&] (float x, float z) {
          return position (Vec3 (
            m_ground.wrap_x (x), m_ground.ground (x, z), m_ground.wrap_z (z)));
        };
        m_crossings.push_back (
          { .from = bank (from_x, from_z), .to = bank (to_x, to_z) });
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
      const float from = -(section.bank[1] + 0.3f);
      const float to = section.bank[0] + 0.3f;
      int stones = 0;
      std::uint32_t k = 0;
      for (float across = from; across <= to; ++k) {
        const std::uint32_t stone = lattice_hash (identity, k, 0x57e9u);
        const float along = 0.08f * (2.0f * hash_lane (stone, 0) - 1.0f);
        const float x = section.at_x (across, along);
        const float z = section.at_z (across, along);
        across += stepping_pitch + 0.04f * (2.0f * hash_lane (stone, 1) - 1.0f);
        // Wherever the feet would be wet between this stone's neighbours;
        // where they would be dry the walker steps on the ground itself.
        float wettest = -1.0f;
        for (float reach = -0.5f; reach <= 0.5f; reach += 0.25f)
          wettest = std::max (
            wettest,
            ground.depth (x + section.nx () * reach * stepping_pitch,
                          z + section.nz () * reach * stepping_pitch));
        if (!(wettest > 0.02f))
          continue;
        const float level =
          std::max (ground.level (x, z), ground.ground (x, z));
        // The bed under the stone's footprint, at its lowest.
        float bed = ground.ground (x, z);
        for (int rim = 0; rim < 4; ++rim) {
          const float angle = 0.25f * river_tau * static_cast<float> (rim);
          bed = std::min (bed,
                          ground.ground (x + 0.4f * cr::cos (angle),
                                         z + 0.4f * cr::sin (angle)));
        }
        const float top = level + 0.09f + 0.09f * hash_lane (stone, 2);
        // Bedded a few centimetres below the lowest of the bed under it, so
        // it stands on the bottom rather than floating over a hollow.
        const float radius =
          std::max (stepping_least_radius + 0.06f * hash_lane (stone, 3),
                    (top - bed + 0.06f) / (2.0f * boulder_squash));
        float settled = 0.0f;
        if (radius > stepping_greatest_radius ||
            !layer.settle (x, z, radius, 0.0f, settled)) {
          layer.truncate (start);
          return false;
        }
        layer.emit (
          x, top - boulder_squash * radius, z, radius, stone, true, true);
        ++stones;
      }
      if (stones == 0)
        return false;
      layer.cross (section.at_x (from - 0.5f, 0.0f),
                   section.at_z (from - 0.5f, 0.0f),
                   section.at_x (to + 0.5f, 0.0f),
                   section.at_z (to + 0.5f, 0.0f));
      return true;
    }

    // A place a reach could be crossed on stones, and how good a one.
    struct CrossingSite {
      float score = 0.0f;
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
            section.deepest > stepping_deepest || section.speed > 2.4f ||
            section.slope > 0.06f || section.wet_width () < 1.5f ||
            section.wet_width () > stepping_widest)
          continue;
        // Both banks a step's height or less above the water.
        bool banks = true;
        for (int side = 0; side < 2 && banks; ++side) {
          const float across =
            (side == 0 ? 1.0f : -1.0f) * (section.bank[side] + 0.8f);
          const float bank = ground.ground (section.at_x (across, 0.0f),
                                            section.at_z (across, 0.0f));
          banks = bank < section.level + 0.4f;
        }
        if (!banks)
          continue;
        float trail = 0.0f;
        for (float along = -12.0f; along <= 12.0f; along += 3.0f)
          trail = std::max (trail,
                            ground.trail (section.at_x (0.0f, along),
                                          section.at_z (0.0f, along)));
        const std::uint32_t identity = lattice_hash (
          reach_identity, static_cast<std::uint32_t> (k), 0xc705u);
        sites.push_back (
          { .score = 0.6f * smoothstep (0.02f, 0.20f, section.competence) +
                     1.2f * smoothstep (0.15f, 0.55f, trail) +
                     0.4f * hash_lane (identity, 0),
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
      float last = -1e9f;
      for (std::size_t k = 0; k < sections.size (); ++k) {
        const RiverSection& section = sections[k];
        if (!section.running || section.slope < 0.035f ||
            section.competence < 0.1f || section.crossing)
          continue;
        const float s = static_cast<float> (k) * river_step;
        const float keystone =
          std::clamp (2.5f * section.competence, 0.5f, 1.6f);
        const float spacing = std::clamp (
          1.5f * keystone / section.slope, 3.0f + section.wet_width (), 40.0f);
        if (s - last < 0.5f * spacing)
          continue;
        const auto reach = std::max<std::size_t> (
          2, static_cast<std::size_t> (spacing / (3.0f * river_step)));
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
        float across = -(section.bank[1] + 0.2f);
        float previous = 0.0f;
        for (std::uint32_t j = 0; across <= section.bank[0] + 0.2f; ++j) {
          const std::uint32_t stone = lattice_hash (row, j, 0x6e75u);
          const float radius =
            0.5f * keystone * (0.75f + 0.5f * hash_lane (stone, 0));
          across += 0.9f * (previous + radius);
          previous = radius;
          const float along = 0.2f * radius * river_gaussian (stone, 1);
          layer.loose (section.at_x (across, along),
                       section.at_z (across, along),
                       radius,
                       stone,
                       -0.08f);
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
        if (!section.running || section.crossing || section.competence < 0.04f)
          continue;
        const float span = section.wet_width () + 2.0f;
        const float density =
          0.05f + 0.25f * smoothstep (0.04f, 0.4f, section.competence);
        const std::uint32_t metre =
          lattice_hash (reach_identity, static_cast<std::uint32_t> (k), 0xbedu);
        const int count = static_cast<int> (
          std::min (60.0f, density * span * river_step + hash_lane (metre, 0)));
        for (int j = 0; j < count; ++j) {
          const std::uint32_t stone =
            lattice_hash (metre, static_cast<std::uint32_t> (j), 0xc0bu);
          const float across =
            -(section.bank[1] + 1.0f) + span * hash_lane (stone, 0);
          const float along = river_step * (hash_lane (stone, 1) - 0.5f);
          float radius = 0.5f * section.competence *
                         cr::exp (0.6f * river_gaussian (stone, 4));
          if (hash_lane (stone, 2) < 0.15f)
            radius *= 2.0f + 2.0f * hash_lane (stone, 3);
          layer.loose (section.at_x (across, along),
                       section.at_z (across, along),
                       std::clamp (radius, 0.06f, 1.2f),
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
                  float strength,
                  float grain,
                  float reach,
                  std::uint32_t identity) {
      const RiverGround& ground = layer.ground ();
      const float sign = side == 0 ? 1.0f : -1.0f;
      const float inner = section.bank[side] - 0.3f * section.wet_width ();
      const float outer = section.bank[side] + reach;
      const float density = strength;
      const int count = static_cast<int> (std::min (
        60.0f,
        density * (outer - inner) * river_step + hash_lane (identity, 0)));
      for (int j = 0; j < count; ++j) {
        const std::uint32_t stone =
          lattice_hash (identity, static_cast<std::uint32_t> (j), 0xba5u);
        const float across =
          sign * (inner + (outer - inner) * hash_lane (stone, 0));
        const float along = river_step * (hash_lane (stone, 1) - 0.5f);
        const float x = section.at_x (across, along);
        const float z = section.at_z (across, along);
        // A bar is low ground the flood covers, not the valley side.
        if (ground.ground (x, z) > section.level + 1.0f)
          continue;
        // What shows on a bar is its coarse armour, a few times the grain
        // the flood moves over it.
        const float radius =
          0.75f * grain * cr::exp (0.5f * river_gaussian (stone, 4));
        layer.loose (x, z, std::clamp (radius, 0.05f, 0.3f), stone, -0.03f);
      }
    }

    // How far a strand runs out from the water's edge on one side: low
    // ground rising gently away from the water, which a flood spreads over
    // and leaves what it carried on. Zero where the bank rises at once.
    float strand_reach (const RiverGround& ground,
                        const RiverSection& section,
                        int side) {
      const float sign = side == 0 ? 1.0f : -1.0f;
      const float farthest =
        std::clamp (1.1f * section.regime_width, 3.0f, 25.0f);
      float reached = 0.0f;
      for (float out = 0.5f; out <= farthest; out += 0.5f) {
        const float across = sign * (section.bank[side] + out);
        const float rise = ground.ground (section.at_x (across, 0.0f),
                                          section.at_z (across, 0.0f)) -
                           section.level;
        if (rise > 0.15f + 0.1f * out)
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
            section.competence < 0.01f || section.wet_width () < 2.0f)
          continue;
        const float bend = std::abs (section.curvature) * section.wet_width ();
        const int inside = section.curvature > 0.0f ? 0 : 1;
        for (int side = 0; side < 2; ++side) {
          const float reach = strand_reach (layer.ground (), section, side);
          const float strength = std::max (
            side == inside ? 0.5f * smoothstep (0.1f, 0.4f, bend) : 0.0f,
            reach >= 2.0f ? 0.06f : 0.0f);
          if (strength <= 0.0f)
            continue;
          lay_bar (layer,
                   section,
                   side,
                   strength,
                   std::min (section.competence, 0.4f),
                   std::max (reach, 1.5f),
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
      float closest = std::numeric_limits<float>::infinity ();
      for (std::size_t k = 0; k < main.size (); ++k) {
        const float dx = ground.apart_x (mouth.x - main[k].x);
        const float dz = ground.apart_z (mouth.z - main[k].z);
        const float d2 = dx * dx + dz * dz;
        if (d2 < closest) {
          closest = d2;
          nearest = k;
        }
      }
      const RiverSection& junction = main[nearest];
      if (!junction.running || closest > 30.0f * 30.0f)
        return;
      // The tributary's own reading upstream of the mixing, where it last
      // ran on its own grade.
      float carried = 0.0f;
      const std::size_t reach_back = std::min<std::size_t> (
        tributary.size (), static_cast<std::size_t> (30.0f / river_step));
      for (std::size_t k = tributary.size () - reach_back;
           k < tributary.size ();
           ++k)
        carried = std::max (carried, tributary[k].competence);
      const float grain = std::max (junction.competence, 0.6f * carried);
      if (grain < 0.05f)
        return;
      const float dx = ground.apart_x (mouth.x - junction.x);
      const float dz = ground.apart_z (mouth.z - junction.z);
      const int side = dx * junction.nx () + dz * junction.nz () > 0.0f ? 0 : 1;
      const auto length = static_cast<std::size_t> (
        std::clamp (3.0f * junction.wet_width (), 6.0f, 90.0f) / river_step);
      for (std::size_t i = 0; i < length && nearest + i < main.size (); ++i) {
        const RiverSection& section = main[nearest + i];
        if (!section.running || !section.bounded || section.crossing)
          continue;
        const float fade =
          0.6f * (1.0f - static_cast<float> (i) / static_cast<float> (length));
        lay_bar (layer,
                 section,
                 side,
                 fade,
                 std::min (grain, 0.4f),
                 1.5f,
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
        if (hash_lane (rock, 0) * 110.0f >= river_step)
          continue;
        const float radius = 0.7f + 0.9f * cr::pow (hash_lane (rock, 1), 1.5f);
        const float span = section.wet_width () + 5.0f;
        const float across =
          -(section.bank[1] + 2.5f) + span * hash_lane (rock, 2);
        layer.loose (section.at_x (across, 0.0f),
                     section.at_z (across, 0.0f),
                     radius,
                     rock,
                     0.15f);
      }
    }
  }

  meters_t bed_competence (meters_t depth, float slope) noexcept {
    const float d = std::max (0.0f, depth.numerical_value_in (u::m));
    const float s = std::max (0.0f, slope);
    if (!(d > 0.0f) || !(s > 0.0f))
      return 0.0f * u::m;
    const float shields = std::max (0.03f, 0.15f * cr::pow (s, 0.25f));
    return d * s / (river_submerged_density * shields) * u::m;
  }

  float bed_fineness (meters_t competence) noexcept {
    constexpr float sand = 0.002f;
    constexpr float cobble = 0.064f;
    const float grain = std::max (competence.numerical_value_in (u::m), 1e-5f);
    return std::clamp ((cr::log (cobble) - cr::log (grain)) /
                         (cr::log (cobble) - cr::log (sand)),
                       0.0f,
                       1.0f);
  }

  meters_t sheet_competence (const map::SurfaceGeometry& surface,
                             const terrain::WaterSheets& water,
                             std::size_t cell) noexcept {
    const float depth =
      terrain::surface_elevation_value (
        spatial::get<terrain::surface_elevation> (water)[cell]) -
      terrain::surface_elevation_value (
        spatial::get<terrain::surface_elevation> (surface)[cell]);
    const Vec3 current =
      spatial::get<terrain::water_velocity> (water)[cell].numerical_value_in (
        u::m / u::s);
    const float speed = cr::hypot (current[0], current[2]);
    if (!(depth > 0.0f) || !(speed > 0.03f))
      return 0.0f * u::m;
    // The current descends the surface's grade as fast as it runs along it.
    return bed_competence (0.67f * spate_depth_ratio () * depth * u::m,
                           std::min (-current[1] / speed, 0.5f));
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
      .fineness = std::vector<float> (domain.size (), -1.0f),
      .strand = std::vector<float> (domain.size (), -1.0f),
    };
    std::vector<float> competence (domain.size (), 0.0f);
    // Each cell the running water nearest it, found outward from the water
    // a ring of cells at a time, the first to arrive keeping it.
    std::vector<std::uint32_t> source (domain.size (), none);
    std::vector<std::uint32_t> front;
    for (std::size_t cell = 0; cell < domain.size (); ++cell) {
      competence[cell] =
        sheet_competence (surface, water, cell).numerical_value_in (u::m);
      if (competence[cell] > 0.0f) {
        source[cell] = static_cast<std::uint32_t> (cell);
        bed.fineness[cell] = bed_fineness (competence[cell] * u::m);
        front.push_back (static_cast<std::uint32_t> (cell));
      }
    }
    const int rings = static_cast<int> (
      std::ceil (12.0f / domain.spacing_x ().numerical_value_in (u::m)));
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
    const float spate = spate_depth_ratio () - 1.0f;
    for (std::size_t cell = 0; cell < domain.size (); ++cell) {
      const std::uint32_t from = source[cell];
      if (from == none || from == cell)
        continue;
      // A strand: level ground the spate stands over, which rises above
      // today's water by the share of the depth the day's flow lacks.
      const float rise = terrain::surface_elevation_value (ground[cell]) -
                         terrain::surface_elevation_value (level[from]);
      const float flood =
        spate * (terrain::surface_elevation_value (level[from]) -
                 terrain::surface_elevation_value (ground[from]));
      if (!(rise < flood) || normal[cell].numerical_value_in (one)[1] < 0.9f)
        continue;
      bed.fineness[cell] = bed_fineness (competence[from] * u::m);
      bed.strand[cell] = std::max (0.0f, rise) / flood;
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
      std::vector<Vec3> crossed;
      for (const CrossingSite& candidate : candidates) {
        std::vector<RiverSection>& sections = surveys[candidate.reach];
        const RiverSection& section = sections[candidate.index];
        const bool near = std::ranges::any_of (crossed, [&] (const Vec3& at) {
          const float dx = ground.apart_x (section.x - at[0]);
          const float dz = ground.apart_z (section.z - at[2]);
          return dx * dx + dz * dz < stepping_spacing * stepping_spacing;
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
        crossed.push_back (Vec3 (section.x, 0.0f, section.z));
        // Keep the way clear for a couple of metres up and down stream.
        const auto clear = static_cast<std::size_t> (2.0f / river_step);
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
