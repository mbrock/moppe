#include <moppe/correct_math.hh>
#include <moppe/terrain/glacial.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    constexpr proportion_t zero_part = 0.0f * proportion[one];
    constexpr proportion_t whole_part = 1.0f * proportion[one];

    struct TroughSample {
      plan_position_t at;
      meters_t distance = 0.0f * u::m;
      square_meters_t area = 0.0f * u::m * u::m;
      SurfaceElevation floor = surface_elevation_point (0.0f * u::m);
      meters_t floor_half_width = 30.0f * u::m;
      proportion_t strength = whole_part;
    };

    std::uint32_t peak_hash (std::uint32_t x, std::uint32_t z) noexcept {
      std::uint32_t h = x * 374761393u + z * 668265263u + 0x9e3779b9u;
      h = (h ^ (h >> 13u)) * 1274126177u;
      return h ^ (h >> 16u);
    }

    proportion_t lattice_value (std::int32_t gx,
                                std::int32_t gz,
                                std::uint32_t seed) noexcept {
      const std::uint32_t h =
        peak_hash (static_cast<std::uint32_t> (gx) ^ seed,
                   static_cast<std::uint32_t> (gz) ^ (seed * 2654435761u));
      return static_cast<float> (h & 0xffffu) * (1.0f / 65535.0f) *
             proportion[one];
    }

    // Deterministic smooth value noise (0..1) for organic alpine domain
    // warping.
    proportion_t smooth_noise_octave (meters_t wx,
                                      meters_t wz,
                                      meters_t wavelength,
                                      std::uint32_t seed) noexcept {
      const quantity<one, float> sx = wx / wavelength;
      const quantity<one, float> sz = wz / wavelength;
      const quantity<one, float> fx =
        std::floor (sx.numerical_value_in (one)) * one;
      const quantity<one, float> fz =
        std::floor (sz.numerical_value_in (one)) * one;
      const std::int32_t ix =
        static_cast<std::int32_t> (fx.numerical_value_in (one));
      const std::int32_t iz =
        static_cast<std::int32_t> (fz.numerical_value_in (one));
      const proportion_t ux =
        (sx - fx).numerical_value_in (one) * proportion[one];
      const proportion_t uz =
        (sz - fz).numerical_value_in (one) * proportion[one];
      const proportion_t tx =
        proportion_t (ux * ux * (3.0f * whole_part - 2.0f * ux));
      const proportion_t tz =
        proportion_t (uz * uz * (3.0f * whole_part - 2.0f * uz));
      const proportion_t v00 = lattice_value (ix, iz, seed);
      const proportion_t v10 = lattice_value (ix + 1, iz, seed);
      const proportion_t v01 = lattice_value (ix, iz + 1, seed);
      const proportion_t v11 = lattice_value (ix + 1, iz + 1, seed);
      return moppe::lerp (
        moppe::lerp (v00, v10, tx), moppe::lerp (v01, v11, tx), tz);
    }

    // Deterministic smooth value noise folded into crisp alpine ridges (0..1).
    proportion_t ridged_noise_octave (meters_t wx,
                                      meters_t wz,
                                      meters_t wavelength,
                                      std::uint32_t seed) noexcept {
      const proportion_t v = smooth_noise_octave (wx, wz, wavelength, seed);
      const proportion_t folded =
        (1.0f - std::fabs (2.0f * v.numerical_value_in (one) - 1.0f)) *
        proportion[one];
      return cr::pow (folded.numerical_value_in (one), 1.50f) * proportion[one];
    }

    radians_t wrapped_angle_diff (radians_t a, radians_t b) noexcept {
      const radians_t d = a - b;
      return cr::atan2 (cr::sin (radians_value (d)),
                        cr::cos (radians_value (d))) *
             u::rad;
    }

    struct AlpineHorn {
      meters_t x = 0.0f * u::m;
      meters_t z = 0.0f * u::m;
      meters_t radius = 260.0f * u::m;
      meters_t prominence = 150.0f * u::m;
      radians_t phase = 0.0f * u::rad;
      int ribs = 4;
    };
  }

  GlacialSculptingResult
  detail::sculpt_glacial_valleys (const TerrainDomain& domain,
                                  std::span<const SurfaceElevation> elevations,
                                  const FloodField& flood,
                                  const LakeCensus& census,
                                  const DrainageGraph& drainage,
                                  const RiverNetwork& rivers,
                                  SurfaceElevation sea_level,
                                  const GlacialSculpting& parameters) {
    MOPPE_PROFILE_ZONE ("sculpt_glacial_valleys");
    const TerrainDomain& grid = flood.domain ();
    if (domain != grid)
      throw std::invalid_argument ("glacial terrain does not match hydrology");
    const int width = static_cast<int> (grid.width ());
    const int height = static_cast<int> (grid.height ());
    const std::size_t count = grid.width () * grid.height ();
    if (census.cell_count () != count || drainage.receiver.size () != count)
      throw std::invalid_argument ("glacial inputs do not share a grid");

    const meters_t spacing_x = grid.spacing_x ();
    const meters_t spacing_z = grid.spacing_z ();
    const meters_t period_x = static_cast<float> (width) * spacing_x;
    const meters_t period_z = static_cast<float> (height) * spacing_z;
    const meters_t cell_reach = std::max (spacing_x, spacing_z);

    std::vector<SurfaceElevation> formed (elevations.begin (),
                                          elevations.end ());
    std::vector<SedimentThickness> outwash (count,
                                            0.0f * sediment_thickness[u::m]);
    std::vector<std::uint8_t> carved_mask (count, 0);

    const auto ground_at = [&] (plan_position_t world) {
      const auto gx =
        wrap_coordinate ((x_of (world) / spacing_x).numerical_value_in (one),
                         static_cast<float> (width));
      const auto gz =
        wrap_coordinate ((z_of (world) / spacing_z).numerical_value_in (one),
                         static_cast<float> (height));
      const int x0 = static_cast<int> (std::floor (gx));
      const int z0 = static_cast<int> (std::floor (gz));
      const int x1 = wrap_index (x0 + 1, width);
      const int z1 = wrap_index (z0 + 1, height);
      const proportion_t tx = (gx - static_cast<float> (x0)) * proportion[one];
      const proportion_t tz = (gz - static_cast<float> (z0)) * proportion[one];
      const auto at = [&] (int x, int z) {
        return elevations[static_cast<std::size_t> (z) * width + x];
      };
      return moppe::lerp (moppe::lerp (at (x0, z0), at (x1, z0), tx),
                          moppe::lerp (at (x0, z1), at (x1, z1), tx),
                          tz);
    };

    const auto reaches_sea = [&] (const RiverReach& reach) {
      return reach.downstream_ocean ||
             (census.domain ().contains (reach.downstream_body) &&
              census.water_body (reach.downstream_body).classification ==
                WaterBodyClass::Sea);
    };

    GlacialSculptingResult result;

    // -------------------------------------------------------------------------
    // 1. Warm-based glacial U-troughs & drowned fjord mouths along the dominant
    //    sea-reaching trunk river valley(s).
    // -------------------------------------------------------------------------
    {
      MOPPE_PROFILE_ZONE ("glacial.troughs");
      std::vector<const RiverReach*> mouths;
      for (const RiverReach& reach : rivers.reaches)
        if (reach.downstream_reach == RiverReach::no_id &&
            reaches_sea (reach) &&
            reach.downstream_area >= parameters.trough_mouth_area &&
            reach.alignment.points.size () >= 2)
          mouths.push_back (&reach);

      std::ranges::sort (mouths, [] (const RiverReach* a, const RiverReach* b) {
        return a->downstream_area != b->downstream_area
                 ? a->downstream_area > b->downstream_area
                 : a->id < b->id;
      });

      std::vector<std::uint8_t> used_reach (rivers.reaches.size (), 0);
      std::vector<plan_position_t> carved_mouths;
      constexpr std::size_t max_troughs = 2;

      for (const RiverReach* mouth : mouths) {
        if (result.troughs >= max_troughs)
          break;
        if (used_reach[mouth->id])
          continue;
        const plan_position_t mouth_pos = mouth->alignment.points.back ().place;
        bool too_close = false;
        for (const plan_position_t& other : carved_mouths) {
          const meters_t dx =
            minimum_image_delta (x_of (mouth_pos) - x_of (other), period_x);
          const meters_t dz =
            minimum_image_delta (z_of (mouth_pos) - z_of (other), period_z);
          if (hypot (plan_displacement (dx, dz)) < 900.0f * u::m) {
            too_close = true;
            break;
          }
        }
        if (too_close)
          continue;

        // Trace the trunk course from its sea mouth upward through confluences
        // and inland lakes while catchment stays above trough_head_area.
        std::vector<const RiverReach*> course;
        const RiverReach* current = mouth;
        while (current && !used_reach[current->id]) {
          course.push_back (current);
          used_reach[current->id] = 1;
          const RiverReach* next = nullptr;
          for (const RiverReach& candidate : rivers.reaches) {
            if (used_reach[candidate.id] ||
                candidate.alignment.points.size () < 2)
              continue;
            const bool feeds_reach = candidate.downstream_reach == current->id;
            const bool feeds_lake =
              current->upstream_body != no_water_body &&
              candidate.downstream_body == current->upstream_body &&
              !reaches_sea (candidate);
            if (!feeds_reach && !feeds_lake)
              continue;
            if (candidate.downstream_area < parameters.trough_head_area)
              continue;
            if (!next || candidate.downstream_area > next->downstream_area)
              next = &candidate;
          }
          current = next;
        }

        // Collect unwrapped alignment points ordered mouth -> highlands,
        // capped to a natural coastal-fjord trough reach so valleys grade
        // smoothly back into the tectonic/fluvial landscape upstream.
        std::vector<TroughSample> raw;
        const meters_t max_trough_length =
          std::min (2800.0f * u::m, 0.45f * std::min (period_x, period_z));
        meters_t raw_dist = 0.0f * u::m;
        for (const RiverReach* reach : course) {
          const auto& pts = reach->alignment.points;
          for (std::size_t idx = pts.size (); idx-- > 0;) {
            if (pts[idx].contributing_area < parameters.trough_head_area &&
                !raw.empty ())
              continue;
            plan_position_t pos = pts[idx].place;
            if (!raw.empty ()) {
              const meters_t dx = minimum_image_delta (
                x_of (pos) - x_of (raw.back ().at), period_x);
              const meters_t dz = minimum_image_delta (
                z_of (pos) - z_of (raw.back ().at), period_z);
              const meters_t seg = hypot (plan_displacement (dx, dz));
              if (seg < 2.0f * u::m)
                continue;
              // Skip huge jumps across big lakes unless we interpolate cleanly.
              pos = plan_position (x_of (raw.back ().at) + dx,
                                   z_of (raw.back ().at) + dz);
              raw_dist += seg;
            }
            raw.push_back ({ .at = pos,
                             .distance = raw_dist,
                             .area = pts[idx].contributing_area });
            if (raw_dist >= max_trough_length)
              break;
          }
          if (raw_dist >= max_trough_length)
            break;
        }
        if (raw.size () < 6 || raw.back ().distance < 500.0f * u::m)
          continue;

        // Subsample every ~48 m and straighten sharp fluvial spurs into grand
        // sweeping glacial U-valley curves (anchoring the sea mouth).
        constexpr meters_t coarse_step = 48.0f * u::m;
        std::vector<TroughSample> spine;
        spine.push_back (raw.front ());
        for (std::size_t i = 1; i < raw.size (); ++i) {
          const meters_t step = hypot (raw[i].at - spine.back ().at);
          if (step >= coarse_step || i + 1 == raw.size ()) {
            const int pieces =
              std::max (1,
                        static_cast<int> (std::ceil (
                          (step / coarse_step).numerical_value_in (one))));
            const TroughSample from = spine.back ();
            const TroughSample& to = raw[i];
            for (int p = 1; p <= pieces; ++p) {
              const proportion_t t = static_cast<float> (p) /
                                     static_cast<float> (pieces) *
                                     proportion[one];
              spine.push_back (
                { .at = plan_position (
                    moppe::lerp (x_of (from.at), x_of (to.at), t),
                    moppe::lerp (z_of (from.at), z_of (to.at), t)),
                  .area = moppe::lerp (from.area, to.area, t) });
            }
          }
        }
        if (spine.size () < 4)
          continue;

        // Lightly ease sharp grid kinks while keeping the river's natural
        // bends instead of straightening it into an artificial canal.
        for (int pass = 0; pass < 2; ++pass) {
          std::vector<plan_position_t> smoothed (spine.size ());
          smoothed.front () = spine.front ().at;
          smoothed.back () = spine.back ().at;
          for (std::size_t i = 1; i + 1 < spine.size (); ++i)
            smoothed[i] = plan_position (
              0.25f * x_of (spine[i - 1].at) + 0.50f * x_of (spine[i].at) +
                0.25f * x_of (spine[i + 1].at),
              0.25f * z_of (spine[i - 1].at) + 0.50f * z_of (spine[i].at) +
                0.25f * z_of (spine[i + 1].at));
          for (std::size_t i = 0; i < spine.size (); ++i)
            spine[i].at = smoothed[i];
        }

        // Recompute arc distance along the straightened glacial spine.
        spine.front ().distance = 0.0f * u::m;
        for (std::size_t i = 1; i < spine.size (); ++i)
          spine[i].distance =
            spine[i - 1].distance + hypot (spine[i].at - spine[i - 1].at);
        const meters_t total_length = spine.back ().distance;
        if (total_length < 500.0f * u::m)
          continue;

        // Build the gentle, navigable concave long profile from the drowned
        // fjord mouth inland to the highland trough head.
        const meters_t fjord_len =
          std::min (parameters.fjord_inlet_length, 0.22f * total_length);
        const meters_t river_len =
          std::max (100.0f * u::m, total_length - fjord_len);
        const meters_t head_fade =
          std::min (720.0f * u::m, 0.28f * total_length);
        constexpr meters_t dry_bench_clearance = 0.90f * u::m;

        std::vector<SurfaceElevation> natural_y (spine.size ());
        for (std::size_t i = 0; i < spine.size (); ++i) {
          natural_y[i] = ground_at (spine[i].at);
          const meters_t s = spine[i].distance;
          SurfaceElevation ideal;
          if (s <= fjord_len) {
            const proportion_t u = band (0.0f * u::m, fjord_len, s);
            ideal = sea_level -
                    meters_t ((whole_part - u) * parameters.fjord_mouth_depth) +
                    meters_t (u * dry_bench_clearance);
          } else {
            const meters_t sr = s - fjord_len;
            const meters_t rise =
              meters_t (parameters.coastal_grade * sr) +
              meters_t (0.5f *
                        (parameters.highland_grade - parameters.coastal_grade) *
                        sr * (sr / river_len).numerical_value_in (one));
            ideal = sea_level + dry_bench_clearance + rise;
          }
          // Never raise natural ground above itself, and keep freshwater floor
          // cleanly above sea level beyond the drowned fjord mouth so flat
          // benches do not pond into disconnected coastal puddles.
          SurfaceElevation target = std::min (natural_y[i], ideal);
          if (s > fjord_len)
            target = std::max (
              target, std::min (natural_y[i], sea_level + dry_bench_clearance));
          spine[i].floor = target;

          // Trough floor half-width scales smoothly with ice flux (catchment).
          const proportion_t area_share = band (parameters.trough_head_area,
                                                30000000.0f * u::m * u::m,
                                                spine[i].area);
          spine[i].floor_half_width =
            moppe::lerp (parameters.minimum_floor_half_width,
                         parameters.maximum_floor_half_width,
                         area_share);
          // Fade incision strength smoothly at the highland trough head so
          // the valley terminates in a natural glacial headwall amphitheater.
          spine[i].strength =
            whole_part - band (total_length - head_fade, total_length, s);
        }

        // Smooth long profile and enforce gentle non-decreasing rise inland.
        for (int pass = 0; pass < 8; ++pass) {
          std::vector<SurfaceElevation> eased = { spine.front ().floor };
          eased.reserve (spine.size ());
          for (std::size_t i = 1; i + 1 < spine.size (); ++i)
            eased.push_back (
              SurfaceElevation (0.25f * spine[i - 1].floor.delta_from_zero () +
                                0.50f * spine[i].floor.delta_from_zero () +
                                0.25f * spine[i + 1].floor.delta_from_zero ()));
          eased.push_back (spine.back ().floor);
          for (std::size_t i = 0; i < spine.size (); ++i)
            spine[i].floor = std::min (natural_y[i], eased[i]);
        }
        for (std::size_t i = 1; i < spine.size (); ++i) {
          const meters_t seg = spine[i].distance - spine[i - 1].distance;
          const SurfaceElevation minimum_rise =
            spine[i - 1].floor + meters_t (0.0012f * grade[one] * seg);
          if (spine[i].floor < minimum_rise)
            spine[i].floor = std::min (natural_y[i], minimum_rise);
        }

        // Stamp the U-shaped trough cross-section using the closest point on
        // the spine (no periodic scalloping or overlapping end-cap ponds),
        // modulated by domain noise so valley walls merge naturally into the
        // surrounding ridges.
        constexpr meters_t wall_reach = 320.0f * u::m;
        const std::uint32_t trough_seed =
          peak_hash (static_cast<std::uint32_t> (result.troughs + 1), 0x7a31u);
        std::vector<meters_t> trough_best_side (count, 1e9f * u::m);
        std::vector<SurfaceElevation> trough_target = formed;
        std::vector<proportion_t> trough_weight (count, 0.0f * proportion[one]);
        std::vector<proportion_t> trough_outwash_share (count,
                                                        0.0f * proportion[one]);
        for (std::size_t seg = 0; seg + 1 < spine.size (); ++seg) {
          const TroughSample& a = spine[seg];
          const TroughSample& b = spine[seg + 1];
          const plan_displacement_t way = b.at - a.at;
          const meters_t run = hypot (way);
          if (run < 1e-3f * u::m)
            continue;
          const meters_t ax_m = x_of (a.at);
          const meters_t az_m = z_of (a.at);
          const meters_t bx_m = x_of (b.at);
          const meters_t bz_m = z_of (b.at);
          const meters_t run_x = x_of (way);
          const meters_t run_z = z_of (way);
          const meters_t max_radius =
            1.25f * std::max (a.floor_half_width, b.floor_half_width) +
            wall_reach;
          const int min_x = static_cast<int> (
            std::floor (((std::min (ax_m, bx_m) - max_radius) / spacing_x)
                          .numerical_value_in (one)));
          const int max_x = static_cast<int> (
            std::ceil (((std::max (ax_m, bx_m) + max_radius) / spacing_x)
                         .numerical_value_in (one)));
          const int min_z = static_cast<int> (
            std::floor (((std::min (az_m, bz_m) - max_radius) / spacing_z)
                          .numerical_value_in (one)));
          const int max_z = static_cast<int> (
            std::ceil (((std::max (az_m, bz_m) + max_radius) / spacing_z)
                         .numerical_value_in (one)));
          for (int iz = min_z; iz <= max_z; ++iz)
            for (int ix = min_x; ix <= max_x; ++ix) {
              const meters_t wx = static_cast<float> (ix) * spacing_x;
              const meters_t wz = static_cast<float> (iz) * spacing_z;
              const meters_t along =
                ((wx - ax_m) * run_x + (wz - az_m) * run_z) / run;
              const proportion_t t =
                std::clamp (proportion_t (along / run), zero_part, whole_part);
              const meters_t cx = moppe::lerp (ax_m, bx_m, t);
              const meters_t cz = moppe::lerp (az_m, bz_m, t);
              const meters_t side =
                hypot (plan_displacement (wx - cx, wz - cz));
              const std::size_t cell =
                static_cast<std::size_t> (wrap_index (iz, height)) * width +
                wrap_index (ix, width);
              if (side >= trough_best_side[cell])
                continue;
              const proportion_t w_noise =
                smooth_noise_octave (wx, wz, 220.0f * u::m, trough_seed);
              const scale_factor_t width_mod =
                (0.84f + 0.32f * w_noise.numerical_value_in (one)) *
                scale_factor[one];
              const meters_t floor_hw = meters_t (
                moppe::lerp (a.floor_half_width, b.floor_half_width, t) *
                width_mod);
              if (side >= floor_hw + wall_reach)
                continue;
              const proportion_t strength =
                moppe::lerp (a.strength, b.strength, t);
              if (strength <= 1e-4f * proportion[one])
                continue;
              const SurfaceElevation floor_y =
                moppe::lerp (a.floor, b.floor, t);
              SurfaceElevation profile_y;
              proportion_t outwash_share = 0.0f * proportion[one];
              if (side <= floor_hw) {
                const proportion_t u = side / floor_hw;
                // Distinct center dish guides the post-glacial river down one
                // clean thalweg while leaving dry alluvial benches beside it.
                profile_y =
                  floor_y + meters_t (0.95f * u::m * proportion_t (u * u));
                outwash_share = proportion_t (
                  strength *
                  (whole_part - band (0.70f * floor_hw, floor_hw, side)));
              } else {
                const meters_t delta = side - floor_hw;
                const proportion_t steepen =
                  band (0.0f * u::m, 110.0f * u::m, delta);
                const proportion_t s_noise = smooth_noise_octave (
                  wx, wz, 180.0f * u::m, trough_seed + 19u);
                const scale_factor_t slope_mod =
                  (0.80f + 0.42f * s_noise.numerical_value_in (one)) *
                  scale_factor[one];
                const slope_t local_slope =
                  slope_t (parameters.wall_slope * slope_mod) *
                  (0.28f + 0.72f * steepen.numerical_value_in (one));
                profile_y =
                  floor_y + 0.95f * u::m + meters_t (local_slope * delta);
              }
              const SurfaceElevation natural = elevations[cell];
              if (profile_y >= natural)
                continue;
              const proportion_t outer_fade =
                whole_part - band (floor_hw + 0.55f * wall_reach,
                                   floor_hw + wall_reach,
                                   side);
              const proportion_t blend = proportion_t (strength * outer_fade);
              trough_best_side[cell] = side;
              trough_target[cell] = moppe::lerp (natural, profile_y, blend);
              trough_weight[cell] = blend;
              trough_outwash_share[cell] = outwash_share;
            }
        }
        for (std::size_t cell = 0; cell < count; ++cell) {
          if (trough_weight[cell] <= 0.0f * proportion[one])
            continue;
          if (trough_target[cell] < formed[cell]) {
            formed[cell] = trough_target[cell];
            carved_mask[cell] = 1;
            if (trough_outwash_share[cell] > 0.0f * proportion[one] &&
                !flood.ocean[cell]) {
              const SedimentThickness fill = quantity_cast<sediment_thickness> (
                meters_t (parameters.valley_floor_outwash *
                          trough_outwash_share[cell]));
              outwash[cell] = std::max (outwash[cell], fill);
            }
          }
        }

        carved_mouths.push_back (mouth_pos);
        ++result.troughs;
      }
    }

    // -------------------------------------------------------------------------
    // 2. High kalfjäll snowfield massif & descending glacier snow valley:
    //    over the world's broadest alpine massif, shape an organically lobed,
    //    gently rolling ice-cap plateau solidly above the snow line, feeding
    //    a sweeping, banked snow-filled glacier valley down toward tree line.
    // -------------------------------------------------------------------------
    std::vector<std::uint8_t> snowfield_mask (count, 0);
    const std::vector<std::uint8_t> trough_mask = carved_mask;
    if (parameters.sculpt_snowfield) {
      MOPPE_PROFILE_ZONE ("glacial.snowfield");
      SurfaceElevation highest = sea_level;
      for (std::size_t cell = 0; cell < count; ++cell)
        if (!flood.ocean[cell] && formed[cell] > highest)
          highest = formed[cell];
      const meters_t land_relief (highest - sea_level);
      const meters_t world_span = std::min (period_x, period_z);
      if (land_relief >= 70.0f * u::m &&
          world_span >= 1.8f * parameters.snowfield_radius_along) {
        const int stride =
          std::max (1,
                    static_cast<int> (std::lround (
                      (64.0f * u::m / cell_reach).numerical_value_in (one))));
        const int probe_step =
          std::max (1,
                    static_cast<int> (std::lround (
                      (std::min (120.0f * u::m,
                                 0.22f * parameters.snowfield_radius_along) /
                       cell_reach)
                        .numerical_value_in (one))));
        bool found_massif = false;
        int best_x = 0;
        int best_z = 0;
        SurfaceElevation best_mean = sea_level + meters_t (0.42f * land_relief);
        for (int z = 0; z < height; z += stride)
          for (int x = 0; x < width; x += stride) {
            const std::size_t center = static_cast<std::size_t> (z) * width + x;
            if (flood.ocean[center] || trough_mask[center])
              continue;
            bool clear = true;
            double sum = 0.0;
            for (int dz = -2; dz <= 2 && clear; ++dz)
              for (int dx = -2; dx <= 2 && clear; ++dx) {
                const std::size_t near =
                  static_cast<std::size_t> (
                    wrap_index (z + dz * probe_step, height)) *
                    width +
                  wrap_index (x + dx * probe_step, width);
                if (flood.ocean[near] || trough_mask[near]) {
                  clear = false;
                  break;
                }
                sum +=
                  static_cast<double> (surface_elevation_value (formed[near]));
              }
            if (!clear)
              continue;
            const SurfaceElevation mean_y =
              surface_elevation_point (static_cast<float> (sum / 25.0) * u::m);
            if (mean_y > best_mean) {
              best_mean = mean_y;
              best_x = x;
              best_z = z;
              found_massif = true;
            }
          }

        if (found_massif) {
          const meters_t anchor_x = static_cast<float> (best_x) * spacing_x;
          const meters_t anchor_z = static_cast<float> (best_z) * spacing_z;
          const std::uint32_t massif_hash =
            peak_hash (static_cast<std::uint32_t> (best_x),
                       static_cast<std::uint32_t> (best_z));
          const auto hash_phase = [&] (std::uint32_t shift) -> radians_t {
            return static_cast<float> ((massif_hash >> shift) & 0xffu) *
                   (6.2831853f / 256.0f) * u::rad;
          };
          // Align the snowfield's long axis with the massif's own ridge
          // elongation (second-moment tensor of height above the floor).
          quantity<one, double> sxx = 0.0 * one;
          quantity<one, double> szz = 0.0 * one;
          quantity<one, double> sxz = 0.0 * one;
          const int tensor_step = std::max (1, probe_step);
          for (int dz = -3; dz <= 3; ++dz)
            for (int dx = -3; dx <= 3; ++dx) {
              const std::size_t near =
                static_cast<std::size_t> (
                  wrap_index (best_z + dz * tensor_step, height)) *
                  width +
                wrap_index (best_x + dx * tensor_step, width);
              const quantity<one, double> w =
                std::max (0.0,
                          static_cast<double> (
                            meters_value (meters_t (formed[near] - best_mean)) +
                            40.0f)) *
                one;
              const quantity<one, double> ox = static_cast<double> (dx) * one;
              const quantity<one, double> oz = static_cast<double> (dz) * one;
              sxx += w * ox * ox;
              szz += w * oz * oz;
              sxz += w * ox * oz;
            }
          const radians_t axis_angle =
            0.5f *
            cr::atan2 (
              static_cast<float> (2.0 * sxz.numerical_value_in (one)),
              static_cast<float> ((sxx - szz).numerical_value_in (one))) *
            u::rad;
          const quantity<one, float> axis_x =
            cr::cos (radians_value (axis_angle)) * one;
          const quantity<one, float> axis_z =
            cr::sin (radians_value (axis_angle)) * one;
          const radians_t phase_a = hash_phase (0);
          const radians_t phase_b = hash_phase (8);
          const radians_t phase_c = hash_phase (16);

          const meters_t r_along = parameters.snowfield_radius_along;
          const meters_t r_across = parameters.snowfield_radius_across;
          const SurfaceElevation plateau_base =
            sea_level +
            meters_t (parameters.snowfield_relief_share * land_relief);
          const SurfaceElevation plateau_floor =
            sea_level +
            meters_t (
              std::max (
                0.74f,
                parameters.snowfield_relief_share.numerical_value_in (one) -
                  0.07f) *
              land_relief);
          const SurfaceElevation plateau_cap =
            sea_level +
            meters_t (
              std::min (
                0.93f,
                parameters.snowfield_relief_share.numerical_value_in (one) +
                  0.08f) *
              land_relief);
          const meters_t crown_height =
            std::min (18.0f * u::m, 0.065f * land_relief);
          const meters_t swell_scale =
            std::min (
              1.0f, (land_relief / (220.0f * u::m)).numerical_value_in (one)) *
            u::m;
          constexpr scale_factor_t outer_ratio = 1.58f * scale_factor[one];
          const meters_t bound_r = 1.30f *
                                   outer_ratio.numerical_value_in (one) *
                                   std::max (r_along, r_across);
          const int min_x = static_cast<int> (std::floor (
            ((anchor_x - bound_r) / spacing_x).numerical_value_in (one)));
          const int max_x = static_cast<int> (std::ceil (
            ((anchor_x + bound_r) / spacing_x).numerical_value_in (one)));
          const int min_z = static_cast<int> (std::floor (
            ((anchor_z - bound_r) / spacing_z).numerical_value_in (one)));
          const int max_z = static_cast<int> (std::ceil (
            ((anchor_z + bound_r) / spacing_z).numerical_value_in (one)));

          const auto plateau_lobe = [&] (radians_t theta) -> scale_factor_t {
            const float a = radians_value (theta);
            return (1.0f +
                    0.15f * cr::sin (2.0f * a + radians_value (phase_a)) +
                    0.10f * cr::cos (3.0f * a + radians_value (phase_b)) +
                    0.05f * cr::sin (5.0f * a + radians_value (phase_c))) *
                   scale_factor[one];
          };

          for (int iz = min_z; iz <= max_z; ++iz)
            for (int ix = min_x; ix <= max_x; ++ix) {
              const std::size_t cell =
                static_cast<std::size_t> (wrap_index (iz, height)) * width +
                wrap_index (ix, width);
              if (flood.ocean[cell] || trough_mask[cell])
                continue;
              const meters_t wx = static_cast<float> (ix) * spacing_x;
              const meters_t wz = static_cast<float> (iz) * spacing_z;
              // Domain-warp the massif footprint so the snowfield perimeter
              // follows natural ridges and spurs instead of a smooth oval.
              const meters_t warp_x =
                (smooth_noise_octave (wx, wz, 340.0f * u::m, massif_hash + 5u)
                   .numerical_value_in (one) -
                 0.5f) *
                  210.0f * u::m +
                (smooth_noise_octave (wx, wz, 145.0f * u::m, massif_hash + 11u)
                   .numerical_value_in (one) -
                 0.5f) *
                  85.0f * u::m;
              const meters_t warp_z =
                (smooth_noise_octave (wx, wz, 340.0f * u::m, massif_hash + 17u)
                   .numerical_value_in (one) -
                 0.5f) *
                  210.0f * u::m +
                (smooth_noise_octave (wx, wz, 145.0f * u::m, massif_hash + 23u)
                   .numerical_value_in (one) -
                 0.5f) *
                  85.0f * u::m;
              const meters_t rx = wx + warp_x - anchor_x;
              const meters_t rz = wz + warp_z - anchor_z;
              const meters_t u_along = rx * axis_x + rz * axis_z;
              const meters_t u_across = -rx * axis_z + rz * axis_x;
              const radians_t theta =
                cr::atan2 (meters_value (u_across), meters_value (u_along)) *
                u::rad;
              const scale_factor_t lobe = plateau_lobe (theta);
              const quantity<one, float> nx =
                u_along / meters_t (r_along * lobe);
              const quantity<one, float> nz =
                u_across / meters_t (r_across * lobe);
              const scale_factor_t r_norm =
                cr::hypot (nx.numerical_value_in (one),
                           nz.numerical_value_in (one)) *
                scale_factor[one];
              if (r_norm >= outer_ratio)
                continue;

              // Gently convex ice-cap crown with subtle fractal snow-drift
              // undulation (never deep periodic hollows that pond into
              // artificial summit lakes).
              const proportion_t drift_a =
                smooth_noise_octave (wx, wz, 220.0f * u::m, massif_hash + 31u);
              const proportion_t drift_b =
                smooth_noise_octave (wx, wz, 95.0f * u::m, massif_hash + 47u);
              const quantity<one, float> swell =
                (3.6f * (drift_a.numerical_value_in (one) - 0.5f) +
                 1.6f * (drift_b.numerical_value_in (one) - 0.5f)) *
                one;
              const proportion_t crown_share =
                std::max (0.0f,
                          1.0f - r_norm.numerical_value_in (one) *
                                   r_norm.numerical_value_in (one)) *
                proportion[one];
              const SurfaceElevation dome_y = std::clamp (
                plateau_base + meters_t (crown_height * crown_share) +
                  swell * swell_scale,
                plateau_floor,
                plateau_cap);

              const proportion_t core_w =
                whole_part -
                band (0.68f * proportion[one],
                      whole_part,
                      std::clamp (r_norm.numerical_value_in (one), 0.0f, 1.0f) *
                        proportion[one]);
              const proportion_t skirt_w =
                whole_part - band (0.76f * scale_factor[one],
                                   1.42f * scale_factor[one],
                                   r_norm);
              const SurfaceElevation existing = formed[cell];
              SurfaceElevation target;
              if (existing >= dome_y) {
                // Keep towering alpine summit horns standing proudly around
                // the snowfield rim while easing sharp interior crags into
                // rideable rolling snow swells.
                const SurfaceElevation softened =
                  dome_y + 0.30f * (existing - dome_y);
                target = moppe::lerp (existing, softened, core_w);
              } else {
                // Preserve natural ridge texture on the outer skirt instead of
                // stamping smooth concentric contours over the flanks.
                const proportion_t lift_w = proportion_t (
                  skirt_w * (0.58f * proportion[one] + 0.42f * core_w));
                target = moppe::lerp (existing, dome_y, lift_w);
              }
              formed[cell] = target;
              if (core_w > 0.20f * proportion[one])
                snowfield_mask[cell] = 1;
              if (core_w > 0.05f * proportion[one]) {
                const SedimentThickness cover =
                  quantity_cast<sediment_thickness> (
                    meters_t (parameters.snow_basin_cover * core_w));
                outwash[cell] = std::max (outwash[cell], cover);
              }
            }

          // Sculpt a sweeping descending glacier snow valley (glaciärtunga)
          // off the leeward flank of the kalfjäll snowfield down toward the
          // upper tree line, giving a continuous, banked alpine snow corridor.
          bool found_exit = false;
          radians_t best_exit = 0.0f * u::rad;
          const meters_t valley_len =
            std::min (parameters.snow_valley_length, 0.35f * world_span);
          if (valley_len >= 300.0f * u::m) {
            scale_factor_t best_score = -1e9f * scale_factor[one];
            const SurfaceElevation target_exit_y =
              sea_level +
              meters_t (parameters.snow_valley_exit_relief_share * land_relief);
            for (int k = 0; k < 16; ++k) {
              const radians_t bearing =
                (static_cast<float> (k) * 6.2831853f / 16.0f) * u::rad;
              const quantity<one, float> bx =
                cr::cos (radians_value (bearing)) * one;
              const quantity<one, float> bz =
                cr::sin (radians_value (bearing)) * one;
              const meters_t start_r = 0.45f * r_across;
              bool clear = true;
              for (int step_i = 1; step_i <= 12 && clear; ++step_i) {
                const meters_t dist =
                  start_r + valley_len * (static_cast<float> (step_i) / 12.0f);
                const meters_t px = anchor_x + dist * bx;
                const meters_t pz = anchor_z + dist * bz;
                const int cx = static_cast<int> (
                  std::lround ((px / spacing_x).numerical_value_in (one)));
                const int cz = static_cast<int> (
                  std::lround ((pz / spacing_z).numerical_value_in (one)));
                for (int dz = -2; dz <= 2 && clear; ++dz)
                  for (int dx = -2; dx <= 2 && clear; ++dx) {
                    const std::size_t near =
                      static_cast<std::size_t> (
                        wrap_index (cz + dz * probe_step, height)) *
                        width +
                      wrap_index (cx + dx * probe_step, width);
                    if (flood.ocean[near] || trough_mask[near])
                      clear = false;
                  }
              }
              if (!clear)
                continue;
              const plan_position_t end_pt =
                plan_position (anchor_x + (start_r + valley_len) * bx,
                               anchor_z + (start_r + valley_len) * bz);
              const SurfaceElevation end_y = ground_at (end_pt);
              // Prefer cool leeward NNE-NW-E exits descending near the tree
              // line without dropping off a sheer abyss.
              const scale_factor_t lee_pref =
                (-0.85f * bz.numerical_value_in (one) +
                 0.55f * bx.numerical_value_in (one)) *
                scale_factor[one];
              const scale_factor_t height_fit =
                (-abs (meters_t (end_y - target_exit_y)) /
                 std::max (land_relief, 1.0f * u::m))
                  .numerical_value_in (one) *
                scale_factor[one];
              const scale_factor_t score = 1.6f * height_fit + 0.45f * lee_pref;
              if (score > best_score) {
                best_score = score;
                best_exit = bearing;
                found_exit = true;
              }
            }

            if (found_exit) {
              const quantity<one, float> ex =
                cr::cos (radians_value (best_exit)) * one;
              const quantity<one, float> ez =
                cr::sin (radians_value (best_exit)) * one;
              const quantity<one, float> sx = -ez;
              const quantity<one, float> sz = ex;
              const meters_t start_r = 0.48f * r_across;
              const quantity<one, float> curve_sign =
                ((massif_hash & 1u) ? 1.0f : -1.0f) * one;
              const meters_t curve_amp =
                std::min (115.0f * u::m, 0.09f * valley_len);
              constexpr std::size_t stations = 28;
              struct ValleyStation {
                plan_position_t at;
                SurfaceElevation floor;
                meters_t half_width;
                proportion_t strength;
              };
              std::vector<ValleyStation> chute (stations);
              const plan_position_t start_pt = plan_position (
                anchor_x + start_r * ex, anchor_z + start_r * ez);
              const plan_position_t end_pt =
                plan_position (anchor_x + (start_r + valley_len) * ex,
                               anchor_z + (start_r + valley_len) * ez);
              const SurfaceElevation head_y =
                std::max (plateau_floor, ground_at (start_pt));
              const SurfaceElevation natural_end_y = ground_at (end_pt);
              const SurfaceElevation foot_y =
                std::clamp (std::min (natural_end_y, target_exit_y),
                            sea_level + 0.48f * land_relief,
                            head_y - 24.0f * u::m);
              for (std::size_t i = 0; i < stations; ++i) {
                const proportion_t u = static_cast<float> (i) /
                                       static_cast<float> (stations - 1) *
                                       proportion[one];
                const meters_t dist = start_r + meters_t (valley_len * u);
                const meters_t sway =
                  curve_sign * curve_amp *
                  cr::sin (3.14159265f * u.numerical_value_in (one));
                chute[i].at = plan_position (anchor_x + dist * ex + sway * sx,
                                             anchor_z + dist * ez + sway * sz);
                // Smooth concave alpine descent: gentle lip leaving the
                // plateau, swooping middle run, easing out at the lower mouth.
                const proportion_t eased =
                  proportion_t (u * u * (3.0f * whole_part - 2.0f * u));
                chute[i].floor = moppe::lerp (head_y, foot_y, eased);
                // Broad upper funnel narrowing into a dramatic banked glacier
                // pass, then flaring slightly at the foot.
                const scale_factor_t width_mod =
                  (1.18f -
                   0.24f * cr::sin (3.14159265f * u.numerical_value_in (one))) *
                  scale_factor[one];
                chute[i].half_width =
                  meters_t (parameters.snow_valley_half_width * width_mod);
                // Feather seamlessly at both ends into the plateau & fell.
                const proportion_t head_in =
                  band (0.0f * proportion[one], 0.14f * proportion[one], u);
                const proportion_t foot_out =
                  whole_part - band (0.82f * proportion[one], whole_part, u);
                chute[i].strength = proportion_t (head_in * foot_out);
              }

              // Stamp the descending glacier snow valley by closest point on
              // the polyline, domain-warped with organic noise and carving
              // naturally into the mountain slope (never building a raised
              // artificial ramp out across lowland lakes).
              constexpr slope_t chute_wall_slope = 0.42f * terrain_slope[one];
              constexpr meters_t chute_wall_reach = 220.0f * u::m;
              const std::uint32_t chute_seed = massif_hash ^ 0x39b17c4du;
              std::vector<meters_t> chute_best_side (count, 1e9f * u::m);
              std::vector<SurfaceElevation> chute_target = formed;
              std::vector<proportion_t> chute_weight (count,
                                                      0.0f * proportion[one]);
              std::vector<proportion_t> chute_floor_share (
                count, 0.0f * proportion[one]);

              for (std::size_t seg = 0; seg + 1 < chute.size (); ++seg) {
                const ValleyStation& a = chute[seg];
                const ValleyStation& b = chute[seg + 1];
                const plan_displacement_t way = b.at - a.at;
                const meters_t run = hypot (way);
                if (run < 1e-3f * u::m)
                  continue;
                const meters_t ax_m = x_of (a.at);
                const meters_t az_m = z_of (a.at);
                const meters_t bx_m = x_of (b.at);
                const meters_t bz_m = z_of (b.at);
                const meters_t run_x = x_of (way);
                const meters_t run_z = z_of (way);
                const meters_t max_r = std::max (a.half_width, b.half_width) +
                                       chute_wall_reach + 48.0f * u::m;
                const int smin_x = static_cast<int> (
                  std::floor (((std::min (ax_m, bx_m) - max_r) / spacing_x)
                                .numerical_value_in (one)));
                const int smax_x = static_cast<int> (
                  std::ceil (((std::max (ax_m, bx_m) + max_r) / spacing_x)
                               .numerical_value_in (one)));
                const int smin_z = static_cast<int> (
                  std::floor (((std::min (az_m, bz_m) - max_r) / spacing_z)
                                .numerical_value_in (one)));
                const int smax_z = static_cast<int> (
                  std::ceil (((std::max (az_m, bz_m) + max_r) / spacing_z)
                               .numerical_value_in (one)));
                for (int iz = smin_z; iz <= smax_z; ++iz)
                  for (int ix = smin_x; ix <= smax_x; ++ix) {
                    const std::size_t cell =
                      static_cast<std::size_t> (wrap_index (iz, height)) *
                        width +
                      wrap_index (ix, width);
                    if (flood.ocean[cell] || trough_mask[cell])
                      continue;
                    const meters_t raw_x = static_cast<float> (ix) * spacing_x;
                    const meters_t raw_z = static_cast<float> (iz) * spacing_z;
                    const meters_t wx =
                      raw_x + (smooth_noise_octave (
                                 raw_x, raw_z, 150.0f * u::m, chute_seed)
                                 .numerical_value_in (one) -
                               0.5f) *
                                58.0f * u::m;
                    const meters_t wz =
                      raw_z + (smooth_noise_octave (
                                 raw_x, raw_z, 150.0f * u::m, chute_seed + 13u)
                                 .numerical_value_in (one) -
                               0.5f) *
                                58.0f * u::m;
                    const meters_t along =
                      ((wx - ax_m) * run_x + (wz - az_m) * run_z) / run;
                    const proportion_t t =
                      std::clamp (proportion_t (along / run),
                                  0.0f * proportion[one],
                                  whole_part);
                    const meters_t side = hypot (
                      plan_displacement (wx - moppe::lerp (ax_m, bx_m, t),
                                         wz - moppe::lerp (az_m, bz_m, t)));
                    if (side >= chute_best_side[cell])
                      continue;
                    const meters_t hw =
                      moppe::lerp (a.half_width, b.half_width, t);
                    if (side >= hw + chute_wall_reach)
                      continue;
                    const proportion_t strength =
                      moppe::lerp (a.strength, b.strength, t);
                    if (strength <= 1e-4f * proportion[one])
                      continue;
                    const proportion_t tex = smooth_noise_octave (
                      raw_x, raw_z, 120.0f * u::m, chute_seed + 29u);
                    const meters_t drift_h =
                      (tex.numerical_value_in (one) - 0.5f) * 5.0f * u::m;
                    const SurfaceElevation base_y =
                      moppe::lerp (a.floor, b.floor, t) + drift_h;
                    // Only carve or softly fill shallow hollows (< 6 m): never
                    // extrude an elevated viaduct above lowland valleys/lakes.
                    const SurfaceElevation existing = formed[cell];
                    const proportion_t support = band (
                      base_y - 14.0f * u::m, base_y - 2.0f * u::m, existing);
                    const proportion_t w = proportion_t (
                      strength * support *
                      (whole_part - band (hw, hw + chute_wall_reach, side)));
                    if (w <= 1e-4f * proportion[one])
                      continue;
                    SurfaceElevation profile_y;
                    proportion_t floor_share = 0.0f * proportion[one];
                    if (side <= hw) {
                      const proportion_t frac = side / hw;
                      // Gentle concave dish cradles the snow corridor like a
                      // natural glacier chute without damming downstream flow.
                      profile_y =
                        base_y +
                        meters_t (1.1f * u::m * proportion_t (frac * frac));
                      profile_y = std::min (profile_y, existing + 4.0f * u::m);
                      floor_share = proportion_t (
                        strength * support *
                        (whole_part - band (0.65f * hw, hw, side)));
                    } else {
                      const meters_t out = side - hw;
                      const proportion_t wall_u = out / chute_wall_reach;
                      const meters_t rise =
                        1.1f * u::m +
                        meters_t (
                          chute_wall_slope * out *
                          (0.30f + 0.70f * wall_u.numerical_value_in (one)));
                      const SurfaceElevation banked = base_y + rise;
                      profile_y = std::min (existing, banked);
                    }
                    chute_best_side[cell] = side;
                    chute_weight[cell] = w;
                    chute_target[cell] = profile_y;
                    chute_floor_share[cell] = floor_share;
                  }
              }

              for (std::size_t cell = 0; cell < count; ++cell) {
                const proportion_t w = chute_weight[cell];
                if (w <= 0.0f * proportion[one])
                  continue;
                formed[cell] =
                  moppe::lerp (formed[cell], chute_target[cell], w);
                if (chute_floor_share[cell] > 0.15f * proportion[one])
                  snowfield_mask[cell] = 1;
                if (chute_floor_share[cell] > 0.05f * proportion[one]) {
                  const SedimentThickness cover =
                    quantity_cast<sediment_thickness> (meters_t (
                      parameters.snow_basin_cover * chute_floor_share[cell]));
                  outwash[cell] = std::max (outwash[cell], cover);
                }
              }
            }
          }

          // Sculpt a dramatic crescent of jagged alpine horn peaks (alptoppar)
          // and knife-edge arêtes ringing ~195 deg of the inland snowfield rim,
          // while keeping the opposite lowland drop-off vista and the
          // descending glacier chute completely open.
          if (parameters.sculpt_alpine_horns &&
              parameters.alpine_horn_prominence > 10.0f * u::m) {
            MOPPE_PROFILE_ZONE ("glacial.alpine_horns");
            const scale_factor_t relief_factor =
              std::clamp (
                (land_relief / (280.0f * u::m)).numerical_value_in (one),
                0.45f,
                1.20f) *
              scale_factor[one];
            const meters_t base_prominence =
              meters_t (parameters.alpine_horn_prominence * relief_factor);

            // Pick the best crescent centre: clear of ocean, carved U-troughs,
            // and the descending snow valley, preferring the highest inland
            // backing so the lowest scenic drop-off stays wide open.
            bool found_crest = false;
            radians_t crest_center = 0.0f * u::rad;
            meters_t best_crest_score = -1e9f * u::m;
            const auto sample_arc_clear = [&] (radians_t center_ang,
                                               radians_t min_exit_sep) {
              if (found_exit && abs (wrapped_angle_diff (
                                  center_ang, best_exit)) < min_exit_sep)
                return std::pair<bool, meters_t> (false, 0.0f * u::m);
              meters_t arc_sum = 0.0f * u::m;
              meters_t opp_sum = 0.0f * u::m;
              int arc_count = 0;
              for (int s = -2; s <= 2; ++s) {
                const radians_t ang =
                  center_ang + static_cast<float> (s) * 0.68f * u::rad;
                const scale_factor_t lobe =
                  plateau_lobe (wrapped_angle_diff (ang, axis_angle));
                const quantity<one, float> ca =
                  cr::cos (radians_value (ang)) * one;
                const quantity<one, float> sa =
                  cr::sin (radians_value (ang)) * one;
                const meters_t rad_here =
                  0.5f * (r_along + r_across) * lobe.numerical_value_in (one);
                for (const float mult : { 1.12f, 1.48f }) {
                  const meters_t px = anchor_x + (mult * rad_here) * ca;
                  const meters_t pz = anchor_z + (mult * rad_here) * sa;
                  const int cx = static_cast<int> (
                    std::lround ((px / spacing_x).numerical_value_in (one)));
                  const int cz = static_cast<int> (
                    std::lround ((pz / spacing_z).numerical_value_in (one)));
                  const std::size_t cell =
                    static_cast<std::size_t> (wrap_index (cz, height)) * width +
                    wrap_index (cx, width);
                  if (flood.ocean[cell] || trough_mask[cell])
                    return std::pair<bool, meters_t> (false, 0.0f * u::m);
                  arc_sum +=
                    quantity_cast<isq::length> (formed[cell] - sea_level);
                  ++arc_count;
                }
                // Check opposite side elevation so lower open vistas win.
                const meters_t ox = anchor_x - (1.35f * rad_here) * ca;
                const meters_t oz = anchor_z - (1.35f * rad_here) * sa;
                const int ocx = static_cast<int> (
                  std::lround ((ox / spacing_x).numerical_value_in (one)));
                const int ocz = static_cast<int> (
                  std::lround ((oz / spacing_z).numerical_value_in (one)));
                const std::size_t ocell =
                  static_cast<std::size_t> (wrap_index (ocz, height)) * width +
                  wrap_index (ocx, width);
                opp_sum +=
                  quantity_cast<isq::length> (formed[ocell] - sea_level);
              }
              const meters_t mean_arc =
                arc_sum / static_cast<float> (std::max (1, arc_count));
              const meters_t mean_opp = opp_sum / 5.0f;
              return std::pair<bool, meters_t> (true,
                                                mean_arc - 1.15f * mean_opp);
            };

            for (const radians_t sep : { 1.85f * u::rad, 1.45f * u::rad }) {
              for (int k = 0; k < 16; ++k) {
                const radians_t cand =
                  (static_cast<float> (k) * 6.2831853f / 16.0f) * u::rad;
                const auto [ok, score] = sample_arc_clear (cand, sep);
                if (ok && score > best_crest_score) {
                  best_crest_score = score;
                  crest_center = cand;
                  found_crest = true;
                }
              }
              if (found_crest)
                break;
            }

            if (found_crest) {
              constexpr radians_t arc_half_span = 1.70f * u::rad; // ~195 deg
              constexpr int primary_count = 7;
              constexpr int outboard_count = 6;
              std::vector<AlpineHorn> horns;
              horns.reserve (primary_count + outboard_count);

              const auto horn_rand = [&] (std::uint32_t idx,
                                          std::uint32_t salt) -> proportion_t {
                const std::uint32_t h = peak_hash (
                  massif_hash ^ (idx * 2246822519u), salt * 3266489917u + 13u);
                return static_cast<float> (h & 0xffffu) * (1.0f / 65535.0f) *
                       proportion[one];
              };

              const auto rim_point = [&] (radians_t world_ang,
                                          scale_factor_t r_scale) {
                const radians_t local =
                  wrapped_angle_diff (world_ang, axis_angle);
                const scale_factor_t lobe = plateau_lobe (local);
                const quantity<one, float> cl =
                  cr::cos (radians_value (local)) * one;
                const quantity<one, float> sl =
                  cr::sin (radians_value (local)) * one;
                const meters_t lu = meters_t (r_along * lobe * r_scale) * cl;
                const meters_t lv = meters_t (r_across * lobe * r_scale) * sl;
                const meters_t rx = lu * axis_x - lv * axis_z;
                const meters_t rz = lu * axis_z + lv * axis_x;
                return std::pair<meters_t, meters_t> (anchor_x + rx,
                                                      anchor_z + rz);
              };

              // 7 broad overlapping alpine massif summits ringing the inland
              // rim; wide bases (400-540 m) merge into a continuous snow-and-
              // rock mountain range instead of isolated cones.
              for (int i = 0; i < primary_count; ++i) {
                const proportion_t t =
                  (static_cast<float> (i) /
                   static_cast<float> (primary_count - 1)) *
                  proportion[one];
                const quantity<one, float> jitter =
                  (horn_rand (static_cast<std::uint32_t> (i), 1u)
                     .numerical_value_in (one) -
                   0.5f) *
                  0.12f * one;
                const quantity<one, float> s =
                  std::clamp (2.0f * t.numerical_value_in (one) - 1.0f +
                                jitter.numerical_value_in (one),
                              -0.98f,
                              0.98f) *
                  one;
                const radians_t ang =
                  crest_center + s.numerical_value_in (one) * arc_half_span;
                const scale_factor_t r_scale =
                  (1.02f +
                   0.16f * horn_rand (static_cast<std::uint32_t> (i), 2u)
                             .numerical_value_in (one)) *
                  scale_factor[one];
                const auto [hx, hz] = rim_point (ang, r_scale);
                const scale_factor_t arch =
                  cr::cos (1.02f * s.numerical_value_in (one)) *
                  scale_factor[one];
                const scale_factor_t var =
                  (0.80f +
                   0.34f * horn_rand (static_cast<std::uint32_t> (i), 3u)
                             .numerical_value_in (one)) *
                  scale_factor[one];
                const meters_t prom = meters_t (base_prominence * arch * var);
                const meters_t rad =
                  (410.0f +
                   130.0f * horn_rand (static_cast<std::uint32_t> (i), 4u)
                              .numerical_value_in (one)) *
                  u::m;
                const radians_t ph =
                  horn_rand (static_cast<std::uint32_t> (i), 5u)
                    .numerical_value_in (one) *
                  6.2831853f * u::rad;
                const int ribs =
                  3 +
                  static_cast<int> (
                    peak_hash (massif_hash, static_cast<std::uint32_t> (i)) %
                    3u);
                horns.push_back ({ .x = hx,
                                   .z = hz,
                                   .radius = rad,
                                   .prominence = prom,
                                   .phase = ph,
                                   .ribs = ribs });
              }

              // 6 secondary outboard ridges & glacial shoulders staggered
              // behind the primary cols for deep multi-layered mountain vistas.
              for (int i = 0; i < outboard_count; ++i) {
                const proportion_t t = ((static_cast<float> (i) + 0.5f) /
                                        static_cast<float> (outboard_count)) *
                                       proportion[one];
                const quantity<one, float> s =
                  (2.0f * t.numerical_value_in (one) - 1.0f) * one;
                const radians_t ang =
                  crest_center +
                  s.numerical_value_in (one) * (0.94f * arc_half_span);
                const scale_factor_t r_scale =
                  (1.30f +
                   0.24f * horn_rand (static_cast<std::uint32_t> (i + 16), 2u)
                             .numerical_value_in (one)) *
                  scale_factor[one];
                const auto [hx, hz] = rim_point (ang, r_scale);
                const scale_factor_t arch =
                  cr::cos (0.98f * s.numerical_value_in (one)) *
                  scale_factor[one];
                const scale_factor_t var =
                  (0.62f +
                   0.34f * horn_rand (static_cast<std::uint32_t> (i + 16), 3u)
                             .numerical_value_in (one)) *
                  scale_factor[one];
                const meters_t prom = meters_t (base_prominence * arch * var);
                const meters_t rad =
                  (370.0f +
                   120.0f * horn_rand (static_cast<std::uint32_t> (i + 16), 4u)
                              .numerical_value_in (one)) *
                  u::m;
                const radians_t ph =
                  horn_rand (static_cast<std::uint32_t> (i + 16), 5u)
                    .numerical_value_in (one) *
                  6.2831853f * u::rad;
                const int ribs =
                  3 + static_cast<int> (
                        peak_hash (massif_hash,
                                   static_cast<std::uint32_t> (i + 32)) %
                        3u);
                horns.push_back ({ .x = hx,
                                   .z = hz,
                                   .radius = rad,
                                   .prominence = prom,
                                   .phase = ph,
                                   .ribs = ribs });
              }

              const meters_t horn_bound =
                2.05f * std::max (r_along, r_across) + 480.0f * u::m;
              const int hmin_x = static_cast<int> (
                std::floor (((anchor_x - horn_bound) / spacing_x)
                              .numerical_value_in (one)));
              const int hmax_x = static_cast<int> (
                std::ceil (((anchor_x + horn_bound) / spacing_x)
                             .numerical_value_in (one)));
              const int hmin_z = static_cast<int> (
                std::floor (((anchor_z - horn_bound) / spacing_z)
                              .numerical_value_in (one)));
              const int hmax_z = static_cast<int> (
                std::ceil (((anchor_z + horn_bound) / spacing_z)
                             .numerical_value_in (one)));
              const std::uint32_t crag_seed = massif_hash ^ 0x51f29a3du;

              for (int iz = hmin_z; iz <= hmax_z; ++iz)
                for (int ix = hmin_x; ix <= hmax_x; ++ix) {
                  const std::size_t cell =
                    static_cast<std::size_t> (wrap_index (iz, height)) * width +
                    wrap_index (ix, width);
                  if (flood.ocean[cell] || trough_mask[cell])
                    continue;
                  const meters_t wx = static_cast<float> (ix) * spacing_x;
                  const meters_t wz = static_cast<float> (iz) * spacing_z;
                  const meters_t rx = wx - anchor_x;
                  const meters_t rz = wz - anchor_z;
                  const meters_t u_along = rx * axis_x + rz * axis_z;
                  const meters_t u_across = -rx * axis_z + rz * axis_x;
                  const radians_t local_theta =
                    cr::atan2 (meters_value (u_across),
                               meters_value (u_along)) *
                    u::rad;
                  const scale_factor_t lobe = plateau_lobe (local_theta);
                  const quantity<one, float> nx =
                    u_along / meters_t (r_along * lobe);
                  const quantity<one, float> nz =
                    u_across / meters_t (r_across * lobe);
                  const scale_factor_t r_norm =
                    cr::hypot (nx.numerical_value_in (one),
                               nz.numerical_value_in (one)) *
                    scale_factor[one];
                  // Keep the wide rideable snowfield interior & frozen tarns
                  // open while broad snow aprons sweep up the surrounding
                  // range.
                  if (r_norm <= 0.62f * scale_factor[one] ||
                      r_norm >= 2.15f * scale_factor[one])
                    continue;

                  const radians_t world_ang =
                    cr::atan2 (meters_value (rz), meters_value (rx)) * u::rad;
                  const radians_t from_crest =
                    abs (wrapped_angle_diff (world_ang, crest_center));
                  if (from_crest >= arc_half_span + 0.55f * u::rad)
                    continue;

                  // Keep the descending glacier snow chute wide open.
                  proportion_t chute_clear = whole_part;
                  if (found_exit) {
                    const radians_t from_exit =
                      abs (wrapped_angle_diff (world_ang, best_exit));
                    chute_clear =
                      band (0.42f * u::rad, 0.76f * u::rad, from_exit);
                    if (chute_clear <= 1e-4f * proportion[one])
                      continue;
                  }

                  const proportion_t inner_fade =
                    band (0.62f * scale_factor[one],
                          0.92f * scale_factor[one],
                          r_norm);
                  const proportion_t outer_fade =
                    whole_part - band (1.28f * scale_factor[one],
                                       2.05f * scale_factor[one],
                                       r_norm);
                  const proportion_t arc_fade =
                    whole_part - band (arc_half_span - 0.12f * u::rad,
                                       arc_half_span + 0.45f * u::rad,
                                       from_crest);

                  // Broad continuous mountain-range backbone linking all
                  // summits with wide snowy cols and sweeping glacier aprons.
                  const proportion_t range_band =
                    proportion_t (inner_fade * outer_fade * arc_fade);
                  meters_t lift =
                    meters_t (base_prominence * 0.56f * range_band);

                  // Blend overlapping broad summit massifs smoothly (never
                  // narrow isolated needle cones).
                  for (const AlpineHorn& horn : horns) {
                    const meters_t dx = wx - horn.x;
                    const meters_t dz = wz - horn.z;
                    const meters_t d = hypot (plan_displacement (dx, dz));
                    if (d >= horn.radius)
                      continue;
                    const radians_t phi =
                      cr::atan2 (meters_value (dz), meters_value (dx)) * u::rad;
                    const proportion_t rib_wave =
                      std::fabs (cr::sin (
                        0.5f * static_cast<float> (horn.ribs) *
                        (radians_value (phi) - radians_value (horn.phase)))) *
                      proportion[one];
                    const scale_factor_t rib_reach =
                      (0.76f +
                       0.24f * (1.0f - rib_wave.numerical_value_in (one))) *
                      scale_factor[one];
                    const meters_t eff_r = meters_t (horn.radius * rib_reach);
                    if (d >= eff_r)
                      continue;
                    const proportion_t u =
                      (1.0f - (d / eff_r).numerical_value_in (one)) *
                      proportion[one];
                    // Broad natural alpine massif profile: sweeping snowy
                    // flanks rising into faceted rocky summits without pinched
                    // shark-fin spikes.
                    const scale_factor_t massif_shape =
                      (0.80f * cr::pow (u.numerical_value_in (one), 1.30f) +
                       0.20f * cr::pow (u.numerical_value_in (one), 1.85f)) *
                      scale_factor[one];
                    const meters_t horn_lift =
                      meters_t (horn.prominence * massif_shape);
                    // Smooth max keeps saddles organic between adjacent peaks.
                    const meters_t diff = abs (horn_lift - lift);
                    const meters_t blend =
                      std::max (0.0f * u::m, 34.0f * u::m - diff);
                    lift = std::max (lift, horn_lift) +
                           (blend * blend) / (136.0f * u::m);
                  }

                  const meters_t masked_lift =
                    meters_t (lift * inner_fade * chute_clear);
                  if (masked_lift <= 0.5f * u::m)
                    continue;

                  // Domain-warped 4-octave ridged multifractal alpine terrain:
                  // carves sweeping glacier couloirs, knife-edge arêtes, and
                  // steep dark-gneiss cliff bands across the snowy massifs.
                  const meters_t warp_x =
                    (smooth_noise_octave (wx, wz, 460.0f * u::m, crag_seed + 3u)
                       .numerical_value_in (one) -
                     0.5f) *
                    220.0f * u::m;
                  const meters_t warp_z =
                    (smooth_noise_octave (wx, wz, 460.0f * u::m, crag_seed + 9u)
                       .numerical_value_in (one) -
                     0.5f) *
                    220.0f * u::m;
                  const meters_t qx = wx + warp_x;
                  const meters_t qz = wz + warp_z;
                  const proportion_t r0 =
                    ridged_noise_octave (qx, qz, 520.0f * u::m, crag_seed);
                  const proportion_t r1 = ridged_noise_octave (
                    qx, qz, 220.0f * u::m, crag_seed + 17u);
                  const proportion_t r2 =
                    ridged_noise_octave (qx, qz, 96.0f * u::m, crag_seed + 53u);
                  const proportion_t r3 =
                    ridged_noise_octave (qx, qz, 44.0f * u::m, crag_seed + 97u);
                  const scale_factor_t w1 =
                    (0.46f + 0.54f * r0.numerical_value_in (one)) *
                    scale_factor[one];
                  const scale_factor_t w2 =
                    (0.40f + 0.60f * r0.numerical_value_in (one)) *
                    scale_factor[one];
                  const proportion_t ridge_fbm =
                    (0.40f * r0.numerical_value_in (one) +
                     0.32f * r1.numerical_value_in (one) *
                       w1.numerical_value_in (one) +
                     0.19f * r2.numerical_value_in (one) *
                       w2.numerical_value_in (one) +
                     0.09f * r3.numerical_value_in (one) *
                       w2.numerical_value_in (one)) *
                    proportion[one];
                  const meters_t sculpted_lift =
                    masked_lift *
                    (0.52f + 0.76f * ridge_fbm.numerical_value_in (one));
                  if (sculpted_lift <= 0.0f * u::m)
                    continue;

                  const proportion_t above_sea_fade =
                    band (sea_level + 4.0f * u::m,
                          sea_level + 28.0f * u::m,
                          formed[cell]);
                  const meters_t final_lift =
                    meters_t (sculpted_lift * above_sea_fade);
                  if (final_lift <= 0.0f * u::m)
                    continue;

                  const SurfaceElevation horn_y = formed[cell] + final_lift;
                  if (horn_y > formed[cell]) {
                    formed[cell] = horn_y;
                    carved_mask[cell] = 1;
                    // Clear soft outwash cover on rising rocky horn faces so
                    // steep gneiss headwalls read crisp beside snow couloirs.
                    if (final_lift > 22.0f * u::m)
                      outwash[cell] = 0.0f * sediment_thickness[u::m];
                  }
                }

              result.horns += horns.size ();
            }
          }

          ++result.snowfields;
        }
      }
    }

    // -------------------------------------------------------------------------
    // 3. Alpine glacial cirques (nisscher) bitten into the lee (NNE-ESE) flanks
    //    of high mountain massifs, sharpening sinusoidal domes into arêtes and
    //    packing wind-sheltered snow drift on their concave floors.
    // -------------------------------------------------------------------------
    if (parameters.maximum_cirques > 0) {
      MOPPE_PROFILE_ZONE ("glacial.cirques");
      const SurfaceElevation min_peak =
        sea_level + parameters.cirque_floor_above_sea + 22.0f * u::m;
      const int stride =
        std::max (1,
                  static_cast<int> (std::lround (
                    (48.0f * u::m / cell_reach).numerical_value_in (one))));
      struct PeakCandidate {
        int x = 0;
        int z = 0;
        SurfaceElevation y = surface_elevation_point (0.0f * u::m);
      };
      std::vector<PeakCandidate> peaks;
      for (int z = 0; z < height; z += stride)
        for (int x = 0; x < width; x += stride) {
          const std::size_t cell = static_cast<std::size_t> (z) * width + x;
          if (carved_mask[cell] || snowfield_mask[cell] ||
              formed[cell] < min_peak)
            continue;
          const SurfaceElevation here = formed[cell];
          bool local_max = true;
          for (int dz = -2 * stride; dz <= 2 * stride && local_max;
               dz += stride)
            for (int dx = -2 * stride; dx <= 2 * stride && local_max;
                 dx += stride) {
              if (dx == 0 && dz == 0)
                continue;
              const std::size_t near =
                static_cast<std::size_t> (wrap_index (z + dz, height)) * width +
                wrap_index (x + dx, width);
              if (formed[near] > here)
                local_max = false;
            }
          if (local_max)
            peaks.push_back ({ .x = x, .z = z, .y = here });
        }

      std::ranges::sort (peaks,
                         [] (const PeakCandidate& a, const PeakCandidate& b) {
                           if (a.y != b.y)
                             return a.y > b.y;
                           return a.z != b.z ? a.z < b.z : a.x < b.x;
                         });

      std::vector<plan_position_t> placed_cirques;
      constexpr meters_t cirque_separation = 560.0f * u::m;
      for (const PeakCandidate& peak : peaks) {
        if (result.cirques >= parameters.maximum_cirques)
          break;
        const plan_position_t peak_at =
          plan_position (static_cast<float> (peak.x) * spacing_x,
                         static_cast<float> (peak.z) * spacing_z);
        bool too_close = false;
        for (const plan_position_t& other : placed_cirques) {
          const meters_t dx =
            minimum_image_delta (x_of (peak_at) - x_of (other), period_x);
          const meters_t dz =
            minimum_image_delta (z_of (peak_at) - z_of (other), period_z);
          if (hypot (plan_displacement (dx, dz)) < cirque_separation) {
            too_close = true;
            break;
          }
        }
        if (too_close)
          continue;

        // Place the cirque armchair on the NNE-ESE leeward flank of the summit
        // where snow drifts accumulate into alpine glaciers.
        const std::uint32_t h = peak_hash (static_cast<std::uint32_t> (peak.x),
                                           static_cast<std::uint32_t> (peak.z));
        const proportion_t u0 = static_cast<float> (h & 0xffffu) *
                                (1.0f / 65536.0f) * proportion[one];
        const proportion_t u1 = static_cast<float> ((h >> 16u) & 0xffffu) *
                                (1.0f / 65536.0f) * proportion[one];
        // Azimuth roughly northeast to east-southeast (0.45..1.35 rad).
        const radians_t lee_angle =
          (0.45f + 0.90f * u0.numerical_value_in (one)) * u::rad;
        const quantity<one, float> dir_x =
          cr::cos (radians_value (lee_angle)) * one;
        const quantity<one, float> dir_z =
          -cr::sin (radians_value (lee_angle)) * one;
        const meters_t radius =
          (125.0f + 60.0f * u1.numerical_value_in (one)) * u::m;
        const meters_t relief (peak.y -
                               (sea_level + parameters.cirque_floor_above_sea));
        const meters_t bowl_depth = std::clamp (
          0.42f * relief + 14.0f * u::m, 18.0f * u::m, 44.0f * u::m);
        const SurfaceElevation bowl_floor = peak.y - bowl_depth;
        const meters_t center_x = x_of (peak_at) + 0.62f * radius * dir_x;
        const meters_t center_z = z_of (peak_at) + 0.62f * radius * dir_z;

        const int min_x = static_cast<int> (std::floor (
          ((center_x - 1.25f * radius) / spacing_x).numerical_value_in (one)));
        const int max_x = static_cast<int> (std::ceil (
          ((center_x + 1.25f * radius) / spacing_x).numerical_value_in (one)));
        const int min_z = static_cast<int> (std::floor (
          ((center_z - 1.25f * radius) / spacing_z).numerical_value_in (one)));
        const int max_z = static_cast<int> (std::ceil (
          ((center_z + 1.25f * radius) / spacing_z).numerical_value_in (one)));
        for (int iz = min_z; iz <= max_z; ++iz)
          for (int ix = min_x; ix <= max_x; ++ix) {
            const meters_t rx = static_cast<float> (ix) * spacing_x - center_x;
            const meters_t rz = static_cast<float> (iz) * spacing_z - center_z;
            const meters_t along_lee = rx * dir_x + rz * dir_z;
            const meters_t across_lee = abs (-rx * dir_z + rz * dir_x);
            // Asymmetric armchair: tight steep crescent headwall toward the
            // summit (along_lee < 0), opening gently down-valley (along_lee >
            // 0).
            const meters_t eff_along =
              along_lee < 0.0f * u::m ? 1.15f * along_lee : 0.72f * along_lee;
            const meters_t dist =
              hypot (plan_displacement (eff_along, across_lee));
            if (dist >= radius)
              continue;
            const proportion_t u = dist / radius;
            // Steep curved headwall rising like u^3 from a broad concave tarn
            // floor, with a subtle downhill lip opening out of the bowl.
            const proportion_t wall_curve = proportion_t (u * u * u);
            const proportion_t opening =
              band (0.0f * u::m, 0.9f * radius, along_lee);
            const SurfaceElevation target =
              bowl_floor +
              meters_t (bowl_depth *
                        (wall_curve * (whole_part - 0.55f * opening)));
            const std::size_t cell =
              static_cast<std::size_t> (wrap_index (iz, height)) * width +
              wrap_index (ix, width);
            if (!flood.ocean[cell] && !snowfield_mask[cell] &&
                target < formed[cell]) {
              formed[cell] = target;
              carved_mask[cell] = 1;
              // Wind-sheltered concave cirque floors accumulate firn & drift
              // snowpack (snölegor), while the plucked headwalls stay bare.
              const proportion_t bowl_share =
                whole_part -
                band (0.42f * proportion[one], 0.72f * proportion[one], u);
              if (bowl_share > 0.05f * proportion[one]) {
                const SedimentThickness cover =
                  quantity_cast<sediment_thickness> (
                    meters_t (parameters.snow_basin_cover * bowl_share));
                outwash[cell] = std::max (outwash[cell], cover);
              }
            }
          }

        placed_cirques.push_back (peak_at);
        ++result.cirques;
      }
    }

    // -------------------------------------------------------------------------
    // 3. Cold-based highland shelf (vidda) preservation polish: lightly ease
    //    gentle upland plateaus outside carved troughs and cirques so high
    //    shelves contrast cleanly against steep glaciated walls and arêtes.
    // -------------------------------------------------------------------------
    {
      MOPPE_PROFILE_ZONE ("glacial.vidda");
      const SurfaceElevation shelf_min = sea_level + parameters.vidda_above_sea;
      const meters_t max_step_x =
        meters_t (parameters.vidda_max_slope * spacing_x);
      const meters_t max_step_z =
        meters_t (parameters.vidda_max_slope * spacing_z);
      std::vector<SurfaceElevation> next_h (count);
      for (int pass = 0; pass < 2; ++pass) {
        next_h = formed;
        for (int z = 0; z < height; ++z)
          for (int x = 0; x < width; ++x) {
            const std::size_t cell = static_cast<std::size_t> (z) * width + x;
            if (carved_mask[cell] || snowfield_mask[cell] ||
                flood.ocean[cell] || formed[cell] < shelf_min)
              continue;
            const std::size_t left =
              static_cast<std::size_t> (z) * width + wrap_index (x - 1, width);
            const std::size_t right =
              static_cast<std::size_t> (z) * width + wrap_index (x + 1, width);
            const std::size_t up =
              static_cast<std::size_t> (wrap_index (z - 1, height)) * width + x;
            const std::size_t down =
              static_cast<std::size_t> (wrap_index (z + 1, height)) * width + x;
            if (carved_mask[left] || carved_mask[right] || carved_mask[up] ||
                carved_mask[down])
              continue;
            if (abs (formed[right] - formed[left]) > 2.0f * max_step_x ||
                abs (formed[down] - formed[up]) > 2.0f * max_step_z)
              continue;
            const ElevationChange avg =
              0.50f * formed[cell].delta_from_zero () +
              0.125f * (formed[left].delta_from_zero () +
                        formed[right].delta_from_zero () +
                        formed[up].delta_from_zero () +
                        formed[down].delta_from_zero ());
            next_h[cell] = SurfaceElevation (avg);
          }
        formed.swap (next_h);
      }
    }

    result.heights = std::move (formed);
    result.change.assign (count, ElevationChange::zero ());
    result.outwash = std::move (outwash);
    const auto cell_area =
      value_cast<double> (spacing_x) * value_cast<double> (spacing_z);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const ElevationChange delta = result.heights[cell] - elevations[cell];
      if (delta == ElevationChange::zero ())
        continue;
      result.change[cell] = delta;
      if (delta < ElevationChange::zero ())
        result.cut += cubic_meters_f64_t (abs (delta) * cell_area);
    }
    return result;
  }
}
