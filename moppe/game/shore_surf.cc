#include <moppe/game/shore_surf.hh>

#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace moppe::game {
  namespace {
    // How long a wave takes over a metre: the reciprocal of its speed.
    using slowness_t = quantity<si::second / si::metre, float>;

    constexpr std::size_t surf_grid_limit = 512;
    constexpr terrain::ElevationChange surf_min_depth =
      0.25f * terrain::surface_elevation[u::m];
    constexpr meters_t surf_far = 256.0f * u::m;
    // Not reached (yet): no arrival, no crossing, no sea.
    constexpr seconds_t surf_unknown_time =
      std::numeric_limits<float>::infinity () * u::s;
    constexpr meters_t surf_unknown_distance =
      std::numeric_limits<float>::infinity () * u::m;

    enum class SurfKind : std::uint8_t { dry, sea, freshwater };

    struct SurfCell {
      terrain::SurfaceElevation ground;
      terrain::SurfaceElevation level;
      slowness_t slowness;
      SurfKind kind;
      bool coast = false;
    };

    // Left, right, -z, +z, including both periodic seams.
    std::array<std::size_t, 4> surf_neighbours (const ShoreSurf& field,
                                                std::size_t i) {
      const std::size_t x = i % field.width;
      const std::size_t n = field.samples.size ();
      return { x ? i - 1 : i + field.width - 1,
               x + 1 < field.width ? i + 1 : i + 1 - field.width,
               i >= field.width ? i - field.width : i + n - field.width,
               i + field.width < n ? i + field.width : i + field.width - n };
    }

    // First-order Godunov solution of |grad T| = slowness. Unlike an
    // eight-way graph distance this does not make octagonal wave fronts.
    // Keep the quadratic in double and subtract the smaller arrival first:
    // far from shore the difference, not the squared absolute time, matters.
    // Arrivals are seconds (slowness in seconds a metre) or metres (unit
    // slowness).
    template <typename Arrival, typename Slowness>
    Arrival surf_arrival (
      Arrival a, Arrival b, meters_t hx, meters_t hz, Slowness slowness) {
      if (a > b) {
        std::swap (a, b);
        std::swap (hx, hz);
      }
      const Arrival one_axis = a + hx * slowness;
      if (one_axis <= b || !isfinite (b))
        return one_axis;
      const auto dx = value_cast<double> (hx), dz = value_cast<double> (hz);
      const auto s = value_cast<double> (slowness);
      const auto delta = value_cast<double> (b) - a;
      const auto squares = dx * dx + dz * dz;
      const auto excess = squares * s * s - delta * delta;
      return value_cast<float> (
        a + (dx * dx * delta +
             dx * dz * sqrt (std::max (decltype (excess)::zero (), excess))) /
              squares);
    }

    template <typename Arrival>
    struct SurfVisit {
      Arrival arrival;
      std::uint32_t cell;

      bool operator> (const SurfVisit& other) const {
        return arrival > other.arrival ||
               (arrival == other.arrival && cell > other.cell);
      }
    };

    // A bounded fast-marching solve, not a fixed count of sweeps: every
    // reachable cell settles, including around the torus or a headland.
    // The two solves share their coast but not their metric: seconds use
    // the local depth; metres use unit speed. Neither crosses freshwater
    // nor crosses land to shortcut the sea on the other side of an isthmus.
    // O(N log N) time and O(N) storage, N <= 512 * 512, not source size.
    template <typename Arrival, typename SlownessOf>
    void surf_march (const ShoreSurf& field,
                     std::span<const SurfCell> cells,
                     std::vector<Arrival>& arrivals,
                     SlownessOf slowness_of) {
      using Visit = SurfVisit<Arrival>;
      const Arrival unknown =
        std::numeric_limits<float>::infinity () * Arrival::reference;
      std::vector<std::uint8_t> settled (cells.size ());
      std::vector<Visit> storage;
      storage.reserve (cells.size ());
      std::priority_queue<Visit, std::vector<Visit>, std::greater<Visit>>
        pending (std::greater<Visit> {}, std::move (storage));
      for (std::size_t i = 0; i < cells.size (); ++i)
        if (cells[i].coast)
          pending.push ({ arrivals[i], static_cast<std::uint32_t> (i) });
      const meters_t hx = field.period_x / static_cast<float> (field.width);
      const meters_t hz = field.period_z / static_cast<float> (field.height);
      while (!pending.empty ()) {
        const Visit next = pending.top ();
        pending.pop ();
        const std::size_t i = next.cell;
        if (settled[i] || next.arrival != arrivals[i])
          continue;
        settled[i] = 1;
        for (const std::size_t to : surf_neighbours (field, i)) {
          if (settled[to] || cells[to].coast || cells[to].kind != cells[i].kind)
            continue;
          const auto neighbours = surf_neighbours (field, to);
          const auto arrival = [&] (std::size_t from) {
            return settled[from] && cells[from].kind == cells[to].kind
                     ? arrivals[from]
                     : unknown;
          };
          const Arrival t = surf_arrival (
            std::min (arrival (neighbours[0]), arrival (neighbours[1])),
            std::min (arrival (neighbours[2]), arrival (neighbours[3])),
            hx,
            hz,
            slowness_of (cells[to]));
          if (t < arrivals[to]) {
            arrivals[to] = t;
            pending.push ({ t, static_cast<std::uint32_t> (to) });
          }
        }
      }
    }

    // Distance to a locally straight shoreline through the axis crossings.
    // A half-cell binary mask would move the coast as its slope changes.
    meters_t surf_seed_distance (meters_t dx, meters_t dz) {
      if (!isfinite (dx))
        return dz;
      if (!isfinite (dz) || dx == 0.0f * u::m || dz == 0.0f * u::m)
        return std::min (dx, dz);
      return dx * dz / sqrt (dx * dx + dz * dz);
    }

    template <typename Q>
    proportion_t surf_ramp (Q low, Q high, Q value) {
      const proportion_t t =
        std::clamp (proportion_t ((value - low) / (high - low)),
                    0.0f * proportion[one],
                    1.0f * proportion[one]);
      return t * t * (3.0f - 2.0f * t);
    }

    // Signed one-sided differences, with a one-sided continuation at a
    // freshwater boundary. Never differentiate an unreached/sentinel value.
    std::array<slowness_t, 2> surf_slopes (seconds_t before,
                                           seconds_t here,
                                           seconds_t after,
                                           meters_t spacing) {
      const bool has_before = isfinite (before);
      const bool has_after = isfinite (after);
      const slowness_t back = has_before
                                ? slowness_t ((here - before) / spacing)
                                : slowness_t::zero ();
      const slowness_t ahead =
        has_after ? slowness_t ((after - here) / spacing) : slowness_t::zero ();
      return { has_before ? back : ahead, has_after ? ahead : back };
    }
  }

  ShoreSurf
  make_shore_surf (const terrain::WaterSheets& water,
                   std::span<const terrain::SurfaceElevation> ground) {
    MOPPE_PROFILE_ZONE ("shore_surf");
    const auto& domain = water.domain ();
    if (ground.size () != domain.size ())
      throw std::invalid_argument ("shore surf ground does not match water");
    ShoreSurf field {
      .width = std::min (domain.width (), surf_grid_limit),
      .height = std::min (domain.height (), surf_grid_limit),
      .period_x = domain.period_x (),
      .period_z = domain.period_z (),
      .samples = {},
    };
    if (!isfinite (field.period_x) || !isfinite (field.period_z) ||
        !(field.period_x > 0.0f * u::m) || !(field.period_z > 0.0f * u::m))
      throw std::invalid_argument ("invalid shore surf period");
    field.samples.assign (field.width * field.height,
                          { 0.0f, 0.0f, 0.0f, 0.0f });
    // Lane 0 and lane 1 of the texels, typed until they are written.
    std::vector<seconds_t> travel (field.samples.size (), surf_unknown_time);
    std::vector<meters_t> reach (field.samples.size (), surf_unknown_distance);
    std::vector<SurfCell> cells;
    cells.reserve (field.samples.size ());
    const auto& levels = spatial::get<terrain::surface_elevation> (water);
    const auto& waves = spatial::get<terrain::wave_amplitude> (water);
    terrain::SurfaceElevation sea_level =
      terrain::surface_elevation_point (surf_unknown_distance);
    for (std::size_t z = 0; z < field.height; ++z)
      for (std::size_t x = 0; x < field.width; ++x) {
        // Keep identity categorical: averaging amplitudes/depths across a
        // bank can turn a shallow lake or dry ground into spurious sea.
        // Sub-grid skerries remain unresolved, not deliberately flooded.
        const std::size_t sx =
          ((x * domain.width () + field.width / 2) / field.width) %
          domain.width ();
        const std::size_t sz =
          ((z * domain.height () + field.height / 2) / field.height) %
          domain.height ();
        const std::size_t source = sz * domain.width () + sx;
        const terrain::SurfaceElevation bed = ground[source];
        const terrain::SurfaceElevation level = levels[source];
        const terrain::WaveAmplitude wave = waves[source];
        const terrain::ElevationChange depth = level - bed;
        if (!isfinite (bed.delta_from_zero ()) ||
            !isfinite (level.delta_from_zero ()) || !isfinite (depth) ||
            !isfinite (wave))
          throw std::invalid_argument ("non-finite shore surf input");
        const SurfKind kind = depth > terrain::ElevationChange::zero ()
                                ? (wave >= 0.95f * terrain::wave_amplitude[one]
                                     ? SurfKind::sea
                                     : SurfKind::freshwater)
                                : SurfKind::dry;
        if (kind == SurfKind::sea && !isfinite (sea_level.delta_from_zero ()))
          sea_level = level;
        cells.push_back (
          { bed,
            level,
            slowness_t (1.0f / sqrt (standard_gravity *
                                     (kind == SurfKind::sea
                                        ? std::max (surf_min_depth, depth)
                                        : surf_min_depth))),
            kind });
      }

    // Inspired by Ultra-Water's depth-based shoreMap.js travel field:
    // github.com/Humpalumps/Ultra-Water
    // (e58ddcdc18ea779b9386a2e5c76f2c6329c8bab9). This is an independent
    // first-order fast-marching solve. All coasts seed it: no incident
    // spectrum, fetch, diffraction, ray-tube gain, or small-island erasure. c =
    // sqrt(g * max(depth, 0.25 m)); the floor regularizes the unresolved
    // waterline singularity. No deep-speed cap.
    const meters_t hx = field.period_x / static_cast<float> (field.width);
    const meters_t hz = field.period_z / static_cast<float> (field.height);
    for (std::size_t i = 0; i < cells.size (); ++i) {
      const SurfCell& here = cells[i];
      if (here.kind == SurfKind::freshwater)
        continue;
      const bool sea = here.kind == SurfKind::sea;
      std::array<meters_t, 2> crossing { surf_unknown_distance,
                                         surf_unknown_distance };
      const auto neighbours = surf_neighbours (field, i);
      for (std::size_t edge = 0; edge < neighbours.size (); ++edge) {
        const SurfCell& there = cells[neighbours[edge]];
        if (there.kind != (sea ? SurfKind::dry : SurfKind::sea))
          continue;
        const SurfCell& wet = sea ? here : there;
        const SurfCell& dry = sea ? there : here;
        const terrain::ElevationChange rise = dry.ground - wet.level;
        if (rise < terrain::ElevationChange::zero ())
          continue; // A dry depression is not an ocean-at-zero coastline.
        const terrain::ElevationChange depth = wet.level - wet.ground;
        // How far across the cell the shore lies.
        const proportion_t fraction =
          quantity_cast<proportion> ((sea ? depth : rise) / (depth + rise));
        const std::size_t axis = edge / 2;
        crossing[axis] = std::min (crossing[axis],
                                   meters_t (fraction * (axis == 0 ? hx : hz)));
      }
      const meters_t distance = surf_seed_distance (crossing[0], crossing[1]);
      if (!isfinite (distance))
        continue;
      cells[i].coast = true;
      travel[i] = distance * here.slowness;
      reach[i] = distance;
    }
    surf_march (field, cells, travel, [] (const SurfCell& cell) {
      return cell.slowness;
    });
    surf_march (
      field, cells, reach, [] (const SurfCell&) { return 1.0f * one; });

    // Sign BEFORE differentiating: clamping to zero on land flattens the
    // first sea gradient and creates a spurious line of incoherent fronts.
    for (std::size_t i = 0; i < cells.size (); ++i)
      if (cells[i].kind == SurfKind::dry && isfinite (travel[i]))
        travel[i] = -travel[i];

    for (std::size_t i = 0; i < cells.size (); ++i) {
      auto& sample = field.samples[i];
      if (!isfinite (travel[i]))
        continue;
      const proportion_t whole = 1.0f * proportion[one];
      proportion_t mask =
        whole - surf_ramp (120.0f * u::m, 240.0f * u::m, reach[i]);
      if (cells[i].kind == SurfKind::dry) {
        const terrain::ElevationChange rise = cells[i].ground - sea_level;
        mask =
          rise < terrain::ElevationChange::zero ()
            ? 0.0f * proportion[one]
            : (whole - surf_ramp (4.0f * u::m, 24.0f * u::m, reach[i])) *
                (whole - surf_ramp (0.2f * terrain::surface_elevation[u::m],
                                    1.25f * terrain::surface_elevation[u::m],
                                    rise));
      }
      if (!(mask > 0.0f * proportion[one]))
        continue;
      const auto n = surf_neighbours (field, i);
      const auto dx = surf_slopes (travel[n[0]], travel[i], travel[n[1]], hx);
      const auto dz = surf_slopes (travel[n[2]], travel[i], travel[n[3]], hz);
      const slowness_t gx = 0.5f * (dx[0] + dx[1]);
      const slowness_t gz = 0.5f * (dz[0] + dz[1]);
      const slowness_t ax = 0.5f * (abs (dx[0]) + abs (dx[1]));
      const slowness_t az = 0.5f * (abs (dz[0]) + abs (dz[1]));
      const slowness_t gradient = sqrt (gx * gx + gz * gz);
      const slowness_t total = sqrt (ax * ax + az * az);
      if (!(gradient > slowness_t::zero ()) || !(total > slowness_t::zero ()))
        continue;
      // Only cancellation loses coherence. A depth/speed change alone must
      // not be mistaken for a collision, especially at the first wet cell.
      mask = mask * surf_ramp (0.35f * proportion[one],
                               0.9f * proportion[one],
                               proportion_t (gradient / total));
      sample[2] = (-gx / gradient * mask).numerical_value_in (one);
      sample[3] = (-gz / gradient * mask).numerical_value_in (one);
    }
    for (std::size_t i = 0; i < cells.size (); ++i) {
      auto& sample = field.samples[i];
      sample[0] = isfinite (travel[i]) ? seconds_value (travel[i]) : 0.0f;
      sample[1] =
        meters_value (std::min (reach[i], surf_far) *
                      (cells[i].kind == SurfKind::sea ? 1.0f : -1.0f));
    }
    return field;
  }
}
