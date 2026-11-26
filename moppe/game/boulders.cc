#include <moppe/correct_math.hh>
#include <moppe/game/boulders.hh>
#include <moppe/game/river_stones.hh>

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
    constexpr meters_t boulder_cell_spacing = 6.0f * u::m;
    constexpr probability_t boulder_cell_odds = 0.70f * probability[one];
    // Stones smaller than this are ridden over rather than into.
    constexpr meters_t boulder_collider_radius = 0.5f * u::m;
    // The tread a rider follows; no part of a rock's footprint enters it.
    constexpr map::TrailInfluence boulder_trail_tread =
      0.75f * map::trail_influence[one];
    constexpr radians_t boulder_tau = 6.2831853f * u::rad;

    // Every field a candidate stone consults, read at one place.
    struct StoneGround {
      terrain::SurfaceElevation elevation;
      terrain::TerrainNormal normal;
      map::ErosionExposure erosion;
      map::DepositionCover deposition;
      map::ForestCover forest;
      map::SurfaceMoisture moisture;
      map::TrailInfluence trail;
      terrain::HomeBaseInfluence home;
      terrain::WaterlineDistance waterline;
      // Still or running water's level above the ground; below it on land.
      terrain::ElevationChange water_depth;
      // Running water within reach, which lays its own stones.
      bool by_running_water;
    };

    position_t stone_probe (const plan_position_t& at) {
      return at_height (at, 0.0f * u::m);
    }

    terrain::SurfaceElevation
    stone_elevation (const map::SurfaceGeometry& surface,
                     const plan_position_t& at) {
      return spatial::sample<terrain::surface_elevation> (surface,
                                                          stone_probe (at));
    }

    terrain::ElevationChange
    stone_water_depth (const map::SurfaceGeometry& surface,
                       const terrain::WaterSheets& water,
                       const plan_position_t& at) {
      const terrain::SurfaceElevation level =
        spatial::sample<terrain::surface_elevation> (water, stone_probe (at));
      return level - stone_elevation (surface, at);
    }

    map::TrailInfluence stone_trail (const map::SurfaceReadings& readings,
                                     const plan_position_t& at) {
      return spatial::sample<map::trail_influence> (readings, stone_probe (at));
    }

    // Every lattice cell within a waterline band's reach of a current. The
    // river planner places the stones there by what the current can move,
    // so the generic shore cobbles keep to still water.
    std::vector<std::uint8_t>
    running_water_reach (const map::SurfaceGeometry& surface,
                         const terrain::WaterSheets& water) {
      const terrain::TerrainDomain& domain = surface.domain ();
      const int width = static_cast<int> (domain.width ());
      const int height = static_cast<int> (domain.height ());
      const auto& ground = spatial::get<terrain::surface_elevation> (surface);
      const auto& level = spatial::get<terrain::surface_elevation> (water);
      const auto& current = spatial::get<terrain::water_velocity> (water);
      const int reach = static_cast<int> (
        std::ceil (8.0f / domain.spacing_x ().numerical_value_in (u::m)));
      std::vector<std::uint8_t> running (domain.size (), 0);
      std::vector<std::uint8_t> near (domain.size (), 0);
      for (std::size_t cell = 0; cell < domain.size (); ++cell) {
        const Vec3 flow = current[cell].numerical_value_in (u::m / u::s);
        running[cell] = level[cell] > ground[cell] &&
                        flow[0] * flow[0] + flow[2] * flow[2] > 0.03f * 0.03f;
      }
      // Rows, then columns: a square of the reach around each current.
      std::vector<std::uint8_t> rows (domain.size (), 0);
      for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x)
          if (running[static_cast<std::size_t> (z) * width + x])
            for (int dx = -reach; dx <= reach; ++dx)
              rows[static_cast<std::size_t> (z) * width +
                   terrain::wrap_index (x + dx, width)] = 1;
      for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x)
          if (rows[static_cast<std::size_t> (z) * width + x])
            for (int dz = -reach; dz <= reach; ++dz)
              near[static_cast<std::size_t> (
                     terrain::wrap_index (z + dz, height)) *
                     width +
                   x] = 1;
      return near;
    }

    std::size_t nearest_cell (const terrain::TerrainDomain& domain,
                              const plan_position_t& at) {
      const auto nearest =
        [] (meters_t along, meters_t spacing, std::size_t count) {
          return static_cast<std::size_t> (
            terrain::wrap_index (static_cast<int> (std::lround (
                                   (along / spacing).numerical_value_in (one))),
                                 static_cast<int> (count)));
        };
      return domain.offset (
        { .column = nearest (x_of (at), domain.spacing_x (), domain.width ()),
          .row = nearest (z_of (at), domain.spacing_z (), domain.height ()) });
    }

    StoneGround read_stone_ground (const map::SurfaceGeometry& surface,
                                   const map::SurfaceReadings& readings,
                                   const terrain::WaterSheets& water,
                                   const std::vector<std::uint8_t>& running,
                                   const plan_position_t& at) {
      const position_t where = stone_probe (at);
      const auto read = [&] (auto quantity) {
        return spatial::sample<quantity> (readings, where);
      };
      return {
        .elevation = stone_elevation (surface, at),
        .normal = spatial::sample<terrain::terrain_normal> (surface, where),
        .erosion = read (map::erosion_exposure),
        .deposition = read (map::deposition_cover),
        .forest = read (map::forest_cover),
        .moisture = read (map::surface_moisture),
        .trail = read (map::trail_influence),
        .home = read (map::home_base_influence),
        .waterline = read (map::waterline_distance),
        .water_depth = stone_water_depth (surface, water, at),
        .by_running_water = running[nearest_cell (surface.domain (), at)] != 0,
      };
    }

    // How much loose rock the ground holds, and which story put it there.
    struct Rockiness {
      proportion_t total;
      proportion_t erratic; // share of the answer owed to high open ground
      proportion_t scree;   // share owed to a cliff above
      proportion_t stream;  // share owed to running or standing water nearby
    };

    // The ground's steepness as one less the normal's upward lane.
    terrain::slope_t steepness_of (const terrain::TerrainNormal& normal) {
      return (1.0f - normal.numerical_value_ref_in (one)[1]) *
             terrain::terrain_slope[one];
    }

    Rockiness stone_rockiness (const StoneGround& ground,
                               terrain::slope_t steepness_above,
                               proportion_t altitude) {
      const terrain::slope_t steepness = steepness_of (ground.normal);
      constexpr proportion_t whole = 1.0f * proportion[one];
      // Talus: frost and erosion break rock out of steep slopes, but it
      // cannot rest on a face much past its angle of repose.
      const proportion_t talus = quantity_cast<proportion> (
        0.8f * band (0.05f, 0.20f, steepness) *
        (0.15f * proportion[one] + 0.85f * ground.erosion) *
        (whole - band (0.45f, 0.62f, steepness)));
      // Scree: what falls from a cliff collects on the gentler ground
      // below it, where the world laid material down.
      const proportion_t scree = quantity_cast<proportion> (
        band (0.18f, 0.40f, steepness_above) *
        (whole - band (0.25f, 0.45f, steepness)) *
        (0.45f * proportion[one] + 0.55f * ground.deposition));
      // Erratics: the ice left single stones lying on the high open ground,
      // while deep high-altitude snowfields bury knee-high stones beneath
      // packed drift, leaving scattered outcrops along steeper fell margins.
      const proportion_t deep_snow = quantity_cast<proportion> (
        band (0.68f * proportion[one], 0.82f * proportion[one], altitude) *
        (whole - band (0.12f, 0.24f, steepness)));
      const proportion_t erratic = quantity_cast<proportion> (
        0.45f *
        band (0.45f * proportion[one], 0.80f * proportion[one], altitude) *
        (whole - ground.forest) * (whole - band (0.15f, 0.35f, steepness)) *
        (whole - 0.84f * deep_snow));
      // Cobbles along a shore, more where it deposits.
      proportion_t stream = 0.0f * proportion[one];
      if (!ground.by_running_water)
        stream = quantity_cast<proportion> (
          (whole - band (0.5f * u::m, 7.5f * u::m, ground.waterline)) *
          (0.35f * proportion[one] + 0.65f * ground.deposition));
      // A lush, level meadow has buried what rock it had.
      const proportion_t lush = quantity_cast<proportion> (
        band (
          0.45f * proportion[one], 0.80f * proportion[one], ground.moisture) *
        (whole - band (0.03f, 0.12f, steepness)) * (whole - ground.erosion));
      const proportion_t dry = quantity_cast<proportion> (
        std::max ({ talus, scree, erratic }) * (whole - 0.7f * lush));
      const proportion_t rock = std::max (dry, stream);
      const proportion_t total = quantity_cast<proportion> (
        (rock + 0.015f * proportion[one]) * (whole - 0.85f * ground.forest));
      const proportion_t sum =
        std::max (talus + scree + erratic + stream, 1e-4f * proportion[one]);
      return { .total = std::clamp (
                 total, 0.0f * proportion[one], 1.0f * proportion[one]),
               .erratic = quantity_cast<proportion> (erratic / sum),
               .scree = quantity_cast<proportion> (scree / sum),
               .stream = quantity_cast<proportion> (stream / sum) };
    }

    // How much steeper the ground is a little way uphill: a cliff above
    // shows as a steep reading there.
    terrain::slope_t steepness_uphill (const map::SurfaceGeometry& surface,
                                       const StoneGround& ground,
                                       const plan_position_t& at) {
      const Vec3& normal = ground.normal.numerical_value_ref_in (one);
      const float across = cr::hypot (normal[0], normal[2]);
      if (across < 1e-3f)
        return steepness_of (ground.normal);
      // The normal leans downhill, so uphill is against its lean.
      constexpr meters_t reach = 12.0f * u::m;
      const plan_direction_t uphill =
        plan_unit_direction (Vec2 (-normal[0] / across, -normal[2] / across));
      return steepness_of (spatial::sample<terrain::terrain_normal> (
        surface, stone_probe (at + along (uphill, reach))));
    }

    // The body's top stands boulder_squash * 2 - boulder_burial radii above
    // its underside; a stone in water must still break the surface.
    bool stone_breaks_water (meters_t radius, terrain::ElevationChange depth) {
      return depth <= 0.0f * u::m ||
             (depth < 0.6f * u::m &&
              meters_t ((2.0f * boulder_squash - boulder_burial) * radius) >
                depth + 0.15f * u::m);
    }

    terrain::SurfaceElevation
    stone_centre_height (terrain::SurfaceElevation ground, meters_t radius) {
      return ground + terrain::ElevationChange (quantity_cast<isq::length> (
                        (boulder_squash - boulder_burial) * radius));
    }

    // Mostly knee-high stones, with a heavy tail of rare large ones. Cobbles
    // on a shore run smaller; erratics and scree run larger.
    meters_t stone_radius (std::uint32_t identity, const Rockiness& rock) {
      const proportion_t draw = hash_lane (identity, 4) * proportion[one];
      // The draw shaped into a size: from 0.3 of a metre to 1.2.
      const scale_factor_t size =
        0.3f * scale_factor[one] +
        quantity_cast<scale_factor> (
          quantity_cast<dimensionless> (0.9f * draw * draw));
      meters_t radius =
        meters_t (size * (1.0f * proportion[one] - 0.25f * rock.stream) * u::m);
      const probability_t large_odds =
        quantity_cast<probability> (quantity_cast<dimensionless> (
          0.03f * proportion[one] + 0.15f * rock.erratic + 0.08f * rock.scree));
      if (hash_lane (identity, 5) * probability[one] < large_odds) {
        const meters_t ceiling =
          std::lerp (2.0f, 3.0f, rock.erratic.numerical_value_in (one)) * u::m;
        radius = 1.2f * u::m + (ceiling - 1.2f * u::m) *
                                 cr::pow (hash_lane (identity, 6), 1.6f);
      }
      return std::max (radius, 0.25f * u::m);
    }

    // What lies under a stone's footprint: four points around its rim and
    // its centre.
    struct StoneFootprint {
      // the ground height the body rests on
      terrain::SurfaceElevation settled;
      bool clear; // out of the tread and above deep water
    };

    StoneFootprint stone_footprint (const map::SurfaceGeometry& surface,
                                    const map::SurfaceReadings& readings,
                                    const terrain::WaterSheets& water,
                                    const StoneGround& ground,
                                    const plan_position_t& at,
                                    meters_t radius,
                                    radians_t turn) {
      // Heights measured from the vertical frame's zero, so they sum.
      terrain::ElevationChange lowest = ground.elevation.delta_from_zero ();
      terrain::ElevationChange sum = lowest;
      terrain::ElevationChange deepest = ground.water_depth;
      for (int k = 0; k < 4; ++k) {
        const radians_t angle =
          turn + 0.25f * boulder_tau * static_cast<float> (k);
        const plan_position_t rim =
          at +
          plan_displacement (0.85f * radius * cr::cos (radians_value (angle)),
                             0.85f * radius * cr::sin (radians_value (angle)));
        if (stone_trail (readings, rim) > boulder_trail_tread)
          return { .settled = {}, .clear = false };
        const terrain::ElevationChange height =
          stone_elevation (surface, rim).delta_from_zero ();
        lowest = std::min (lowest, height);
        sum += height;
        deepest = std::max (deepest, stone_water_depth (surface, water, rim));
      }
      // Neither perched on the highest ground beneath it nor sunk to the
      // lowest: halfway between the lowest and the mean.
      return { .settled =
                 terrain::SurfaceElevation (0.5f * (lowest + 0.2f * sum)),
               .clear = stone_breaks_water (radius, deepest) };
    }

    BoulderSite stone_site (const plan_position_t& at,
                            terrain::SurfaceElevation centre,
                            const StoneGround& ground,
                            meters_t radius,
                            std::uint32_t identity) {
      return {
        .centre = terrain::at_elevation (at, centre),
        .normal = ground.normal,
        .radius = radius,
        .moisture = ground.moisture,
        .seed = identity,
      };
    }
  }

  BoulderPlan plan_boulders (const map::SurfaceGeometry& surface,
                             const map::SurfaceReadings& readings,
                             const terrain::WaterSheets& water,
                             const terrain::RiverNetwork& rivers,
                             std::uint32_t seed,
                             terrain::SurfaceElevation sea_level,
                             meters_t land_relief) {
    MOPPE_PROFILE_ZONE ("plan_boulders");
    if (land_relief <= 0.0f * u::m)
      throw std::invalid_argument ("Boulder land relief must be positive");
    const terrain::TerrainDomain& domain = surface.domain ();
    const meters_t width = domain.period_x ();
    const meters_t depth = domain.period_z ();
    BoulderPlan plan;
    plan.period = spatial_extent_in_metres (
      Vec3 (meters_value (width), 0.0f, meters_value (depth)));
    const std::vector<std::uint8_t> running =
      running_water_reach (surface, water);

    const meters_t largest_side = std::max (width, depth);
    const meters_t effective_boulder_spacing =
      largest_side > 10000.0f * u::m
        ? boulder_cell_spacing * (meters_value (largest_side) / 10000.0f)
        : boulder_cell_spacing;
    const auto cells = [effective_boulder_spacing] (meters_t extent) {
      return std::max<std::uint32_t> (
        1,
        static_cast<std::uint32_t> (std::round (
          (extent / effective_boulder_spacing).numerical_value_in (one))));
    };
    const std::uint32_t cells_x = cells (width);
    const std::uint32_t cells_z = cells (depth);
    const meters_t cell_x = width / static_cast<float> (cells_x);
    const meters_t cell_z = depth / static_cast<float> (cells_z);
    // Rocks come in groups: a field of patches a couple of hundred metres
    // across, broken up by a finer one, decides where the ground is stony.
    const auto laps = [] (meters_t extent, meters_t patch) {
      return std::max<std::uint32_t> (
        1,
        static_cast<std::uint32_t> (
          std::round ((extent / patch).numerical_value_in (one))));
    };
    const std::uint32_t broad_laps = laps (width, 180.0f * u::m);
    const std::uint32_t fine_laps = laps (width, 45.0f * u::m);
    // A satellite's place, wrapped back onto the torus as the planner
    // always has: one lap added, then the remainder.
    const auto wrapped = [] (meters_t along, meters_t period) {
      return std::fmod (meters_value (along), meters_value (period)) * u::m;
    };

    // Rows are independent, so bands of them plan in parallel and join in
    // row order: the plan is the same whatever the thread count.
    const auto plan_row = [&] (std::uint32_t iz,
                               std::vector<BoulderSite>& sites) {
      for (std::uint32_t ix = 0; ix < cells_x; ++ix) {
        const std::uint32_t identity = lattice_hash (ix, iz, seed);
        const probability_t odds = hash_lane (identity, 2) * probability[one];
        if (odds >= boulder_cell_odds)
          continue;
        const plan_position_t at = plan_position (
          (static_cast<float> (ix) + hash_lane (identity, 0)) * cell_x,
          (static_cast<float> (iz) + hash_lane (identity, 1)) * cell_z);
        const proportion_t along_x = x_of (at) / width;
        const proportion_t along_z = z_of (at) / depth;
        const noise_signal_t grouping =
          0.6f * periodic_noise (along_x, along_z, broad_laps, seed ^ 0x60b1u) +
          0.4f * periodic_noise (along_x, along_z, fine_laps, seed ^ 0x7c3au);
        // How strongly the groups gather stone here: past one at their
        // hearts.
        const scale_factor_t cluster =
          0.12f * scale_factor[one] +
          quantity_cast<scale_factor> (quantity_cast<dimensionless> (
            1.6f * band (0.30f * noise_signal[one],
                         0.75f * noise_signal[one],
                         grouping)));
        const scale_factor_t whole = 1.0f * scale_factor[one];
        // Rockiness never exceeds one, so between the groups most candidates
        // are refused before any field is read.
        if (odds >= quantity_cast<probability> (boulder_cell_odds *
                                                std::min (whole, cluster)))
          continue;

        const StoneGround ground =
          read_stone_ground (surface, readings, water, running, at);
        if (ground.elevation < sea_level ||
            ground.home > 0.02f * proportion[one] ||
            ground.trail > boulder_trail_tread ||
            ground.water_depth > 0.6f * u::m)
          continue;
        const Rockiness rock = stone_rockiness (
          ground,
          steepness_uphill (surface, ground, at),
          quantity_cast<proportion> (meters_t (ground.elevation - sea_level) /
                                     land_relief));
        if (odds >=
            quantity_cast<probability> (
              boulder_cell_odds * std::min (whole,
                                            quantity_cast<scale_factor> (
                                              quantity_cast<dimensionless> (
                                                rock.total * cluster)))))
          continue;

        const meters_t radius = stone_radius (identity, rock);
        const StoneFootprint footprint =
          stone_footprint (surface,
                           readings,
                           water,
                           ground,
                           at,
                           radius,
                           boulder_tau * hash_lane (identity, 3));
        if (!footprint.clear)
          continue;
        sites.push_back (
          stone_site (at,
                      stone_centre_height (footprint.settled, radius),
                      ground,
                      radius,
                      identity));

        // A large rock gathers a few smaller ones that broke from it or
        // lodged against it, nestled close around its foot.
        if (radius < 0.6f * u::m)
          continue;
        const int satellites = static_cast<int> (
          hash_lane (identity, 7) * 3.99f *
          std::min (1.0f, (radius / (1.2f * u::m)).numerical_value_in (one)));
        for (int k = 0; k < satellites; ++k) {
          const std::uint32_t child = lattice_hash (
            identity, static_cast<std::uint32_t> (k + 1), seed ^ 0x5a7e111u);
          const meters_t small =
            radius * (0.18f + 0.27f * hash_lane (child, 0));
          const radians_t angle = boulder_tau * hash_lane (child, 1);
          const meters_t reach = 0.95f * radius + 0.6f * small;
          const plan_position_t beside_at = plan_position (
            wrapped (x_of (at) + reach * cr::cos (radians_value (angle)) +
                       width,
                     width),
            wrapped (z_of (at) + reach * cr::sin (radians_value (angle)) +
                       depth,
                     depth));
          const StoneGround beside =
            read_stone_ground (surface, readings, water, running, beside_at);
          if (beside.elevation < sea_level ||
              beside.home > 0.02f * proportion[one] ||
              beside.trail > boulder_trail_tread)
            continue;
          const StoneFootprint under =
            stone_footprint (surface,
                             readings,
                             water,
                             beside,
                             beside_at,
                             small,
                             boulder_tau * hash_lane (child, 2));
          if (!under.clear)
            continue;
          sites.push_back (
            stone_site (beside_at,
                        stone_centre_height (under.settled, small),
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
    RiverStones river =
      plan_river_stones (surface, readings, water, rivers, seed ^ 0x51e7u);
    plan.sites.insert (
      plan.sites.end (), river.sites.begin (), river.sites.end ());
    plan.crossings = std::move (river.crossings);
    return plan;
  }

  namespace {
    // A buried vertical capsule in the boulder's shape: its upper cap meets
    // the top of the body, and its walls run down into the ground, so a bike
    // meets a rock's flank rather than tripping on a buried sphere. A stone
    // made solid to be stood on takes nearly its whole breadth, which
    // follows its flattened top more closely, so that feet passing from one
    // stepping stone to the next find a footing in the crease between them.
    mov::Trunk boulder_collider (const BoulderSite& site) {
      const meters_t height = position_value (site.centre)[1] * u::m;
      const meters_t girth = (site.solid ? 0.95f : 0.8f) * site.radius;
      const meters_t upper =
        height + meters_t (boulder_squash * site.radius) - girth;
      const meters_t lower = upper - 0.6f * site.radius;
      return { .root = at_height (plan (site.centre), lower - girth),
               .axis = unit_direction (Vec3 (0.0f, 1.0f, 0.0f)),
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
        .moisture = site.moisture,
        .seed = site.seed,
      });
      if (site.solid || site.radius >= boulder_collider_radius)
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
