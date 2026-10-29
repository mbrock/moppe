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
    constexpr std::size_t surf_grid_limit = 512;
    constexpr float surf_gravity = 9.81f;
    constexpr float surf_min_depth = 0.25f;
    constexpr float surf_far = 256.0f;
    constexpr float surf_unknown = std::numeric_limits<float>::infinity ();

    enum class SurfKind : std::uint8_t { dry, sea, freshwater };

    struct SurfCell {
      float ground;
      float level;
      float slowness; // seconds per metre
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
    float surf_arrival (float a, float b, float hx, float hz, float slowness) {
      if (a > b) {
        std::swap (a, b);
        std::swap (hx, hz);
      }
      const float one_axis = a + hx * slowness;
      if (one_axis <= b || !std::isfinite (b))
        return one_axis;
      const double dx = hx, dz = hz, s = slowness;
      const double delta = static_cast<double> (b) - a;
      const double squares = dx * dx + dz * dz;
      return static_cast<float> (
        a + (dx * dx * delta +
             dx * dz *
               std::sqrt (std::max (0.0, squares * s * s - delta * delta))) /
              squares);
    }

    struct SurfVisit {
      float arrival;
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
    void surf_march (ShoreSurf& field,
                     std::span<const SurfCell> cells,
                     std::size_t lane) {
      std::vector<std::uint8_t> settled (cells.size ());
      std::vector<SurfVisit> storage;
      storage.reserve (cells.size ());
      std::priority_queue<SurfVisit,
                          std::vector<SurfVisit>,
                          std::greater<SurfVisit>>
        pending (std::greater<SurfVisit> {}, std::move (storage));
      for (std::size_t i = 0; i < cells.size (); ++i)
        if (cells[i].coast)
          pending.push (
            { field.samples[i][lane], static_cast<std::uint32_t> (i) });
      const float hx = field.period_x / static_cast<float> (field.width);
      const float hz = field.period_z / static_cast<float> (field.height);
      while (!pending.empty ()) {
        const SurfVisit next = pending.top ();
        pending.pop ();
        const std::size_t i = next.cell;
        if (settled[i] || next.arrival != field.samples[i][lane])
          continue;
        settled[i] = 1;
        for (const std::size_t to : surf_neighbours (field, i)) {
          if (settled[to] || cells[to].coast || cells[to].kind != cells[i].kind)
            continue;
          const auto neighbours = surf_neighbours (field, to);
          const auto arrival = [&] (std::size_t from) {
            return settled[from] && cells[from].kind == cells[to].kind
                     ? field.samples[from][lane]
                     : surf_unknown;
          };
          const float t = surf_arrival (
            std::min (arrival (neighbours[0]), arrival (neighbours[1])),
            std::min (arrival (neighbours[2]), arrival (neighbours[3])),
            hx,
            hz,
            lane == 0 ? cells[to].slowness : 1.0f);
          if (t < field.samples[to][lane]) {
            field.samples[to][lane] = t;
            pending.push ({ t, static_cast<std::uint32_t> (to) });
          }
        }
      }
    }

    // Distance to a locally straight shoreline through the axis crossings.
    // A half-cell binary mask would move the coast as its slope changes.
    float surf_seed_distance (float dx, float dz) {
      if (!std::isfinite (dx))
        return dz;
      if (!std::isfinite (dz) || dx == 0.0f || dz == 0.0f)
        return std::min (dx, dz);
      return dx * dz / std::sqrt (dx * dx + dz * dz);
    }

    float surf_ramp (float low, float high, float value) {
      const float t = std::clamp ((value - low) / (high - low), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    // Signed one-sided differences, with a one-sided continuation at a
    // freshwater boundary. Never differentiate an unreached/sentinel value.
    std::array<float, 2>
    surf_slopes (float before, float here, float after, float spacing) {
      const bool has_before = std::isfinite (before);
      const bool has_after = std::isfinite (after);
      const float back = has_before ? (here - before) / spacing : 0.0f;
      const float ahead = has_after ? (after - here) / spacing : 0.0f;
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
      .period_x = domain.period_x ().numerical_value_in (u::m),
      .period_z = domain.period_z ().numerical_value_in (u::m),
      .samples = {},
    };
    if (!std::isfinite (field.period_x) || !std::isfinite (field.period_z) ||
        !(field.period_x > 0.0f) || !(field.period_z > 0.0f))
      throw std::invalid_argument ("invalid shore surf period");
    field.samples.assign (field.width * field.height,
                          { surf_unknown, surf_unknown, 0.0f, 0.0f });
    std::vector<SurfCell> cells;
    cells.reserve (field.samples.size ());
    const auto& levels = spatial::get<terrain::surface_elevation> (water);
    const auto& waves = spatial::get<terrain::wave_amplitude> (water);
    float sea_level = surf_unknown;
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
        const float bed = terrain::surface_elevation_value (ground[source]);
        const float level = terrain::surface_elevation_value (levels[source]);
        const float wave = waves[source].numerical_value_in (one);
        const float depth = level - bed;
        if (!std::isfinite (bed) || !std::isfinite (level) ||
            !std::isfinite (depth) || !std::isfinite (wave))
          throw std::invalid_argument ("non-finite shore surf input");
        const SurfKind kind =
          depth > 0.0f ? (wave >= 0.95f ? SurfKind::sea : SurfKind::freshwater)
                       : SurfKind::dry;
        if (kind == SurfKind::sea && !std::isfinite (sea_level))
          sea_level = level;
        cells.push_back (
          { bed,
            level,
            1.0f /
              std::sqrt (surf_gravity * (kind == SurfKind::sea
                                           ? std::max (surf_min_depth, depth)
                                           : surf_min_depth)),
            kind });
      }

    // Inspired by Ultra-Water's depth-based shoreMap.js travel field:
    // github.com/Humpalumps/Ultra-Water
    // (e58ddcdc18ea779b9386a2e5c76f2c6329c8bab9). This is an independent
    // first-order fast-marching solve. All coasts seed it: no incident
    // spectrum, fetch, diffraction, ray-tube gain, or small-island erasure. c =
    // sqrt(g * max(depth, 0.25 m)); the floor regularizes the unresolved
    // waterline singularity. No deep-speed cap.
    const float hx = field.period_x / static_cast<float> (field.width);
    const float hz = field.period_z / static_cast<float> (field.height);
    for (std::size_t i = 0; i < cells.size (); ++i) {
      const SurfCell& here = cells[i];
      if (here.kind == SurfKind::freshwater)
        continue;
      const bool sea = here.kind == SurfKind::sea;
      std::array<float, 2> crossing { surf_unknown, surf_unknown };
      const auto neighbours = surf_neighbours (field, i);
      for (std::size_t edge = 0; edge < neighbours.size (); ++edge) {
        const SurfCell& there = cells[neighbours[edge]];
        if (there.kind != (sea ? SurfKind::dry : SurfKind::sea))
          continue;
        const SurfCell& wet = sea ? here : there;
        const SurfCell& dry = sea ? there : here;
        const float rise = dry.ground - wet.level;
        if (rise < 0.0f)
          continue; // A dry depression is not an ocean-at-zero coastline.
        const float depth = wet.level - wet.ground;
        const float fraction = (sea ? depth : rise) / (depth + rise);
        const std::size_t axis = edge / 2;
        crossing[axis] =
          std::min (crossing[axis], fraction * (axis == 0 ? hx : hz));
      }
      const float distance = surf_seed_distance (crossing[0], crossing[1]);
      if (!std::isfinite (distance))
        continue;
      cells[i].coast = true;
      field.samples[i][0] = distance * here.slowness;
      field.samples[i][1] = distance;
    }
    surf_march (field, cells, 0);
    surf_march (field, cells, 1);

    // Sign BEFORE differentiating: clamping to zero on land flattens the
    // first sea gradient and creates a spurious line of incoherent fronts.
    for (std::size_t i = 0; i < cells.size (); ++i)
      if (cells[i].kind == SurfKind::dry && std::isfinite (field.samples[i][0]))
        field.samples[i][0] = -field.samples[i][0];

    for (std::size_t i = 0; i < cells.size (); ++i) {
      auto& sample = field.samples[i];
      if (!std::isfinite (sample[0]))
        continue;
      float mask = 1.0f - surf_ramp (120.0f, 240.0f, sample[1]);
      if (cells[i].kind == SurfKind::dry) {
        const float rise = cells[i].ground - sea_level;
        mask = rise < 0.0f ? 0.0f
                           : (1.0f - surf_ramp (4.0f, 24.0f, sample[1])) *
                               (1.0f - surf_ramp (0.2f, 1.25f, rise));
      }
      if (!(mask > 0.0f))
        continue;
      const auto n = surf_neighbours (field, i);
      const auto dx = surf_slopes (
        field.samples[n[0]][0], sample[0], field.samples[n[1]][0], hx);
      const auto dz = surf_slopes (
        field.samples[n[2]][0], sample[0], field.samples[n[3]][0], hz);
      const float gx = 0.5f * (dx[0] + dx[1]);
      const float gz = 0.5f * (dz[0] + dz[1]);
      const float ax = 0.5f * (std::abs (dx[0]) + std::abs (dx[1]));
      const float az = 0.5f * (std::abs (dz[0]) + std::abs (dz[1]));
      const float gradient = std::sqrt (gx * gx + gz * gz);
      const float total = std::sqrt (ax * ax + az * az);
      if (!(gradient > 0.0f) || !(total > 0.0f))
        continue;
      // Only cancellation loses coherence. A depth/speed change alone must
      // not be mistaken for a collision, especially at the first wet cell.
      mask *= surf_ramp (0.35f, 0.9f, gradient / total);
      sample[2] = -gx / gradient * mask;
      sample[3] = -gz / gradient * mask;
    }
    for (std::size_t i = 0; i < cells.size (); ++i) {
      auto& sample = field.samples[i];
      if (!std::isfinite (sample[0]))
        sample[0] = 0.0f;
      sample[1] = std::min (sample[1], surf_far) *
                  (cells[i].kind == SurfKind::sea ? 1.0f : -1.0f);
    }
    return field;
  }
}
