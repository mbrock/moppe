#include <moppe/game/forest_plan.hh>

#include <moppe/correct_math.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace moppe::game {
  namespace {
    constexpr std::uint64_t forest_plan_magic = 0x4d4f505045465253ULL;
    constexpr std::uint32_t forest_plan_version = 21;

    // Marginal woodland stays close to the old proposal density while the
    // most suitable habitat can form a genuinely closed spruce stand. The
    // hard-core pass remains the physical upper bound. Proposals themselves
    // are a uniform deterministic stream, not one draw per square, so no
    // planting lattice exists to survive the rejection.
    constexpr scale_factor_t forest_proposal_scale_min =
      0.55f * scale_factor[one];
    constexpr scale_factor_t forest_proposal_scale_max =
      0.95f * scale_factor[one];
    // The hard core's radius, as a share of the planting spacing.
    constexpr scale_factor_t forest_exclusion_ratio = 0.40f * scale_factor[one];

    struct ForestPlanHeader {
      std::uint64_t magic;
      std::uint32_t version;
      std::uint32_t seed;
      std::array<float, 3> period;
      std::uint32_t reserved;
      std::uint64_t site_count;
    };

    struct ForestSiteRecord {
      std::array<float, 3> position;
      std::array<float, 3> normal;
      float cover;
      float moisture;
      float size;
      std::uint32_t seed;
      std::uint32_t form;
      std::uint32_t age;
    };

    static_assert (std::is_trivially_copyable_v<ForestPlanHeader>);
    static_assert (std::is_trivially_copyable_v<ForestSiteRecord>);
    static_assert (sizeof (ForestPlanHeader) == 40);
    static_assert (sizeof (ForestSiteRecord) == 48);

    struct ForestCandidate {
      meters_t x;
      meters_t z;
      map::ForestCover cover;
      map::SurfaceMoisture moisture;
      proportion_t grove;
      bool pioneer; // admitted by birch's own open-ground population
      bool open;    // admitted by the open trees' own population
      bool snag;    // the old stand a fire killed, still standing
      // What the canopy's competition left each tree here, and the bog.
      map::SpruceAbundance spruce;
      map::PineAbundance pine;
      map::BirchAbundance birch;
      map::MountainBirchAbundance fell;
      map::SphagnumAbundance bog;
      // How far the ground has come back since it was last opened.
      map::StandRecovery recovery;
      proportion_t priority;
      std::uint32_t identity;
    };

    // A uniform draw from [0, 1) off one of a site's hash lanes.
    proportion_t draw (std::uint32_t identity, std::uint32_t lane) {
      return hash_lane (identity, lane) * proportion[one];
    }

    // Where birch may dominate: a coherent field of patches from roughly
    // eighty to two hundred metres across, so birches arrive as groves
    // rather than as a sprinkle through the spruce.
    proportion_t birch_grove (meters_t x,
                              meters_t z,
                              meters_t width,
                              meters_t depth,
                              std::uint32_t seed) {
      const proportion_t along_x = x / width;
      const proportion_t along_z = z / depth;
      const auto laps = [] (meters_t extent, meters_t patch) {
        return std::max<std::uint32_t> (
          1,
          static_cast<std::uint32_t> (
            std::round ((extent / patch).numerical_value_in (one))));
      };
      const noise_signal_t field =
        0.65f *
          periodic_noise (
            along_x, along_z, laps (width, 200.0f * u::m), seed ^ 0x5b1e) +
        0.35f * periodic_noise (
                  along_x, along_z, laps (depth, 80.0f * u::m), seed ^ 0x8c3d);
      return band (0.54f * noise_signal[one], 0.68f * noise_signal[one], field);
    }

    proportion_t wetness (map::SurfaceMoisture moisture) {
      return band (0.30f * map::surface_moisture[one],
                   0.70f * map::surface_moisture[one],
                   moisture);
    }

    // Birch is the pioneer: it holds the edges of the spruce, wet ground,
    // and open land inside its groves. A few birches stand anywhere.
    bool is_birch (const ForestCandidate& candidate) {
      if (candidate.pioneer)
        return true;
      const proportion_t edge =
        1.0f * proportion[one] - band (0.15f * map::forest_cover[one],
                                       0.55f * map::forest_cover[one],
                                       candidate.cover);
      const probability_t odds =
        (0.03f +
         candidate.grove.numerical_value_in (one) *
           (0.30f + 0.70f * std::max (edge, 0.8f * wetness (candidate.moisture))
                              .numerical_value_in (one))) *
        probability[one];
      return draw (candidate.identity, 7).numerical_value_in (one) <
             odds.numerical_value_in (one);
    }

    position_t sample_position (meters_t x, meters_t z) {
      return position (
        Vec3 (x.numerical_value_in (u::m), 0, z.numerical_value_in (u::m)));
    }

    terrain::SurfaceElevation
    elevation_at (const map::SurfaceGeometry& surface, meters_t x, meters_t z) {
      return spatial::sample<terrain::surface_elevation> (
        surface, sample_position (x, z));
    }

    terrain::TerrainNormal
    normal_at (const map::SurfaceGeometry& surface, meters_t x, meters_t z) {
      return spatial::sample<terrain::terrain_normal> (surface,
                                                       sample_position (x, z));
    }

    map::ForestCover
    cover_at (const map::SurfaceReadings& readings, meters_t x, meters_t z) {
      return spatial::sample<map::forest_cover> (readings,
                                                 sample_position (x, z));
    }

    map::SurfaceMoisture
    moisture_at (const map::SurfaceReadings& readings, meters_t x, meters_t z) {
      return spatial::sample<map::surface_moisture> (readings,
                                                     sample_position (x, z));
    }

    // A tree needs dry roots: the trunk's foot and a ring about its root
    // plate must all stand above the painted water. The ring keeps trunks
    // off a bank so steep that their crowns would lean out over the stream.
    bool roots_dry (const map::SurfaceGeometry& surface,
                    const terrain::WaterSheets& water,
                    meters_t x,
                    meters_t z) {
      constexpr meters_t root_plate = 1.6f * u::m;
      constexpr terrain::ElevationChange wet_depth =
        0.02f * terrain::surface_elevation[u::m];
      constexpr std::array<std::array<float, 2>, 7> probes { {
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.5f, 0.866f },
        { -0.5f, 0.866f },
        { -1.0f, 0.0f },
        { -0.5f, -0.866f },
        { 0.5f, -0.866f },
      } };
      for (const auto& [dx, dz] : probes) {
        const meters_t px = x + root_plate * dx;
        const meters_t pz = z + root_plate * dz;
        const position_t where = sample_position (px, pz);
        const terrain::SurfaceElevation ground =
          spatial::sample<terrain::surface_elevation> (surface, where);
        const terrain::SurfaceElevation level =
          spatial::sample<terrain::surface_elevation> (water, where);
        if (level - ground > wet_depth)
          return false;
      }
      return true;
    }

    position_t forest_position (meters_t x,
                                terrain::SurfaceElevation elevation,
                                meters_t z) {
      return position (
        Vec3 (x.numerical_value_in (u::m),
              elevation.delta_from_zero ().numerical_value_in (u::m),
              z.numerical_value_in (u::m)));
    }

    bool same_extent (const std::array<float, 3>& stored,
                      const spatial_extent_t& expected) {
      const Vec3 value = extent_value (expected);
      return stored[0] == value[0] && stored[1] == value[1] &&
             stored[2] == value[2];
    }

    template <typename T>
    void write_record (std::ofstream& output, const T& value) {
      output.write (reinterpret_cast<const char*> (&value), sizeof (value));
      if (!output)
        throw std::runtime_error ("could not write forest plan");
    }

    // A stand is no older than the ground under it has had to grow one:
    // on a burn of a decade the trees are all saplings, and by a century
    // every age is there again.
    ForestAge age_from_identity (std::uint32_t identity,
                                 map::StandRecovery recovery) {
      // Young ground draws from the younger end of the ages only.
      const proportion_t age_draw =
        draw (identity, 6) * std::lerp (0.2f,
                                        1.0f,
                                        band (0.03f * map::stand_recovery[one],
                                              0.6f * map::stand_recovery[one],
                                              recovery)
                                          .numerical_value_in (one));
      if (age_draw < 0.18f * proportion[one])
        return ForestAge::Sapling;
      if (age_draw < 0.46f * proportion[one])
        return ForestAge::Young;
      if (age_draw < 0.94f * proportion[one])
        return ForestAge::Mature;
      return ForestAge::Ancient;
    }

    TreeSizeFactor size_for_age (ForestAge age, std::uint32_t identity) {
      // Each age spans its own range of sizes; the draw says where in it.
      const proportion_t along = draw (identity, 4);
      const auto between = [along] (float least, float range) {
        return least * tree_size_factor[one] +
               range * along.numerical_value_in (one) * tree_size_factor[one];
      };
      switch (age) {
      case ForestAge::Sapling:
        return between (0.18f, 0.27f);
      case ForestAge::Young:
        return between (0.48f, 0.32f);
      case ForestAge::Mature:
        return between (0.78f, 0.42f);
      case ForestAge::Ancient:
        return between (1.25f, 0.45f);
      }
      return 1.0f * tree_size_factor[one];
    }
  }

  ForestPlan plan_global_forest (const map::SurfaceGeometry& surface,
                                 const map::SurfaceReadings& readings,
                                 const terrain::WaterSheets& water,
                                 std::uint32_t seed,
                                 meters_t spacing) {
    if (spacing <= 0.0f * u::m)
      throw std::invalid_argument ("Forest spacing must be positive");
    const terrain::TerrainDomain& domain = surface.domain ();
    ForestPlan plan;
    const meters_t width = domain.period_x ();
    const meters_t depth = domain.period_z ();
    plan.period = spatial_extent_in_metres (Vec3 (
      width.numerical_value_in (u::m), 0, depth.numerical_value_in (u::m)));
    // Draw a deterministic uniform proposal stream over the whole torus, then
    // use habitat-weighted selection and Matérn-style priority thinning. The
    // count matches the earlier fine proposal density, but positions have no
    // cell boundaries or preferred row and column for the hard-core pass to
    // inherit.
    // Until forest planting is streamed per tile (docs/large-world.md), widen
    // the global planting pitch on worlds larger than 10 km so memory stays
    // bounded on 16 GiB machines while 10 km and smaller worlds stay exact.
    const meters_t largest_side = std::max (width, depth);
    const meters_t effective_spacing =
      largest_side > 10000.0f * u::m
        ? spacing * std::pow (meters_value (largest_side) / 10000.0f, 0.78f)
        : spacing;
    const meters_t proposal_spacing = effective_spacing / std::sqrt (2.0f);
    const std::uint64_t proposal_count = std::max<std::uint64_t> (
      1,
      static_cast<std::uint64_t> (
        std::ceil ((width / proposal_spacing).numerical_value_in (one) *
                   (depth / proposal_spacing).numerical_value_in (one))));
    std::vector<ForestCandidate> candidates;
    candidates.reserve (static_cast<std::size_t> (proposal_count / 12));

    for (std::uint64_t proposal = 0; proposal < proposal_count; ++proposal) {
      const std::uint32_t identity =
        lattice_hash (static_cast<std::uint32_t> (proposal),
                      static_cast<std::uint32_t> (proposal >> 32),
                      seed);
      const meters_t x = hash_lane (identity, 0) * width;
      const meters_t z = hash_lane (identity, 1) * depth;
      const map::ForestCover cover = cover_at (readings, x, z);
      proportion_t population = band (
        0.08f * map::forest_cover[one], 0.62f * map::forest_cover[one], cover);
      const proportion_t grove = birch_grove (x, z, width, depth, seed);
      // Inside a birch grove, wet open ground carries a sparse stand of its
      // own: spread-out birches over a meadow the spruce never reaches. Only
      // the spruce mosaic is waived; habitat, routes, and settlements still
      // keep their ground clear.
      map::SurfaceMoisture moisture = 0.0f * map::surface_moisture[one];
      bool pioneer = false;
      // What the readings hold here.
      const auto held = [&] (auto quantity) {
        return spatial::sample<quantity> (readings, sample_position (x, z));
      };
      // Away from the trails and the home base: what a route leaves open.
      const auto unrouted = [&] {
        return (1.0f - held (map::trail_influence).numerical_value_in (one)) *
               (1.0f -
                held (map::home_base_influence).numerical_value_in (one)) *
               proportion[one];
      };
      if (grove > 0.0f * proportion[one]) {
        const proportion_t habitat = band (0.25f * map::tree_habitat[one],
                                           0.60f * map::tree_habitat[one],
                                           held (map::tree_habitat));
        const proportion_t open = unrouted ();
        moisture = moisture_at (readings, x, z);
        const proportion_t pioneer_population =
          0.22f * grove.numerical_value_in (one) *
          wetness (moisture).numerical_value_in (one) *
          habitat.numerical_value_in (one) *
          std::clamp (open.numerical_value_in (one), 0.0f, 1.0f) *
          proportion[one];
        pioneer = pioneer_population > population;
        population = std::max (population, pioneer_population);
      }
      // Pine and the fell birch stand open where they hold the ground --
      // over the lichen, on the bog, along the treeline -- so they come
      // up at a population of their own, low enough to ride between.
      const map::SpruceAbundance spruce = held (map::spruce_abundance);
      const map::PineAbundance pine = held (map::pine_abundance);
      const map::BirchAbundance birch = held (map::birch_abundance);
      const map::MountainBirchAbundance fell =
        held (map::mountain_birch_abundance);
      const map::SphagnumAbundance bog = held (map::sphagnum_abundance);
      const map::StandRecovery recovery = held (map::stand_recovery);
      // On a burn the stand the fire killed still stands as snags, as
      // thick as it grew, and falls over the decades after: most of it is
      // down in thirty years and nearly all in a century.
      const map::BurnRecency burn = held (map::burn_recency);
      const bool burnt = burn > 1e-6f * map::burn_recency[one];
      const age_t since_fire =
        (burnt ? -12.0f * cr::log (burn.numerical_value_in (one))
               : std::numeric_limits<float>::infinity ()) *
        age[astronomy::Julian_year];
      const proportion_t snag_population =
        (burnt ? 0.42f * cr::exp (
                           (-since_fire / (30.0f * age[astronomy::Julian_year]))
                             .numerical_value_in (one))
               : 0.0f) *
        proportion[one];
      const proportion_t routes = unrouted ();
      const proportion_t open_population =
        (0.34f * pine.numerical_value_in (one) +
         0.32f * fell.numerical_value_in (one)) *
        std::clamp (routes.numerical_value_in (one), 0.0f, 1.0f) *
        proportion[one];
      const bool open = open_population > population;
      if (open) {
        population = open_population;
        pioneer = false;
      }
      const bool snag =
        draw (identity, 11) *
          (population + snag_population).numerical_value_in (one) <
        snag_population;
      population =
        std::min (population + snag_population, 1.0f * proportion[one]);
      const scale_factor_t proposal_scale =
        std::lerp (forest_proposal_scale_min.numerical_value_in (one),
                   forest_proposal_scale_max.numerical_value_in (one),
                   population.numerical_value_in (one)) *
        scale_factor[one];
      // The chance this proposal is planted at all.
      const probability_t planted = population.numerical_value_in (one) *
                                    proposal_scale.numerical_value_in (one) *
                                    probability[one];
      if ((cover < 0.06f * map::forest_cover[one] &&
           population <= 0.0f * proportion[one]) ||
          draw (identity, 2).numerical_value_in (one) >
            planted.numerical_value_in (one) ||
          !roots_dry (surface, water, x, z))
        continue;
      candidates.push_back ({ .x = x,
                              .z = z,
                              .cover = cover,
                              .moisture = moisture,
                              .grove = grove,
                              .pioneer = pioneer,
                              .open = open,
                              .snag = snag,
                              .spruce = spruce,
                              .pine = pine,
                              .birch = birch,
                              .fell = fell,
                              .bog = bog,
                              .recovery = recovery,
                              .priority = draw (identity, 3),
                              .identity = identity });
    }

    std::ranges::sort (
      candidates, [] (const ForestCandidate& a, const ForestCandidate& b) {
        return a.priority < b.priority ||
               (a.priority == b.priority && a.identity < b.identity);
      });
    const meters_t exclusion =
      effective_spacing * forest_exclusion_ratio.numerical_value_in (one);
    // How many whole lengths of SPAN fit, rounded up.
    const auto lengths = [] (meters_t extent, meters_t span) {
      return std::ceil ((extent / span).numerical_value_in (one));
    };
    const std::uint32_t bins_x =
      std::max (1U, static_cast<std::uint32_t> (lengths (width, exclusion)));
    const std::uint32_t bins_z =
      std::max (1U, static_cast<std::uint32_t> (lengths (depth, exclusion)));
    const meters_t bin_x = width / static_cast<float> (bins_x);
    const meters_t bin_z = depth / static_cast<float> (bins_z);
    const int reach_x = static_cast<int> (lengths (exclusion, bin_x));
    const int reach_z = static_cast<int> (lengths (exclusion, bin_z));
    std::vector<std::int32_t> heads (static_cast<std::size_t> (bins_x) * bins_z,
                                     -1);
    std::vector<std::int32_t> next;
    std::vector<ForestCandidate> accepted;
    next.reserve (candidates.size ());
    accepted.reserve (candidates.size ());
    const auto wrap = [] (int value, std::uint32_t period) {
      const int extent = static_cast<int> (period);
      value %= extent;
      return static_cast<std::uint32_t> (value < 0 ? value + extent : value);
    };
    const square_meters_t exclusion_squared = exclusion * exclusion;
    const auto bin_of = [] (meters_t at, meters_t bin, std::uint32_t bins) {
      return std::min (
        static_cast<std::uint32_t> ((at / bin).numerical_value_in (one)),
        bins - 1);
    };

    for (const ForestCandidate& candidate : candidates) {
      const std::uint32_t bx = bin_of (candidate.x, bin_x, bins_x);
      const std::uint32_t bz = bin_of (candidate.z, bin_z, bins_z);
      bool separated = true;
      for (int dz = -reach_z; separated && dz <= reach_z; ++dz)
        for (int dx = -reach_x; separated && dx <= reach_x; ++dx) {
          const std::uint32_t nx = wrap (static_cast<int> (bx) + dx, bins_x);
          const std::uint32_t nz = wrap (static_cast<int> (bz) + dz, bins_z);
          std::int32_t index =
            heads[static_cast<std::size_t> (nz) * bins_x + nx];
          while (index >= 0) {
            const ForestCandidate& neighbour =
              accepted[static_cast<std::size_t> (index)];
            const meters_t delta_x =
              terrain::minimum_image_delta (candidate.x - neighbour.x, width);
            const meters_t delta_z =
              terrain::minimum_image_delta (candidate.z - neighbour.z, depth);
            if (delta_x * delta_x + delta_z * delta_z < exclusion_squared) {
              separated = false;
              break;
            }
            index = next[static_cast<std::size_t> (index)];
          }
        }
      if (!separated)
        continue;
      const std::size_t bin = static_cast<std::size_t> (bz) * bins_x + bx;
      next.push_back (heads[bin]);
      heads[bin] = static_cast<std::int32_t> (accepted.size ());
      accepted.push_back (candidate);
    }

    plan.sites.reserve (accepted.size ());
    for (const ForestCandidate& candidate : accepted) {
      const meters_t x = candidate.x;
      const meters_t z = candidate.z;
      const map::ForestCover cover = candidate.cover;
      const terrain::SurfaceElevation elevation = elevation_at (surface, x, z);
      // A boreal landscape: spruce is the forest, birch its pioneer.
      ForestCandidate site = candidate;
      if (site.grove > 0.0f * proportion[one] &&
          site.moisture == 0.0f * map::surface_moisture[one])
        site.moisture = moisture_at (readings, x, z);
      // A snag was the old stand: grown before the fire, its top broken
      // off since.
      if (candidate.snag) {
        const ForestAge age =
          draw (candidate.identity, 12) < 0.25f * proportion[one]
            ? ForestAge::Ancient
            : ForestAge::Mature;
        plan.sites.push_back (
          { .position = forest_position (x, elevation, z),
            .normal = normal_at (surface, x, z),
            .cover = cover,
            .moisture = moisture_at (readings, x, z),
            .size =
              size_for_age (age, candidate.identity) *
              (0.45f +
               0.45f * draw (candidate.identity, 13).numerical_value_in (one)),
            .seed = candidate.identity,
            .form = ForestForm::Snag,
            .age = age });
        continue;
      }
      const ForestAge age =
        age_from_identity (candidate.identity, candidate.recovery);
      // Which tree: drawn by the canopy's shares here, so a stand at an
      // edge mixes its neighbours. Birch's pioneers, and its groves among
      // the spruce, are birch whatever the draw.
      const std::array<proportion_t, 4> shares {
        candidate.spruce, candidate.pine, candidate.birch, candidate.fell
      };
      const proportion_t total = shares[0] + shares[1] + shares[2] + shares[3];
      proportion_t left =
        draw (candidate.identity, 9) * total.numerical_value_in (one);
      std::size_t kind = 0;
      while (kind < 3 && left >= shares[kind])
        left -= shares[kind++];
      // Where no tree has come back yet, the first to is a birch.
      if (total <= 0.0f * proportion[one])
        kind = candidate.recovery < 0.5f * map::stand_recovery[one] ? 2 : 0;
      ForestForm form = kind == 1   ? ForestForm::Pine
                        : kind == 0 ? ForestForm::Conifer
                        : kind == 3 ? ForestForm::FellBirch
                                    : ForestForm::Broadleaf;
      if (form != ForestForm::FellBirch &&
          (candidate.pioneer ||
           (form == ForestForm::Conifer && is_birch (site))))
        form = ForestForm::Broadleaf;
      // Toward the treeline the trees shrink: the fell birch low and
      // many-crowned, spruce stunted to krummholz among it. A bog's pines
      // are old and no taller than a person or two.
      const proportion_t fell_share =
        (total > 0.0f * proportion[one]
           ? (candidate.fell / total).numerical_value_in (one)
           : 0.0f) *
        proportion[one];
      scale_factor_t shrink = 1.0f * scale_factor[one];
      if (form == ForestForm::FellBirch)
        shrink = 0.30f * scale_factor[one];
      else if (form == ForestForm::Conifer)
        shrink = (1.0f - 0.6f * fell_share.numerical_value_in (one)) *
                 scale_factor[one];
      else if (form == ForestForm::Pine)
        shrink =
          std::lerp (1.0f, 0.28f, candidate.bog.numerical_value_in (one)) *
          (1.0f - 0.4f * fell_share.numerical_value_in (one)) *
          scale_factor[one];
      // And young ground grows young trees smaller still.
      shrink *= std::lerp (0.45f,
                           1.0f,
                           band (0.0f * map::stand_recovery[one],
                                 0.3f * map::stand_recovery[one],
                                 candidate.recovery)
                             .numerical_value_in (one));
      const TreeSizeFactor size = size_for_age (age, candidate.identity) *
                                  shrink.numerical_value_in (one);
      plan.sites.push_back ({ .position = forest_position (x, elevation, z),
                              .normal = normal_at (surface, x, z),
                              .cover = cover,
                              .moisture = moisture_at (readings, x, z),
                              .size = size,
                              .seed = candidate.identity,
                              .form = form,
                              .age = age });
    }
    return plan;
  }

  void save_forest_plan (const ForestPlan& plan,
                         std::uint32_t seed,
                         const std::string& path) {
    const Vec3 period = extent_value (plan.period);
    const ForestPlanHeader header {
      .magic = forest_plan_magic,
      .version = forest_plan_version,
      .seed = seed,
      .period = { period[0], period[1], period[2] },
      .reserved = 0,
      .site_count = plan.sites.size (),
    };
    std::ofstream output (path, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error ("could not create forest plan: " + path);
    write_record (output, header);
    for (const ForestSite& site : plan.sites) {
      const Vec3 point = position_value (site.position);
      const Vec3 normal = site.normal.numerical_value_in (one);
      const ForestSiteRecord record {
        .position = { point[0], point[1], point[2] },
        .normal = { normal[0], normal[1], normal[2] },
        .cover = site.cover.numerical_value_in (one),
        .moisture = site.moisture.numerical_value_in (one),
        .size = site.size.numerical_value_in (one),
        .seed = site.seed,
        .form = static_cast<std::uint32_t> (site.form),
        .age = static_cast<std::uint32_t> (site.age),
      };
      write_record (output, record);
    }
    output.close ();
    if (!output)
      throw std::runtime_error ("could not write forest plan: " + path);
  }

  std::optional<ForestPlan>
  try_load_forest_plan (const std::string& path,
                        std::uint32_t seed,
                        const spatial_extent_t& expected_period) {
    std::ifstream input (path, std::ios::binary);
    ForestPlanHeader header {};
    input.read (reinterpret_cast<char*> (&header), sizeof (header));
    if (!input || header.magic != forest_plan_magic ||
        header.version != forest_plan_version || header.seed != seed ||
        !same_extent (header.period, expected_period) ||
        header.site_count > 25'000'000)
      return std::nullopt;
    const std::uintmax_t expected_bytes =
      sizeof (header) + header.site_count * sizeof (ForestSiteRecord);
    std::error_code error;
    if (std::filesystem::file_size (path, error) != expected_bytes || error)
      return std::nullopt;

    ForestPlan plan;
    plan.period = expected_period;
    plan.sites.reserve (static_cast<std::size_t> (header.site_count));
    for (std::uint64_t i = 0; i < header.site_count; ++i) {
      ForestSiteRecord record {};
      input.read (reinterpret_cast<char*> (&record), sizeof (record));
      if (!input ||
          record.form > static_cast<std::uint32_t> (ForestForm::Snag) ||
          record.age > static_cast<std::uint32_t> (ForestAge::Ancient))
        return std::nullopt;
      plan.sites.push_back (
        { .position = position (
            Vec3 (record.position[0], record.position[1], record.position[2])),
          .normal =
            Vec3 (record.normal[0], record.normal[1], record.normal[2]) *
            terrain::terrain_normal[one],
          .cover = record.cover * map::forest_cover[one],
          .moisture = record.moisture * map::surface_moisture[one],
          .size = record.size * tree_size_factor[one],
          .seed = record.seed,
          .form = static_cast<ForestForm> (record.form),
          .age = static_cast<ForestAge> (record.age) });
    }
    return plan;
  }
}
