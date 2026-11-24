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

        // Collect unwrapped alignment points ordered mouth -> highlands.
        std::vector<TroughSample> raw;
        const meters_t max_trough_length =
          0.95f * std::min (period_x, period_z);
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

        for (int pass = 0; pass < 6; ++pass) {
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
        // fjord mouth inland to the highland trough head, with one subtle
        // over-deepened glacial ribbon-lake basin (dalsjö) midway up a long
        // valley.
        const meters_t fjord_len =
          std::min (parameters.fjord_inlet_length, 0.25f * total_length);
        const meters_t river_len =
          std::max (100.0f * u::m, total_length - fjord_len);
        const meters_t head_fade =
          std::min (680.0f * u::m, 0.22f * total_length);
        const bool add_ribbon_lake = total_length >= 2400.0f * u::m;
        const meters_t basin_center = 0.52f * total_length;
        const meters_t basin_half_len = 460.0f * u::m;

        std::vector<SurfaceElevation> natural_y (spine.size ());
        for (std::size_t i = 0; i < spine.size (); ++i) {
          natural_y[i] = ground_at (spine[i].at);
          const meters_t s = spine[i].distance;
          SurfaceElevation ideal;
          if (s <= fjord_len) {
            const proportion_t u = band (0.0f * u::m, fjord_len, s);
            ideal = sea_level -
                    meters_t ((whole_part - u) * parameters.fjord_mouth_depth) +
                    meters_t (u * (0.35f * u::m));
          } else {
            const meters_t sr = s - fjord_len;
            const meters_t rise =
              meters_t (parameters.coastal_grade * sr) +
              meters_t (0.5f *
                        (parameters.highland_grade - parameters.coastal_grade) *
                        sr * (sr / river_len).numerical_value_in (one));
            ideal = sea_level + 0.35f * u::m + rise;
          }
          // Never raise natural ground above itself, and keep freshwater floor
          // slightly above sea level beyond the drowned fjord mouth.
          SurfaceElevation target = std::min (natural_y[i], ideal);
          if (s > fjord_len)
            target = std::max (
              target, std::min (natural_y[i], sea_level + 0.35f * u::m));
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
            spine[i - 1].floor + meters_t (0.0006f * grade[one] * seg);
          if (spine[i].floor < minimum_rise)
            spine[i].floor = std::min (natural_y[i], minimum_rise);
        }

        // Optional subtle over-deepened glacial ribbon lake (dalsjö) midway
        // up a long trough where ice stream flux carved deepest.
        if (add_ribbon_lake) {
          for (std::size_t i = 0; i < spine.size (); ++i) {
            const meters_t away = abs (spine[i].distance - basin_center);
            if (away >= basin_half_len)
              continue;
            const proportion_t u = away / basin_half_len;
            const proportion_t ring = whole_part - proportion_t (u * u);
            const proportion_t bell = proportion_t (ring * ring);
            if (natural_y[i] > spine[i].floor + 12.0f * u::m) {
              spine[i].floor = spine[i].floor - meters_t (2.4f * u::m * bell);
              spine[i].floor_half_width =
                spine[i].floor_half_width *
                (1.0f + 0.35f * bell.numerical_value_in (one));
            }
          }
        }

        // Stamp the U-shaped trough cross-section using full capsule distance
        // to each segment (so outer bends never leave uncarved pie-slice fins),
        // strictly lowering bedrock where the glacier cut through elevated
        // shelves and varying floor/wall width organically along the valley.
        constexpr meters_t wall_reach = 360.0f * u::m;
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
            1.20f * std::max (a.floor_half_width, b.floor_half_width) +
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
              // Full capsule distance: clamp t cleanly onto [0, 1] so curved
              // outer walls join seamlessly around every bend.
              const proportion_t t =
                std::clamp (proportion_t (along / run), zero_part, whole_part);
              const meters_t cx = moppe::lerp (ax_m, bx_m, t);
              const meters_t cz = moppe::lerp (az_m, bz_m, t);
              const meters_t side =
                hypot (plan_displacement (wx - cx, wz - cz));
              const meters_t dist_along =
                moppe::lerp (a.distance, b.distance, t);
              const scale_factor_t width_wave =
                (1.0f +
                 0.16f *
                   cr::sin (
                     (dist_along / (260.0f * u::m)).numerical_value_in (one)) +
                 0.09f *
                   cr::cos (
                     (dist_along / (590.0f * u::m)).numerical_value_in (one))) *
                scale_factor[one];
              const meters_t floor_hw = meters_t (
                moppe::lerp (a.floor_half_width, b.floor_half_width, t) *
                width_wave);
              if (side >= floor_hw + wall_reach)
                continue;
              const proportion_t strength =
                moppe::lerp (a.strength, b.strength, t);
              if (strength <= 1e-4f * proportion[one])
                continue;
              const SurfaceElevation floor_y =
                moppe::lerp (a.floor, b.floor, t);
              SurfaceElevation profile_y;
              bool on_floor = false;
              if (side <= floor_hw) {
                on_floor = true;
                const proportion_t u = side / floor_hw;
                // Subtle parabolic center dish keeps the post-glacial river
                // naturally centered on the wide flat alluvial floor.
                profile_y =
                  floor_y + meters_t (0.45f * u::m * proportion_t (u * u));
              } else {
                const meters_t delta = side - floor_hw;
                // Parabolic U-valley wall: gentle alluvial footslope rising
                // smoothly into a glaciated ~32-38 degree canyon wall.
                const proportion_t steepen =
                  band (0.0f * u::m, 95.0f * u::m, delta);
                const scale_factor_t slope_wave =
                  (1.0f + 0.14f * cr::cos ((dist_along / (310.0f * u::m))
                                             .numerical_value_in (one))) *
                  scale_factor[one];
                const slope_t local_slope =
                  slope_t (parameters.wall_slope * slope_wave) *
                  (0.34f + 0.72f * steepen.numerical_value_in (one));
                profile_y =
                  floor_y + 0.45f * u::m + meters_t (local_slope * delta);
              }
              const std::size_t cell =
                static_cast<std::size_t> (wrap_index (iz, height)) * width +
                wrap_index (ix, width);
              const SurfaceElevation natural = elevations[cell];
              if (profile_y >= natural)
                continue;
              const SurfaceElevation blended =
                moppe::lerp (natural, profile_y, strength);
              if (blended < formed[cell]) {
                formed[cell] = blended;
                carved_mask[cell] = 1;
                if (on_floor && !flood.ocean[cell]) {
                  const proportion_t edge_fade =
                    whole_part - band (0.70f * floor_hw, floor_hw, side);
                  const SedimentThickness fill =
                    quantity_cast<sediment_thickness> (meters_t (
                      parameters.valley_floor_outwash * edge_fade * strength));
                  outwash[cell] = std::max (outwash[cell], fill);
                }
              }
            }
        }

        carved_mouths.push_back (mouth_pos);
        ++result.troughs;
      }
    }

    // -------------------------------------------------------------------------
    // 2. Alpine glacial cirques (nisscher) bitten into the lee (NNE-ESE) flanks
    //    of high mountain massifs, sharpening sinusoidal domes into arêtes.
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
          if (carved_mask[cell] || formed[cell] < min_peak)
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
            if (!flood.ocean[cell] && target < formed[cell]) {
              formed[cell] = target;
              carved_mask[cell] = 1;
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
            if (carved_mask[cell] || flood.ocean[cell] ||
                formed[cell] < shelf_min)
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
