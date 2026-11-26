#include <moppe/map/surface.hh>

#include <moppe/terrain/benchmark_memory.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/local_evolution.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/world_recipe.hh>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
  int parse_positive_int (std::string_view text, const char* name) {
    std::size_t consumed = 0;
    const int value = std::stoi (std::string (text), &consumed);
    if (consumed != text.size () || value <= 0)
      throw std::invalid_argument (std::string (name) +
                                   " must be a positive integer");
    return value;
  }

  int parse_non_negative_int (std::string_view text, const char* name) {
    std::size_t consumed = 0;
    const int value = std::stoi (std::string (text), &consumed);
    if (consumed != text.size () || value < 0)
      throw std::invalid_argument (std::string (name) +
                                   " must be a non-negative integer");
    return value;
  }

  moppe::quantity<mp_units::si::kilo<mp_units::si::metre>, float>
  parse_positive_km (std::string_view text, const char* name) {
    std::size_t consumed = 0;
    const float value = std::stof (std::string (text), &consumed);
    if (consumed != text.size () || !std::isfinite (value) || value <= 0.0f)
      throw std::invalid_argument (std::string (name) +
                                   " must be a positive number");
    return value * moppe::u::km;
  }

  void print_geomorph_report (const moppe::map::SurfaceGeometry& surface,
                              moppe::meters_t sea_level_m) {
    using namespace moppe;
    using namespace moppe::terrain;
    const TerrainDomain& domain = surface.domain ();
    const std::size_t w = domain.width ();
    const std::size_t h = domain.height ();
    const std::size_t count = domain.size ();
    const auto& elevations = spatial::get<surface_elevation> (surface);
    const SurfaceElevation sea = surface_elevation_point (sea_level_m);
    const meters_t dx = domain.spacing_x ();
    const meters_t dz = domain.spacing_z ();

    std::vector<SurfaceElevation> dry_heights;
    dry_heights.reserve (count);
    quantity<u::deg, double> slope_deg_sum = 0.0 * u::deg;
    std::size_t dry_count = 0;
    for (std::size_t r = 0; r < h; ++r) {
      const std::size_t r_prev = (r + h - 1) % h;
      const std::size_t r_next = (r + 1) % h;
      for (std::size_t c = 0; c < w; ++c) {
        const std::size_t idx = r * w + c;
        const SurfaceElevation z = elevations[idx];
        if (z > sea) {
          dry_heights.push_back (z);
          ++dry_count;
        }
        const std::size_t c_prev = (c + w - 1) % w;
        const std::size_t c_next = (c + 1) % w;
        const slope_t gx = quantity_cast<terrain_slope> (
          (elevations[r * w + c_next] - elevations[r * w + c_prev]) /
          (2.0f * dx));
        const slope_t gz = quantity_cast<terrain_slope> (
          (elevations[r_next * w + c] - elevations[r_prev * w + c]) /
          (2.0f * dz));
        slope_deg_sum += std::atan (std::hypot (gx.numerical_value_in (one),
                                                gz.numerical_value_in (one))) *
                         (180.0 / std::numbers::pi) * u::deg;
      }
    }
    const auto quantile_elev = [&] (std::vector<SurfaceElevation>& vals,
                                    proportion_t q) {
      if (vals.empty ())
        return surface_elevation_point (0.0f * u::m);
      const std::size_t k = std::min (
        vals.size () - 1,
        static_cast<std::size_t> (q.numerical_value_in (one) *
                                  static_cast<float> (vals.size () - 1)));
      std::nth_element (vals.begin (), vals.begin () + k, vals.end ());
      return vals[k];
    };
    const SurfaceElevation p10 =
      quantile_elev (dry_heights, 0.10f * proportion[one]);
    const SurfaceElevation p50 =
      quantile_elev (dry_heights, 0.50f * proportion[one]);
    const SurfaceElevation p90 =
      quantile_elev (dry_heights, 0.90f * proportion[one]);
    const SurfaceElevation p99 =
      quantile_elev (dry_heights, 0.99f * proportion[one]);
    const SurfaceElevation hmax =
      dry_heights.empty ()
        ? surface_elevation_point (0.0f * u::m)
        : *std::max_element (dry_heights.begin (), dry_heights.end ());

    // Median relief inside 625 m x 625 m blocks (matching
    // orogeny-performance.md).
    const std::size_t block_x = std::max<std::size_t> (
      1,
      static_cast<std::size_t> (
        std::lround (((625.0f * u::m) / dx).numerical_value_in (one))));
    const std::size_t block_z = std::max<std::size_t> (
      1,
      static_cast<std::size_t> (
        std::lround (((625.0f * u::m) / dz).numerical_value_in (one))));
    std::vector<meters_t> block_relief;
    for (std::size_t r0 = 0; r0 + block_z <= h; r0 += block_z)
      for (std::size_t c0 = 0; c0 + block_x <= w; c0 += block_x) {
        SurfaceElevation lo = surface_elevation_point (1e9f * u::m);
        SurfaceElevation hi = surface_elevation_point (-1e9f * u::m);
        for (std::size_t r = r0; r < r0 + block_z; ++r)
          for (std::size_t c = c0; c < c0 + block_x; ++c) {
            const SurfaceElevation z = elevations[r * w + c];
            lo = std::min (lo, z);
            hi = std::max (hi, z);
          }
        block_relief.push_back (quantity_cast<isq::length> (hi - lo));
      }
    meters_t median_relief_625 = 0.0f * u::m;
    if (!block_relief.empty ()) {
      const std::size_t mid = (block_relief.size () - 1) / 2;
      std::nth_element (block_relief.begin (),
                        block_relief.begin () + mid,
                        block_relief.end ());
      median_relief_625 = block_relief[mid];
    }

    // Coastal basins & main-stem river length / gradient.
    const FloodField flood = analyze_standing_water (surface, sea);
    const LakeCensus census = census_lakes (flood);
    const DrainageGraph drainage = analyze_wet_drainage (flood, census);
    square_meters_t largest_outlet_area = 0.0f * u::m * u::m;
    std::size_t coastal_mouths_01 = 0;
    std::size_t largest_mouth_cell = 0;
    for (std::size_t i = 0; i < count; ++i) {
      if (flood.ocean[i])
        continue;
      const std::uint32_t next = drainage.receiver[i];
      if (next == i || !flood.ocean[next])
        continue;
      const square_meters_t area = drainage.contributing_area_at (i);
      if (area >= 100000.0f * u::m * u::m)
        ++coastal_mouths_01;
      if (area > largest_outlet_area) {
        largest_outlet_area = area;
        largest_mouth_cell = i;
      }
    }
    // Trace the main stem upstream from the largest coastal mouth by following
    // the largest-area donor at each step.
    std::vector<std::uint32_t> best_donor (count, UINT32_MAX);
    std::vector<square_meters_t> best_area (count, -1.0f * u::m * u::m);
    for (std::size_t i = 0; i < count; ++i) {
      const std::uint32_t next = drainage.receiver[i];
      if (next == i)
        continue;
      const square_meters_t a = drainage.contributing_area_at (i);
      if (a > best_area[next]) {
        best_area[next] = a;
        best_donor[next] = static_cast<std::uint32_t> (i);
      }
    }
    meters_f64_t main_len = 0.0 * u::m;
    std::size_t cur = largest_mouth_cell;
    const SurfaceElevation mouth_z = elevations[cur];
    SurfaceElevation lower_half_z = mouth_z;
    meters_f64_t lower_half_len = 0.0 * u::m;
    const square_meters_t min_channel_area = visible_river_minimum_area ();
    std::vector<std::pair<meters_f64_t, SurfaceElevation>> profile;
    profile.push_back ({ main_len, mouth_z });
    while (best_donor[cur] != UINT32_MAX) {
      const std::size_t prev = best_donor[cur];
      if (drainage.contributing_area_at (prev) < min_channel_area)
        break;
      const int cx = static_cast<int> (cur % w),
                cz = static_cast<int> (cur / w);
      const int px = static_cast<int> (prev % w),
                pz = static_cast<int> (prev / w);
      int ddx = px - cx;
      int ddz = pz - cz;
      if (ddx > static_cast<int> (w / 2))
        ddx -= static_cast<int> (w);
      if (ddx < -static_cast<int> (w / 2))
        ddx += static_cast<int> (w);
      if (ddz > static_cast<int> (h / 2))
        ddz -= static_cast<int> (h);
      if (ddz < -static_cast<int> (h / 2))
        ddz += static_cast<int> (h);
      main_len += std::hypot (static_cast<double> (ddx) * meters_value (dx),
                              static_cast<double> (ddz) * meters_value (dz)) *
                  u::m;
      cur = prev;
      profile.push_back ({ main_len, elevations[cur] });
    }
    const SurfaceElevation head_z = profile.back ().second;
    for (const auto& [dist, z] : profile)
      if (dist <= main_len * 0.5) {
        lower_half_len = dist;
        lower_half_z = z;
      }
    const grade_t mean_grade =
      main_len > 1.0 * u::m
        ? static_cast<float> (
            meters_value (quantity_cast<isq::length> (head_z - mouth_z)) /
            main_len.numerical_value_in (u::m)) *
            grade[one]
        : 0.0f * grade[one];
    const grade_t lower_grade =
      lower_half_len > 1.0 * u::m
        ? static_cast<float> (
            meters_value (quantity_cast<isq::length> (lower_half_z - mouth_z)) /
            lower_half_len.numerical_value_in (u::m)) *
            grade[one]
        : 0.0f * grade[one];

    std::cerr << std::fixed << std::setprecision (2) << "geomorph: extent="
              << meters_value (domain.period_x ()) * 0.001f << "km"
              << " dx=" << meters_value (dx) << "m"
              << " dry=" << (100.0 * dry_count / count) << "%"
              << " elev_p10/p50/p90/p99/max=" << surface_elevation_value (p10)
              << "/" << surface_elevation_value (p50) << "/"
              << surface_elevation_value (p90) << "/"
              << surface_elevation_value (p99) << "/"
              << surface_elevation_value (hmax) << "m"
              << " relief625=" << meters_value (median_relief_625) << "m"
              << " mean_slope="
              << (slope_deg_sum.numerical_value_in (u::deg) / count) << "deg"
              << " max_basin="
              << (largest_outlet_area.numerical_value_in (u::m * u::m) * 1e-6)
              << "km2"
              << " mouths=" << coastal_mouths_01
              << " main_stem=" << (main_len.numerical_value_in (u::m) * 0.001)
              << "km"
              << " grade_all/lower="
              << (mean_grade.numerical_value_in (one) * 100.0f) << "%/"
              << (lower_grade.numerical_value_in (one) * 100.0f) << "%\n";
  }

  std::uint64_t height_hash (const moppe::map::SurfaceGeometry& surface) {
    // FNV-1a over the exact float representation makes benchmark output a
    // cheap numerical-regression ledger as well as a timing record.
    std::uint64_t hash = 14695981039346656037ull;
    const std::size_t count = surface.domain ().size ();
    const auto& elevations =
      moppe::spatial::get<moppe::terrain::surface_elevation> (surface);
    for (std::size_t cell = 0; cell < count; ++cell) {
      std::uint32_t bits = std::bit_cast<std::uint32_t> (
        moppe::terrain::surface_elevation_value (elevations[cell]));
      for (int byte = 0; byte < 4; ++byte) {
        hash ^= bits & 0xffu;
        hash *= 1099511628211ull;
        bits >>= 8;
      }
    }
    return hash;
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    moppe::quantity<mp_units::si::kilo<mp_units::si::metre>, float> extent_km =
      10.0f * u::km;
    bool print_report = false;
    std::vector<std::string_view> positional;
    for (int i = 1; i < argc; ++i) {
      const std::string_view arg (argv[i]);
      if (arg == "--extent-km" && i + 1 < argc) {
        extent_km = parse_positive_km (argv[++i], "extent-km");
      } else if (arg == "--report") {
        print_report = true;
      } else {
        positional.push_back (arg);
      }
    }
    if (positional.size () < 3 || positional.size () > 6)
      throw std::invalid_argument (
        "usage: terrain-orogeny-benchmark [--extent-km KM] [--report] SIZE "
        "SEED STEPS [REPEATS [BACKEND [HEIGHTS.f32]]]");
    const int resolution = parse_positive_int (positional[0], "size");
    const int seed = parse_non_negative_int (positional[1], "seed");
    const int steps = parse_positive_int (positional[2], "steps");
    const int repeats = positional.size () >= 4
                          ? parse_positive_int (positional[3], "repeats")
                          : 1;
    if (resolution < 3)
      throw std::invalid_argument ("size must be at least three");

    const Seed terrain_seed { static_cast<std::uint32_t> (seed) };
    const meters_t extent_side =
      extent_km.numerical_value_in (u::km) * 1000.0f * u::m;
    const auto extent = spatial_extent_in_metres (
      Vec3 (meters_value (extent_side), 320, meters_value (extent_side)));
    auto evolution = make_world_recipe (extent,
                                        resolution,
                                        terrain_seed,
                                        50 * u::m,
                                        TerrainGenerationProfile::Play)
                       .evolution ();
    evolution.duration = static_cast<float> (steps) * evolution.time_step;
    const std::string backend =
      positional.size () >= 5 ? std::string (positional[4]) : "cpu";
    EvolutionBackend mode = EvolutionBackend::Cpu;
    if (backend == "metal")
      mode = EvolutionBackend::Metal;
    else if (backend == "metal-multigrid")
      mode = EvolutionBackend::MetalMultigrid;
    else if (backend != "cpu")
      throw std::invalid_argument ("unknown backend " + backend);

    std::cout
      << "resolution,cells,seed,steps,repeat,"
         "backend,elapsed_ms,peak_rss_mib,height_hash,uplift_m3,exported_m3,"
         "balance_error_m3,drainage_area_error_m2\n";
    for (int repeat = 0; repeat < repeats; ++repeat) {
      map::SurfaceGeometry surface (
        TerrainDomain (static_cast<std::size_t> (resolution),
                       static_cast<std::size_t> (resolution),
                       extent));
      const auto uplift =
        map::initialize_terrain (surface, terrain_seed, 50.0f * u::m);
      const auto start = std::chrono::steady_clock::now ();
      const auto report = evolve_local (surface, uplift, evolution, mode);
      const auto stop = std::chrono::steady_clock::now ();
      const quantity<si::milli<si::second>, double> elapsed =
        std::chrono::duration<double, std::milli> (stop - start).count () *
        si::milli<si::second>;

      if (positional.size () == 6) {
        std::ofstream file (std::string (positional[5]), std::ios::binary);
        file.exceptions (std::ios::badbit | std::ios::failbit);
        const auto& heights = spatial::get<surface_elevation> (surface);
        const auto bytes = std::as_bytes (std::span (heights));
        file.write (reinterpret_cast<const char*> (bytes.data ()),
                    bytes.size ());
      }
      if (print_report)
        print_geomorph_report (surface, 50.0f * u::m);
      std::cout
        << resolution << ','
        << static_cast<std::size_t> (resolution) *
             static_cast<std::size_t> (resolution)
        << ',' << seed << ',' << steps << ',' << repeat << ',' << backend << ','
        << std::fixed << std::setprecision (3)
        << elapsed.numerical_value_in (si::milli<si::second>) << ","
        << benchmark::peak_rss_mib () << ',' << std::hex
        << height_hash (surface) << std::dec << ','
        << report.tectonic_uplift.numerical_value_in (u::m * u::m * u::m) << ','
        << report.exported_sediment.numerical_value_in (u::m * u::m * u::m)
        << ',' << report.balance_error.numerical_value_in (u::m * u::m * u::m)
        << ',';
      if (report.drainage_area_error)
        std::cout << report.drainage_area_error->numerical_value_in (u::m *
                                                                     u::m);
      std::cout << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "terrain orogeny benchmark: " << error.what () << '\n';
    return -1;
  }
}
