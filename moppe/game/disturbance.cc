#include <moppe/game/disturbance.hh>

#include <moppe/correct_math.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace moppe::game {
  namespace {
    using mp_units::one;

    // How far ground has come back some years after it was opened: a
    // tenth in ten years, half in sixty, nine tenths in two hundred.
    float recovery_after_years (float years) {
      return 1.0f - cr::exp (-years / 90.0f);
    }

    // The ground's own record: the least recovery any opening has left
    // it, whether that opening was a fire, and how hard it is grazed now.
    struct OpeningLedger {
      std::vector<float> recovery, grazed, burnt;

      explicit OpeningLedger (std::size_t count)
          : recovery (count, 1.0f), grazed (count, 0.0f), burnt (count, 0.0f) {}

      // WEIGHT fades an opening out at its edges.
      void open (std::size_t i,
                 float weight,
                 float recovery_then,
                 float grazing,
                 float burn) {
        const float left = 1.0f - weight * (1.0f - recovery_then);
        if (left < recovery[i]) {
          recovery[i] = left;
          burnt[i] = weight * burn;
        }
        grazed[i] = std::max (grazed[i], weight * grazing);
      }
    };

    struct OpeningLattice {
      std::size_t width, height;
      float dx, dz;

      std::size_t at (long column, long row) const {
        const long w = static_cast<long> (width);
        const long h = static_cast<long> (height);
        column %= w;
        row %= h;
        return static_cast<std::size_t> (row < 0 ? row + h : row) * width +
               static_cast<std::size_t> (column < 0 ? column + w : column);
      }
    };
  }

  map::DisturbanceMap plan_disturbance (const map::SurfaceGeometry& geometry,
                                        const map::SurfaceReadings& readings,
                                        const terrain::WaterSheets& water,
                                        const terrain::RoadNetwork& roads,
                                        std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("game::plan_disturbance");
    using spatial::get;
    const terrain::TerrainDomain& domain = geometry.domain ();
    const std::size_t count = domain.size ();
    const OpeningLattice lattice {
      domain.width (),
      domain.height (),
      domain.spacing_x ().numerical_value_in (u::m),
      domain.spacing_z ().numerical_value_in (u::m)
    };
    const float cell_area = lattice.dx * lattice.dz;

    const auto& ground = get<terrain::surface_elevation> (geometry);
    const auto& level = get<terrain::surface_elevation> (water);
    const auto& normals = get<terrain::terrain_normal> (geometry);
    const auto value = [&] (auto quantity, std::size_t i) {
      return get<quantity> (readings)[i].numerical_value_in (one);
    };

    // What each place is: open water, or ground; and on ground how steep
    // and how readily it burns -- dry, and carrying the resinous trees and
    // the lichen and heath that carry a fire, but never the bog.
    std::vector<float> height (count), up (count), fuel (count), mire (count);
    std::vector<bool> wet_cell (count);
    std::size_t land = 0;
    for (std::size_t i = 0; i < count; ++i) {
      height[i] = terrain::surface_elevation_value (ground[i]);
      wet_cell[i] = terrain::surface_elevation_value (level[i]) > height[i];
      up[i] = normals[i].numerical_value_in (one)[1];
      mire[i] = value (map::sphagnum_abundance, i);
      if (wet_cell[i])
        continue;
      ++land;
      const float dry =
        1.0f - smoothstep (0.22f, 0.50f, value (terrain::soil_wetness, i));
      const float load = value (map::pine_abundance, i) +
                         0.8f * value (map::spruce_abundance, i) +
                         0.5f * value (map::mountain_birch_abundance, i) +
                         0.6f * value (map::lichen_abundance, i) +
                         0.6f * value (map::heather_abundance, i) + 0.12f;
      const float sodden = 1.0f - mire[i];
      fuel[i] = dry * std::min (load, 1.0f) * sodden * sodden;
    }

    // Raggedness for every edge, at a stand's scale and a tree's.
    const float period_m = domain.period_x ().numerical_value_in (u::m);
    const auto laps = [&] (float patch_m) {
      return std::max<std::uint32_t> (
        1, static_cast<std::uint32_t> (std::lround (period_m / patch_m)));
    };
    const std::uint32_t coarse = laps (140.0f);
    const std::uint32_t fine = laps (25.0f);
    std::vector<float> ragged (count);
    for (const terrain::TerrainIndex site : spatial::sites (geometry)) {
      const auto where = domain.lap_position (site);
      ragged[domain.offset (site)] =
        0.65f * periodic_noise (
                  where.along_x, where.along_z, coarse, seed ^ 0x2a91c3d5U)
                  .numerical_value_in (one) +
        0.35f * periodic_noise (
                  where.along_x, where.along_z, fine, seed ^ 0x71be0f13U)
                  .numerical_value_in (one);
    }

    OpeningLedger ledger (count);

    // Pastures. Each place kept the ground round it grazed open while it
    // was lived in -- the gentle, dry ground most, never the bog -- and
    // the birch has been walking back in since it went quiet.
    std::size_t pastures = 0, kept = 0;
    for (const terrain::RoadPlace& place : roads.places) {
      using Kind = terrain::RoadPlace::Kind;
      if (place.kind == Kind::Trailhead)
        continue;
      ++pastures;
      const bool living = place.abandoned_years_ago <= 0.0f;
      kept += living;
      const float reach = place.kind == Kind::Sanctuary ? 1300.0f
                          : place.kind == Kind::Landing ? 650.0f
                                                        : 500.0f;
      const float then =
        living ? 0.36f : recovery_after_years (place.abandoned_years_ago);
      const float grazing = living ? 0.85f : 0.0f;
      const long cx = std::lround (place.x_m / lattice.dx);
      const long cz = std::lround (place.z_m / lattice.dz);
      const long rx = static_cast<long> (std::ceil (1.4f * reach / lattice.dx));
      const long rz = static_cast<long> (std::ceil (1.4f * reach / lattice.dz));
      for (long dz = -rz; dz <= rz; ++dz)
        for (long dx = -rx; dx <= rx; ++dx) {
          const std::size_t i = lattice.at (cx + dx, cz + dz);
          if (wet_cell[i])
            continue;
          const float metres = cr::hypot (static_cast<float> (dx) * lattice.dx,
                                          static_cast<float> (dz) * lattice.dz);
          const float near =
            1.0f - smoothstep (
                     0.5f, 1.0f, metres / (reach * (0.7f + 0.6f * ragged[i])));
          const float weight = near * smoothstep (0.86f, 0.95f, up[i]) *
                               (1.0f - smoothstep (0.2f, 0.5f, mire[i]));
          if (weight > 0.01f)
            ledger.open (i, weight, then, grazing, 0.0f);
        }
    }

    // Verges. A road in use is kept clear its width and a little more,
    // cut and trodden; one gone out of use grows over.
    float verge_m = 0.0f;
    for (const terrain::Road& road : roads.roads) {
      const bool cart = road.kind == terrain::Road::Kind::Road;
      const float half =
        cart ? 6.0f + 3.0f * cr::log2 (1.0f + static_cast<float> (road.traffic))
             : 2.5f;
      const float edge = 1.7f * half;
      const bool living = road.abandoned_years_ago <= 0.0f;
      const float then =
        living ? 0.22f : recovery_after_years (road.abandoned_years_ago);
      const float grazing = living ? (cart ? 0.75f : 0.5f) : 0.0f;
      for (std::size_t p = 1; p < road.points.size (); ++p) {
        const float ax = road.points[p - 1].x_m, az = road.points[p - 1].z_m;
        const float bx = road.points[p].x_m, bz = road.points[p].z_m;
        const float sx = bx - ax, sz = bz - az;
        const float span2 = std::max (sx * sx + sz * sz, 1e-6f);
        verge_m += std::sqrt (span2);
        const long c0 = static_cast<long> (
          std::floor ((std::min (ax, bx) - edge) / lattice.dx));
        const long c1 = static_cast<long> (
          std::ceil ((std::max (ax, bx) + edge) / lattice.dx));
        const long r0 = static_cast<long> (
          std::floor ((std::min (az, bz) - edge) / lattice.dz));
        const long r1 = static_cast<long> (
          std::ceil ((std::max (az, bz) + edge) / lattice.dz));
        for (long r = r0; r <= r1; ++r)
          for (long c = c0; c <= c1; ++c) {
            const float px = static_cast<float> (c) * lattice.dx - ax;
            const float pz = static_cast<float> (r) * lattice.dz - az;
            const float t =
              std::clamp ((px * sx + pz * sz) / span2, 0.0f, 1.0f);
            const float ex = px - t * sx, ez = pz - t * sz;
            const float metres = std::sqrt (ex * ex + ez * ez);
            if (metres >= edge)
              continue;
            const std::size_t i = lattice.at (c, r);
            if (wet_cell[i])
              continue;
            const float weight =
              1.0f -
              smoothstep (half, edge, metres * (0.8f + 0.4f * ragged[i]));
            ledger.open (i, weight, then, grazing, 0.0f);
          }
      }
    }

    // Fires. Lightning strikes where the ground is ready to burn, and the
    // fire runs from there to its neighbours as fast as their fuel lets it,
    // faster before the prevailing west-south-west wind and up a slope,
    // until it has had its fill; water, bog, and bare rock stop it.
    const float land_km2 = static_cast<float> (land) * cell_area * 1e-6f;
    const std::size_t fires =
      static_cast<std::size_t> (std::lround (0.6f * land_km2));
    const float wind_x = 0.92f, wind_z = -0.38f; // toward east-north-east
    std::vector<float> reached (count, std::numeric_limits<float>::infinity ());
    std::vector<std::size_t> touched;
    using Front = std::pair<float, std::size_t>;
    std::size_t burnt_cells = 0;
    float youngest = std::numeric_limits<float>::infinity ();
    for (std::size_t fire = 0; fire < fires; ++fire) {
      const std::uint32_t identity = lattice_hash (
        static_cast<std::uint32_t> (fire), 0x6f1e5U, seed ^ 0x0f1a3e5dU);
      std::size_t origin = count;
      for (std::uint32_t attempt = 0; attempt < 400 && origin == count;
           ++attempt) {
        const std::uint32_t draw = lattice_hash (identity, attempt, 0x51a7e1U);
        const std::size_t i = draw % count;
        const float f = fuel[i];
        if (!wet_cell[i] && hash_lane (draw, 1) < f * f)
          origin = i;
      }
      if (origin == count)
        continue;
      const float years =
        1.0f + 260.0f * cr::pow (hash_lane (identity, 2), 1.8f);
      const float hectares = 3.0f * cr::pow (40.0f, hash_lane (identity, 3));
      const std::size_t fill =
        static_cast<std::size_t> (hectares * 1e4f / cell_area);
      youngest = std::min (youngest, years);
      const float then = recovery_after_years (years);
      const float burn = cr::exp (-years / 12.0f);

      std::priority_queue<Front, std::vector<Front>, std::greater<Front>> front;
      reached[origin] = 0.0f;
      touched.push_back (origin);
      front.push ({ 0.0f, origin });
      std::size_t burned = 0;
      while (!front.empty () && burned < fill) {
        const auto [time, i] = front.top ();
        front.pop ();
        if (time > reached[i])
          continue;
        ledger.open (i, 1.0f, then, 0.0f, burn);
        ++burned;
        const long c = static_cast<long> (i % lattice.width);
        const long r = static_cast<long> (i / lattice.width);
        for (long dz = -1; dz <= 1; ++dz)
          for (long dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz)
              continue;
            const std::size_t n = lattice.at (c + dx, r + dz);
            if (wet_cell[n] || fuel[n] < 0.08f)
              continue;
            const float sx = static_cast<float> (dx) * lattice.dx;
            const float sz = static_cast<float> (dz) * lattice.dz;
            const float step = cr::hypot (sx, sz);
            const float wind = (sx * wind_x + sz * wind_z) / step;
            const float climb =
              std::clamp ((height[n] - height[i]) / step, -0.4f, 0.6f);
            const float rate = fuel[n] * cr::exp (0.9f * wind + 2.0f * climb) *
                               (0.3f + 1.4f * ragged[n]);
            const float arrival = time + step / rate;
            if (arrival < reached[n]) {
              if (reached[n] == std::numeric_limits<float>::infinity ())
                touched.push_back (n);
              reached[n] = arrival;
              front.push ({ arrival, n });
            }
          }
      }
      burnt_cells += burned;
      for (const std::size_t i : touched)
        reached[i] = std::numeric_limits<float>::infinity ();
      touched.clear ();
    }

    std::size_t opened = 0;
    for (std::size_t i = 0; i < count; ++i)
      opened += !wet_cell[i] && ledger.recovery[i] < 0.5f;
    std::cerr << "moppe: disturbance: " << fires << " fires over "
              << std::lround (burnt_cells * cell_area * 1e-4f)
              << " ha (the youngest " << std::lround (youngest)
              << " years ago), " << pastures << " pastures (" << kept
              << " still grazed), " << std::lround (verge_m * 1e-3f)
              << " km of verges; "
              << std::lround (100.0 * opened / std::max<std::size_t> (land, 1))
              << "% of the land less than half recovered" << std::endl;

    map::DisturbanceMap values (domain);
    auto& grazing = get<map::grazing_pressure> (values);
    auto& recovery = get<map::stand_recovery> (values);
    auto& burnt = get<map::burn_recency> (values);
    for (std::size_t i = 0; i < count; ++i) {
      grazing[i] = ledger.grazed[i] * map::grazing_pressure[one];
      recovery[i] = ledger.recovery[i] * map::stand_recovery[one];
      burnt[i] = ledger.burnt[i] * map::burn_recency[one];
    }
    return values;
  }
}
