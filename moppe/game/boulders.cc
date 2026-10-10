#include <moppe/correct_math.hh>
#include <moppe/game/boulders.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace moppe::game {
  namespace {
    // One candidate per lattice cell, jittered anywhere inside it. The
    // pitch bounds the density of the rockiest ground; thinning by habitat
    // decides everywhere else.
    constexpr float boulder_cell_metres = 6.0f;
    constexpr float boulder_cell_odds = 0.70f;
    // Stones smaller than this are ridden over rather than into.
    constexpr float boulder_collider_radius = 0.5f;
    // The tread a rider follows; no part of a rock's footprint enters it.
    constexpr float boulder_trail_tread = 0.75f;
    constexpr float boulder_tau = 6.2831853f;

    // Every field a candidate stone consults, read at one place.
    struct StoneGround {
      float elevation;
      Vec3 normal;
      float erosion;
      float deposition;
      float forest;
      float moisture;
      float trail;
      float home;
      float waterline;
      float water_depth;
    };

    position_t stone_probe (float x, float z) {
      return position (Vec3 (x, 0.0f, z));
    }

    float
    stone_elevation (const map::SurfaceGeometry& surface, float x, float z) {
      return terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (surface,
                                                     stone_probe (x, z)));
    }

    float stone_water_depth (const map::SurfaceGeometry& surface,
                             const terrain::WaterSheets& water,
                             float x,
                             float z) {
      const float level = terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (water,
                                                     stone_probe (x, z)));
      return level - stone_elevation (surface, x, z);
    }

    float stone_trail (const map::SurfaceReadings& readings, float x, float z) {
      return spatial::sample<map::trail_influence> (readings,
                                                    stone_probe (x, z))
        .numerical_value_in (one);
    }

    StoneGround read_stone_ground (const map::SurfaceGeometry& surface,
                                   const map::SurfaceReadings& readings,
                                   const terrain::WaterSheets& water,
                                   float x,
                                   float z) {
      const position_t where = stone_probe (x, z);
      const auto read = [&] (auto quantity) {
        return spatial::sample<quantity> (readings, where)
          .numerical_value_in (one);
      };
      return {
        .elevation = stone_elevation (surface, x, z),
        .normal = spatial::sample<terrain::terrain_normal> (surface, where)
                    .numerical_value_in (one),
        .erosion = read (map::erosion_exposure),
        .deposition = read (map::deposition_cover),
        .forest = read (map::forest_cover),
        .moisture = read (map::surface_moisture),
        .trail = read (map::trail_influence),
        .home = read (map::home_base_influence),
        .waterline = spatial::sample<map::waterline_distance> (readings, where)
                       .numerical_value_in (u::m),
        .water_depth = stone_water_depth (surface, water, x, z),
      };
    }

    // How much loose rock the ground holds, and which story put it there.
    struct Rockiness {
      float total;
      float erratic; // share of the answer owed to high open ground
      float scree;   // share owed to a cliff above
      float stream;  // share owed to running or standing water nearby
    };

    Rockiness stone_rockiness (const StoneGround& ground,
                               float steepness_above,
                               float altitude) {
      const float steepness = 1.0f - ground.normal[1];
      // Talus: frost and erosion break rock out of steep slopes, but it
      // cannot rest on a face much past its angle of repose.
      const float talus = 0.8f * smoothstep (0.05f, 0.20f, steepness) *
                          (0.15f + 0.85f * ground.erosion) *
                          (1.0f - smoothstep (0.45f, 0.62f, steepness));
      // Scree: what falls from a cliff collects on the gentler ground
      // below it, where the world laid material down.
      const float scree = smoothstep (0.18f, 0.40f, steepness_above) *
                          (1.0f - smoothstep (0.25f, 0.45f, steepness)) *
                          (0.45f + 0.55f * ground.deposition);
      // Erratics: the ice left single stones lying on the high open ground.
      const float erratic = 0.45f * smoothstep (0.45f, 0.80f, altitude) *
                            (1.0f - ground.forest) *
                            (1.0f - smoothstep (0.15f, 0.35f, steepness));
      // Cobbles along a stream or a shore, more where it deposits.
      const float stream = (1.0f - smoothstep (0.5f, 7.5f, ground.waterline)) *
                           (0.35f + 0.65f * ground.deposition);
      // A lush, level meadow has buried what rock it had.
      const float lush = smoothstep (0.45f, 0.80f, ground.moisture) *
                         (1.0f - smoothstep (0.03f, 0.12f, steepness)) *
                         (1.0f - ground.erosion);
      const float dry =
        std::max ({ talus, scree, erratic }) * (1.0f - 0.7f * lush);
      const float rock = std::max (dry, stream);
      const float total = (rock + 0.015f) * (1.0f - 0.85f * ground.forest);
      const float sum = std::max (talus + scree + erratic + stream, 1e-4f);
      return { .total = std::clamp (total, 0.0f, 1.0f),
               .erratic = erratic / sum,
               .scree = scree / sum,
               .stream = stream / sum };
    }

    // How much steeper the ground is a little way uphill: a cliff above
    // shows as a steep reading there.
    float steepness_uphill (const map::SurfaceGeometry& surface,
                            const StoneGround& ground,
                            float x,
                            float z) {
      const float across = cr::hypot (ground.normal[0], ground.normal[2]);
      if (across < 1e-3f)
        return 1.0f - ground.normal[1];
      // The normal leans downhill, so uphill is against its lean.
      constexpr float reach = 12.0f;
      const float ux = -ground.normal[0] / across;
      const float uz = -ground.normal[2] / across;
      return 1.0f - spatial::sample<terrain::terrain_normal> (
                      surface, stone_probe (x + ux * reach, z + uz * reach))
                      .numerical_value_in (one)[1];
    }

    // The body's top stands boulder_squash * 2 - boulder_burial radii above
    // its underside; a stone in water must still break the surface.
    bool stone_breaks_water (float radius, float depth) {
      return depth <= 0.0f ||
             (depth < 0.6f &&
              (2.0f * boulder_squash - boulder_burial) * radius >
                depth + 0.15f);
    }

    float stone_centre_height (float ground, float radius) {
      return ground + (boulder_squash - boulder_burial) * radius;
    }

    // Mostly knee-high stones, with a heavy tail of rare large ones. Cobbles
    // by water run smaller; erratics and scree run larger.
    float stone_radius (std::uint32_t identity, const Rockiness& rock) {
      const float draw = hash_lane (identity, 4);
      float radius = (0.3f + 0.9f * draw * draw) * (1.0f - 0.25f * rock.stream);
      const float large_odds =
        0.03f + 0.15f * rock.erratic + 0.08f * rock.scree;
      if (hash_lane (identity, 5) < large_odds) {
        const float ceiling = std::lerp (2.0f, 3.0f, rock.erratic);
        radius =
          1.2f + (ceiling - 1.2f) * cr::pow (hash_lane (identity, 6), 1.6f);
      }
      return std::max (radius, 0.25f);
    }

    // What lies under a stone's footprint: four points around its rim and
    // its centre.
    struct StoneFootprint {
      float settled; // the ground height the body rests on
      bool clear;    // out of the tread and above deep water
    };

    StoneFootprint stone_footprint (const map::SurfaceGeometry& surface,
                                    const map::SurfaceReadings& readings,
                                    const terrain::WaterSheets& water,
                                    const StoneGround& ground,
                                    float x,
                                    float z,
                                    float radius,
                                    float turn) {
      float lowest = ground.elevation;
      float sum = ground.elevation;
      float deepest = ground.water_depth;
      for (int k = 0; k < 4; ++k) {
        const float angle = turn + 0.25f * boulder_tau * static_cast<float> (k);
        const float px = x + 0.85f * radius * cr::cos (angle);
        const float pz = z + 0.85f * radius * cr::sin (angle);
        if (stone_trail (readings, px, pz) > boulder_trail_tread)
          return { .settled = 0.0f, .clear = false };
        const float height = stone_elevation (surface, px, pz);
        lowest = std::min (lowest, height);
        sum += height;
        deepest =
          std::max (deepest, stone_water_depth (surface, water, px, pz));
      }
      // Neither perched on the highest ground beneath it nor sunk to the
      // lowest: halfway between the lowest and the mean.
      return { .settled = 0.5f * (lowest + 0.2f * sum),
               .clear = stone_breaks_water (radius, deepest) };
    }

    BoulderSite stone_site (float x,
                            float centre_y,
                            float z,
                            const StoneGround& ground,
                            float radius,
                            std::uint32_t identity) {
      return {
        .centre = position (Vec3 (x, centre_y, z)),
        .normal = ground.normal * terrain::terrain_normal[one],
        .radius = radius * u::m,
        .moisture = ground.moisture * map::surface_moisture[one],
        .seed = identity,
      };
    }
  }

  BoulderPlan plan_boulders (const map::SurfaceGeometry& surface,
                             const map::SurfaceReadings& readings,
                             const terrain::WaterSheets& water,
                             std::uint32_t seed,
                             meters_t sea_level,
                             meters_t land_relief) {
    MOPPE_PROFILE_ZONE ("plan_boulders");
    if (land_relief <= 0.0f * u::m)
      throw std::invalid_argument ("Boulder land relief must be positive");
    const terrain::TerrainDomain& domain = surface.domain ();
    const float width = domain.period_x ().numerical_value_in (u::m);
    const float depth = domain.period_z ().numerical_value_in (u::m);
    const float sea = sea_level.numerical_value_in (u::m);
    const float relief = land_relief.numerical_value_in (u::m);
    BoulderPlan plan;
    plan.period = spatial_extent_in_metres (Vec3 (width, 0.0f, depth));

    const auto cells = [] (float extent) {
      return std::max<std::uint32_t> (
        1,
        static_cast<std::uint32_t> (std::round (extent / boulder_cell_metres)));
    };
    const std::uint32_t cells_x = cells (width);
    const std::uint32_t cells_z = cells (depth);
    const float cell_x = width / static_cast<float> (cells_x);
    const float cell_z = depth / static_cast<float> (cells_z);
    // Rocks come in groups: a field of patches a couple of hundred metres
    // across, broken up by a finer one, decides where the ground is stony.
    const auto laps = [] (float extent, float patch_metres) {
      return std::max<std::uint32_t> (
        1, static_cast<std::uint32_t> (std::round (extent / patch_metres)));
    };
    const std::uint32_t broad_laps = laps (width, 180.0f);
    const std::uint32_t fine_laps = laps (width, 45.0f);

    // Rows are independent, so bands of them plan in parallel and join in
    // row order: the plan is the same whatever the thread count.
    const auto plan_row = [&] (std::uint32_t iz,
                               std::vector<BoulderSite>& sites) {
      for (std::uint32_t ix = 0; ix < cells_x; ++ix) {
        const std::uint32_t identity = lattice_hash (ix, iz, seed);
        const float odds = hash_lane (identity, 2);
        if (odds >= boulder_cell_odds)
          continue;
        const float x =
          (static_cast<float> (ix) + hash_lane (identity, 0)) * cell_x;
        const float z =
          (static_cast<float> (iz) + hash_lane (identity, 1)) * cell_z;
        const proportion_t along_x = (x / width) * one;
        const proportion_t along_z = (z / depth) * one;
        const float grouping =
          0.6f * periodic_noise (along_x, along_z, broad_laps, seed ^ 0x60b1u)
                   .numerical_value_in (one) +
          0.4f * periodic_noise (along_x, along_z, fine_laps, seed ^ 0x7c3au)
                   .numerical_value_in (one);
        const float cluster =
          0.12f + 1.6f * smoothstep (0.30f, 0.75f, grouping);
        // Rockiness never exceeds one, so between the groups most candidates
        // are refused before any field is read.
        if (odds >= boulder_cell_odds * std::min (1.0f, cluster))
          continue;

        const StoneGround ground =
          read_stone_ground (surface, readings, water, x, z);
        if (ground.elevation < sea || ground.home > 0.02f ||
            ground.trail > boulder_trail_tread || ground.water_depth > 0.6f)
          continue;
        const Rockiness rock =
          stone_rockiness (ground,
                           steepness_uphill (surface, ground, x, z),
                           (ground.elevation - sea) / relief);
        if (odds >= boulder_cell_odds * std::min (1.0f, rock.total * cluster))
          continue;

        const float radius = stone_radius (identity, rock);
        const StoneFootprint footprint =
          stone_footprint (surface,
                           readings,
                           water,
                           ground,
                           x,
                           z,
                           radius,
                           boulder_tau * hash_lane (identity, 3));
        if (!footprint.clear)
          continue;
        sites.push_back (
          stone_site (x,
                      stone_centre_height (footprint.settled, radius),
                      z,
                      ground,
                      radius,
                      identity));

        // A large rock gathers a few smaller ones that broke from it or
        // lodged against it, nestled close around its foot.
        if (radius < 0.6f)
          continue;
        const int satellites = static_cast<int> (
          hash_lane (identity, 7) * 3.99f * std::min (1.0f, radius / 1.2f));
        for (int k = 0; k < satellites; ++k) {
          const std::uint32_t child = lattice_hash (
            identity, static_cast<std::uint32_t> (k + 1), seed ^ 0x5a7e111u);
          const float small = radius * (0.18f + 0.27f * hash_lane (child, 0));
          const float angle = boulder_tau * hash_lane (child, 1);
          const float reach = 0.95f * radius + 0.6f * small;
          const float sx =
            std::fmod (x + reach * cr::cos (angle) + width, width);
          const float sz =
            std::fmod (z + reach * cr::sin (angle) + depth, depth);
          const StoneGround beside =
            read_stone_ground (surface, readings, water, sx, sz);
          if (beside.elevation < sea || beside.home > 0.02f ||
              beside.trail > boulder_trail_tread)
            continue;
          const StoneFootprint under =
            stone_footprint (surface,
                             readings,
                             water,
                             beside,
                             sx,
                             sz,
                             small,
                             boulder_tau * hash_lane (child, 2));
          if (!under.clear)
            continue;
          sites.push_back (
            stone_site (sx,
                        stone_centre_height (under.settled, small),
                        sz,
                        beside,
                        small,
                        child));
        }
      }
    };
    const std::size_t hardware_threads =
      std::max (1u, std::thread::hardware_concurrency ());
    const std::size_t band_count = std::min<std::size_t> (
      cells_z, std::max<std::size_t> (1, hardware_threads));
    std::vector<std::vector<BoulderSite>> bands (band_count);
    {
      const auto plan_band = [&] (std::size_t band) {
        const auto first =
          static_cast<std::uint32_t> (band * cells_z / band_count);
        const auto last =
          static_cast<std::uint32_t> ((band + 1) * cells_z / band_count);
        for (std::uint32_t iz = first; iz < last; ++iz)
          plan_row (iz, bands[band]);
      };
      // The caller plans the first band, so a host with one hardware
      // thread (a browser page) starts none.
      std::vector<std::jthread> workers;
      workers.reserve (band_count - 1);
      for (std::size_t band = 1; band < band_count; ++band)
        workers.emplace_back (plan_band, band);
      plan_band (0);
    }
    for (const std::vector<BoulderSite>& band : bands)
      plan.sites.insert (plan.sites.end (), band.begin (), band.end ());
    return plan;
  }

  namespace {
    // A buried vertical capsule in the boulder's shape: its upper cap meets
    // the top of the body, and its walls run down into the ground, so a bike
    // meets a rock's flank rather than tripping on a buried sphere.
    mov::Trunk boulder_collider (const BoulderSite& site) {
      const Vec3 centre = position_value (site.centre);
      const float radius = site.radius.numerical_value_in (u::m);
      const float girth = 0.8f * radius;
      const float upper = centre[1] + boulder_squash * radius - girth;
      const float lower = upper - 0.6f * radius;
      return { .root = Vec3 (centre[0], lower - girth, centre[2]),
               .axis = Vec3 (0.0f, 1.0f, 0.0f),
               .height = upper - lower + girth,
               .radius = girth };
    }
  }

  void BoulderLandscape::rebuild (render::Renderer& renderer,
                                  const BoulderPlan& plan) {
    MOPPE_PROFILE_ZONE ("BoulderLandscape::rebuild");
    std::vector<render::BoulderInstance> instances;
    instances.reserve (plan.sites.size ());
    m_colliders.clear ();
    for (const BoulderSite& site : plan.sites) {
      instances.push_back ({
        .centre = site.centre,
        .ground_normal = site.normal,
        .radius = site.radius,
        .moisture = site.moisture.numerical_value_in (one) * one,
        .seed = site.seed,
      });
      if (site.radius >= boulder_collider_radius * u::m)
        m_colliders.push_back (boulder_collider (site));
    }
    renderer.set_boulders ({ .period = plan.period }, instances);
    m_boulder_count = instances.size ();
    std::cerr << "moppe: boulders: " << m_boulder_count << " rocks, "
              << m_colliders.size () << " solid" << std::endl;
  }

  void BoulderLandscape::draw (render::Renderer& renderer) const {
    if (m_boulder_count)
      renderer.draw_boulders ();
  }
}
