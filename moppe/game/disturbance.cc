#include <moppe/game/disturbance.hh>

#include <moppe/correct_math.hh>
#include <moppe/game/place_kind.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/spatial/bundle_operations.hh>

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
    using mp_units::astronomy::Julian_year;

    // How far ground has come back some years after it was opened: a
    // tenth in ten years, half in sixty, nine tenths in two hundred.
    map::StandRecovery recovery_after_years (age_t years) {
      return (1.0f -
              cr::exp (-years.numerical_value_in (Julian_year) / 90.0f)) *
             map::stand_recovery[one];
    }

    // The ground's own record: the least recovery any opening has left
    // it, whether that opening was a fire, and how hard it is grazed now.
    struct OpeningLedger {
      std::vector<map::StandRecovery> recovery;
      std::vector<map::GrazingPressure> grazed;
      std::vector<map::BurnRecency> burnt;

      explicit OpeningLedger (std::size_t count)
          : recovery (count, 1.0f * map::stand_recovery[one]),
            grazed (count, 0.0f * map::grazing_pressure[one]),
            burnt (count, 0.0f * map::burn_recency[one]) {}

      // WEIGHT fades an opening out at its edges.
      void open (std::size_t i,
                 blend_weight_t weight,
                 map::StandRecovery recovery_then,
                 map::GrazingPressure grazing,
                 map::BurnRecency burn) {
        const map::StandRecovery left = quantity_cast<map::stand_recovery> (
          1.0f * one - weight * (1.0f * one - recovery_then));
        if (left < recovery[i]) {
          recovery[i] = left;
          burnt[i] = quantity_cast<map::burn_recency> (weight * burn);
        }
        grazed[i] = std::max (
          grazed[i], quantity_cast<map::grazing_pressure> (weight * grazing));
      }
    };

    struct OpeningLattice {
      std::size_t width, height;
      meters_t dx, dz;

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
    const OpeningLattice lattice { domain.width (),
                                   domain.height (),
                                   domain.spacing_x (),
                                   domain.spacing_z () };
    const square_meters_t cell_area = lattice.dx * lattice.dz;

    const auto& ground = get<terrain::surface_elevation> (geometry);
    const auto& level = get<terrain::surface_elevation> (water);
    const auto& normals = get<terrain::terrain_normal> (geometry);
    const auto value = [&] (auto quantity, std::size_t i) {
      return get<quantity> (readings)[i].numerical_value_in (one);
    };

    // What each place is: open water, or ground; and on ground how steep
    // and how readily it burns -- dry, and carrying the resinous trees and
    // the lichen and heath that carry a fire, but never the bog.
    std::vector<terrain::SurfaceElevation> height (count);
    std::vector<proportion_t> up (count), fuel (count), mire (count);
    std::vector<bool> wet_cell (count);
    std::size_t land = 0;
    for (std::size_t i = 0; i < count; ++i) {
      height[i] = ground[i];
      wet_cell[i] = level[i] > height[i];
      up[i] = static_cast<float> (normals[i].numerical_value_in (one)[1]) *
              proportion[one];
      mire[i] = get<map::sphagnum_abundance> (readings)[i];
      if (wet_cell[i])
        continue;
      ++land;
      const proportion_t dry =
        (1.0f - smoothstep (0.22f, 0.50f, value (terrain::soil_wetness, i))) *
        proportion[one];
      const proportion_t load =
        (value (map::pine_abundance, i) +
         0.8f * value (map::spruce_abundance, i) +
         0.5f * value (map::mountain_birch_abundance, i) +
         0.6f * value (map::lichen_abundance, i) +
         0.6f * value (map::heather_abundance, i) + 0.12f) *
        proportion[one];
      const proportion_t sodden = 1.0f * proportion[one] - mire[i];
      fuel[i] = quantity_cast<proportion> (
        dry * std::min (load, 1.0f * proportion[one]) * sodden * sodden);
    }

    // Raggedness for every edge, at a stand's scale and a tree's.
    const meters_t period = domain.period_x ();
    const auto laps = [&] (meters_t patch) {
      return std::max<std::uint32_t> (
        1,
        static_cast<std::uint32_t> (
          std::lround ((period / patch).numerical_value_in (one))));
    };
    const std::uint32_t coarse = laps (140.0f * u::m);
    const std::uint32_t fine = laps (25.0f * u::m);
    std::vector<noise_signal_t> ragged (count);
    for (const terrain::TerrainIndex site : spatial::sites (geometry)) {
      const auto where = domain.lap_position (site);
      ragged[domain.offset (site)] =
        0.65f * periodic_noise (
                  where.along_x, where.along_z, coarse, seed ^ 0x2a91c3d5U) +
        0.35f * periodic_noise (
                  where.along_x, where.along_z, fine, seed ^ 0x71be0f13U);
    }

    OpeningLedger ledger (count);

    // Pastures. Each place kept the ground round it grazed open while it
    // was lived in -- the gentle, dry ground most, never the bog -- and
    // the birch has been walking back in since it went quiet.
    std::size_t pastures = 0, kept = 0;
    for (const terrain::RoadPlace& place : roads.places) {
      const meters_t reach = place_kind (place.kind).pasture_m * u::m;
      if (reach <= 0.0f * u::m)
        continue;
      ++pastures;
      const age_t abandoned = place.abandoned;
      const bool living = abandoned <= 0.0f * age[Julian_year];
      kept += living;
      const map::StandRecovery then = living ? 0.36f * map::stand_recovery[one]
                                             : recovery_after_years (abandoned);
      const map::GrazingPressure grazing =
        (living ? 0.85f : 0.0f) * map::grazing_pressure[one];
      const long cx =
        std::lround ((x_of (place.at) / lattice.dx).numerical_value_in (one));
      const long cz =
        std::lround ((z_of (place.at) / lattice.dz).numerical_value_in (one));
      const long rx = static_cast<long> (
        std::ceil ((1.4f * reach / lattice.dx).numerical_value_in (one)));
      const long rz = static_cast<long> (
        std::ceil ((1.4f * reach / lattice.dz).numerical_value_in (one)));
      for (long dz = -rz; dz <= rz; ++dz)
        for (long dx = -rx; dx <= rx; ++dx) {
          const std::size_t i = lattice.at (cx + dx, cz + dz);
          if (wet_cell[i])
            continue;
          const meters_t metres =
            hypot (plan_displacement (static_cast<float> (dx) * lattice.dx,
                                      static_cast<float> (dz) * lattice.dz));
          const meters_t extent =
            reach * (0.7f + 0.6f * ragged[i].numerical_value_in (one));
          const blend_weight_t near =
            (1.0f - smoothstep (
                      0.5f, 1.0f, (metres / extent).numerical_value_in (one))) *
            blend_weight[one];
          const blend_weight_t weight =
            near * smoothstep (0.86f, 0.95f, up[i].numerical_value_in (one)) *
            (1.0f - smoothstep (0.2f, 0.5f, mire[i].numerical_value_in (one)));
          if (weight > 0.01f * blend_weight[one])
            ledger.open (
              i, weight, then, grazing, 0.0f * map::burn_recency[one]);
        }
    }

    // Verges. A road in use is kept clear its width and a little more,
    // cut and trodden; one gone out of use grows over.
    meters_t verge = 0.0f * u::m;
    for (const terrain::Road& road : roads.roads) {
      const bool cart = road.kind == terrain::Road::Kind::Road;
      // A busier road is kept clear wider, but even a trunk road's verge
      // is a strip beside it, not a clearing.
      const meters_t half =
        cart ? std::min ((4.0f + 1.5f * cr::log2 (1.0f + static_cast<float> (
                                                           road.traffic))) *
                           u::m,
                         12.0f * u::m)
             : 2.5f * u::m;
      const meters_t edge = 1.7f * half;
      const age_t abandoned = road.abandoned;
      const bool living = abandoned <= 0.0f * age[Julian_year];
      const map::StandRecovery then = living ? 0.22f * map::stand_recovery[one]
                                             : recovery_after_years (abandoned);
      const map::GrazingPressure grazing =
        (living ? (cart ? 0.75f : 0.5f) : 0.0f) * map::grazing_pressure[one];
      for (std::size_t p = 1; p < road.points.size (); ++p) {
        const meters_t ax = x_of (road.points[p - 1]);
        const meters_t az = z_of (road.points[p - 1]);
        const meters_t bx = x_of (road.points[p]);
        const meters_t bz = z_of (road.points[p]);
        const meters_t sx = bx - ax, sz = bz - az;
        const square_meters_t span2 =
          std::max<square_meters_t> (sx * sx + sz * sz, 1e-6f * u::m2);
        verge += sqrt (span2);
        const long c0 = static_cast<long> (std::floor (
          ((std::min (ax, bx) - edge) / lattice.dx).numerical_value_in (one)));
        const long c1 = static_cast<long> (std::ceil (
          ((std::max (ax, bx) + edge) / lattice.dx).numerical_value_in (one)));
        const long r0 = static_cast<long> (std::floor (
          ((std::min (az, bz) - edge) / lattice.dz).numerical_value_in (one)));
        const long r1 = static_cast<long> (std::ceil (
          ((std::max (az, bz) + edge) / lattice.dz).numerical_value_in (one)));
        for (long r = r0; r <= r1; ++r)
          for (long c = c0; c <= c1; ++c) {
            const meters_t px = static_cast<float> (c) * lattice.dx - ax;
            const meters_t pz = static_cast<float> (r) * lattice.dz - az;
            // How far along the span the nearest point of it lies.
            const proportion_t t =
              std::clamp (
                ((px * sx + pz * sz) / span2).numerical_value_in (one),
                0.0f,
                1.0f) *
              proportion[one];
            const meters_t ex = px - t * sx, ez = pz - t * sz;
            const meters_t metres = sqrt (ex * ex + ez * ez);
            if (metres >= edge)
              continue;
            const std::size_t i = lattice.at (c, r);
            if (wet_cell[i])
              continue;
            const meters_t frayed =
              metres * (0.8f + 0.4f * ragged[i].numerical_value_in (one));
            const blend_weight_t weight =
              (1.0f - smoothstep (half.numerical_value_in (u::m),
                                  edge.numerical_value_in (u::m),
                                  frayed.numerical_value_in (u::m))) *
              blend_weight[one];
            ledger.open (
              i, weight, then, grazing, 0.0f * map::burn_recency[one]);
          }
      }
    }

    // Fires. Lightning strikes where the ground is ready to burn, and the
    // fire runs from there to its neighbours as fast as their fuel lets it,
    // faster before the prevailing west-south-west wind and up a slope,
    // until it has had its fill; water, bog, and bare rock stop it.
    const square_meters_t land_area = static_cast<float> (land) * cell_area;
    const std::size_t fires = static_cast<std::size_t> (
      std::lround (0.6f * (land_area * 1e-6f).numerical_value_in (u::m2)));
    // Toward east-north-east.
    const plan_direction_t wind = Vec2 (0.92f, -0.38f) * direction[one];
    // How far the fire has come to reach each place, the distance stretched
    // where it burns slowly and shrunk where it races.
    const meters_t never = std::numeric_limits<float>::infinity () * u::m;
    std::vector<meters_t> reached (count, never);
    std::vector<std::size_t> touched;
    using Front = std::pair<meters_t, std::size_t>;
    std::size_t burnt_cells = 0;
    age_t youngest = std::numeric_limits<float>::infinity () * age[Julian_year];
    for (std::size_t fire = 0; fire < fires; ++fire) {
      const std::uint32_t identity = lattice_hash (
        static_cast<std::uint32_t> (fire), 0x6f1e5U, seed ^ 0x0f1a3e5dU);
      std::size_t origin = count;
      for (std::uint32_t attempt = 0; attempt < 400 && origin == count;
           ++attempt) {
        const std::uint32_t draw = lattice_hash (identity, attempt, 0x51a7e1U);
        const std::size_t i = draw % count;
        const probability_t odds =
          quantity_cast<probability> (fuel[i] * fuel[i]);
        if (!wet_cell[i] && hash_lane (draw, 1) * probability[one] < odds)
          origin = i;
      }
      if (origin == count)
        continue;
      const age_t years =
        (1.0f + 260.0f * cr::pow (hash_lane (identity, 2), 1.8f)) *
        age[Julian_year];
      // From three hectares to a hundred and twenty.
      const square_meters_t extent =
        3.0f * cr::pow (40.0f, hash_lane (identity, 3)) * 1e4f * u::m2;
      const std::size_t fill = static_cast<std::size_t> (
        (extent / cell_area).numerical_value_in (one));
      youngest = std::min (youngest, years);
      const map::StandRecovery then = recovery_after_years (years);
      const map::BurnRecency burn =
        cr::exp (-years.numerical_value_in (Julian_year) / 12.0f) *
        map::burn_recency[one];

      std::priority_queue<Front, std::vector<Front>, std::greater<Front>> front;
      reached[origin] = 0.0f * u::m;
      touched.push_back (origin);
      front.push ({ 0.0f * u::m, origin });
      std::size_t burned = 0;
      while (!front.empty () && burned < fill) {
        const auto [time, i] = front.top ();
        front.pop ();
        if (time > reached[i])
          continue;
        ledger.open (i,
                     1.0f * blend_weight[one],
                     then,
                     0.0f * map::grazing_pressure[one],
                     burn);
        ++burned;
        const long c = static_cast<long> (i % lattice.width);
        const long r = static_cast<long> (i / lattice.width);
        for (long dz = -1; dz <= 1; ++dz)
          for (long dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz)
              continue;
            const std::size_t n = lattice.at (c + dx, r + dz);
            if (wet_cell[n] || fuel[n] < 0.08f * proportion[one])
              continue;
            const plan_displacement_t stride =
              plan_displacement (static_cast<float> (dx) * lattice.dx,
                                 static_cast<float> (dz) * lattice.dz);
            const meters_t step = hypot (stride);
            const meters_t downwind =
              dot (plan_value (stride), plan_value (wind)) * u::m;
            const grade_t climb =
              std::clamp (
                ((height[n] - height[i]) / step).numerical_value_in (one),
                -0.4f,
                0.6f) *
              grade[one];
            const scale_factor_t rate =
              fuel[n].numerical_value_in (one) *
              cr::exp (0.9f * (downwind / step).numerical_value_in (one) +
                       2.0f * climb.numerical_value_in (one)) *
              (0.3f + 1.4f * ragged[n].numerical_value_in (one)) *
              scale_factor[one];
            const meters_t arrival =
              time + step / rate.numerical_value_in (one);
            if (arrival < reached[n]) {
              if (reached[n] == never)
                touched.push_back (n);
              reached[n] = arrival;
              front.push ({ arrival, n });
            }
          }
      }
      burnt_cells += burned;
      for (const std::size_t i : touched)
        reached[i] = never;
      touched.clear ();
    }

    std::size_t opened = 0;
    for (std::size_t i = 0; i < count; ++i)
      opened +=
        !wet_cell[i] && ledger.recovery[i] < 0.5f * map::stand_recovery[one];
    std::cerr << "moppe: disturbance: " << fires << " fires over "
              << std::lround (
                   (static_cast<float> (burnt_cells) * cell_area * 1e-4f)
                     .numerical_value_in (u::m2))
              << " ha (the youngest "
              << std::lround (youngest.numerical_value_in (Julian_year))
              << " years ago), " << pastures << " pastures (" << kept
              << " still grazed), "
              << std::lround ((verge * 1e-3f).numerical_value_in (u::m))
              << " km of verges; "
              << std::lround (100.0 * opened / std::max<std::size_t> (land, 1))
              << "% of the land less than half recovered" << std::endl;

    map::DisturbanceMap values (domain);
    auto& grazing = get<map::grazing_pressure> (values);
    auto& recovery = get<map::stand_recovery> (values);
    auto& burnt = get<map::burn_recency> (values);
    for (std::size_t i = 0; i < count; ++i) {
      grazing[i] = ledger.grazed[i];
      recovery[i] = ledger.recovery[i];
      burnt[i] = ledger.burnt[i];
    }
    return values;
  }
}
