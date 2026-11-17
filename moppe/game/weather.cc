#include <moppe/game/weather.hh>

#include <algorithm>
#include <array>
#include <cmath>

namespace moppe::game {
  namespace {
    constexpr std::array<const char*, 7> names { "clear", "fair",    "overcast",
                                                 "mist",  "drizzle", "rain",
                                                 "storm" };

    std::uint64_t mix_bits (std::uint64_t x) {
      x += 0x9e3779b97f4a7c15ull;
      x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
      x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
      return x ^ (x >> 31);
    }

    // A number in [0, 1) for this seed, index, and question.
    double roll (std::uint64_t seed, std::uint64_t index, std::uint64_t what) {
      return double (mix_bits (seed ^ mix_bits (index * 8 + what)) >> 11) *
             0x1.0p-53;
    }

    using clock_hours_t = quantity<si::hour, double>;
    constexpr clock_hours_t weather_day = 24.0 * si::hour;

    // How long a spell of each kind lasts, at least and at most.
    struct Span {
      clock_hours_t least, most;
    };
    constexpr Span hours (double least, double most) {
      return { least * si::hour, most * si::hour };
    }
    Span span_of (SkyKind kind) {
      switch (kind) {
      case SkyKind::clear:
        return hours (4.0, 10.0);
      case SkyKind::fair:
        return hours (4.0, 9.0);
      case SkyKind::overcast:
        return hours (3.0, 7.0);
      case SkyKind::mist:
        return hours (3.0, 4.5);
      case SkyKind::drizzle:
        return hours (3.0, 5.0);
      case SkyKind::rain:
        return hours (3.0, 4.5);
      case SkyKind::storm:
        return hours (3.0, 3.5);
      }
      return hours (4.0, 8.0);
    }

    // How long the sky takes to turn into a spell of this kind.
    clock_hours_t change_hours (SkyKind kind) {
      switch (kind) {
      case SkyKind::storm:
        return 1.0 * si::hour;
      case SkyKind::mist:
        return 1.5 * si::hour;
      default:
        return 2.0 * si::hour;
      }
    }

    // What follows a spell, weighed by the hour the next begins at:
    // rain comes only out of an overcast, thunder mostly by night, and
    // mist mostly toward dawn after a clear night.
    SkyKind next_kind (SkyKind kind, clock_hours_t clock, double dice) {
      const bool night = clock >= 19.0 * si::hour || clock < 4.0 * si::hour;
      const bool dawn = clock >= 2.0 * si::hour && clock < 7.5 * si::hour;
      std::array<double, 7> weight {};
      auto set = [&] (SkyKind to, double w) { weight[std::size_t (to)] = w; };
      switch (kind) {
      case SkyKind::clear:
        set (SkyKind::clear, 3.0);
        set (SkyKind::fair, 5.0);
        set (SkyKind::mist, dawn ? 4.0 : 0.0);
        break;
      case SkyKind::fair:
        set (SkyKind::clear, 4.0);
        set (SkyKind::fair, 2.5);
        set (SkyKind::overcast, 3.0);
        set (SkyKind::mist, dawn ? 1.5 : 0.0);
        break;
      case SkyKind::overcast:
        set (SkyKind::fair, 4.0);
        set (SkyKind::overcast, 1.2);
        set (SkyKind::mist, dawn ? 1.5 : 0.3);
        set (SkyKind::drizzle, 1.6);
        set (SkyKind::rain, night ? 1.6 : 0.9);
        break;
      case SkyKind::mist:
        set (SkyKind::clear, 3.0);
        set (SkyKind::fair, 3.0);
        set (SkyKind::overcast, 1.0);
        set (SkyKind::drizzle, 0.5);
        break;
      case SkyKind::drizzle:
        set (SkyKind::overcast, 3.0);
        set (SkyKind::drizzle, 0.5);
        set (SkyKind::rain, 0.7);
        set (SkyKind::mist, 1.0);
        break;
      case SkyKind::rain:
        set (SkyKind::overcast, 3.0);
        set (SkyKind::drizzle, 1.0);
        set (SkyKind::storm, night ? 4.0 : 0.4);
        break;
      case SkyKind::storm:
        // A storm clears the air.
        set (SkyKind::rain, 1.0);
        set (SkyKind::overcast, 1.5);
        set (SkyKind::fair, 2.0);
        break;
      }
      double total = 0.0;
      for (double w : weight)
        total += w;
      double left = dice * total;
      for (std::size_t i = 0; i < weight.size (); ++i) {
        left -= weight[i];
        if (left < 0.0 && weight[i] > 0.0)
          return SkyKind (i);
      }
      return SkyKind::fair;
    }

    blend_weight_t smooth (float t) {
      t = std::clamp (t, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t) * blend_weight[one];
    }

    // The settings of a kind of sky, named by what each means.
    constexpr cloud_cover_t cover (float x) {
      return x * cloud_cover[one];
    }
    constexpr scale_factor_t times (float x) {
      return x * scale_factor[one];
    }
    constexpr blend_weight_t greyed (float x) {
      return x * blend_weight[one];
    }
    constexpr proportion_t share (float x) {
      return x * proportion[one];
    }
  }

  const char* sky_kind_name (SkyKind kind) {
    return names[std::size_t (kind)];
  }

  std::optional<SkyKind> sky_kind_named (std::string_view name) {
    for (std::size_t i = 0; i < names.size (); ++i)
      if (name == names[i])
        return SkyKind (i);
    return std::nullopt;
  }

  Weather weather_of (SkyKind kind) {
    switch (kind) {
    case SkyKind::clear:
      return { .cloud_floor = cover (0.0f), .cloud_ceiling = cover (0.3f) };
    case SkyKind::fair:
      return { .cloud_floor = cover (0.2f), .cloud_ceiling = cover (0.72f) };
    case SkyKind::overcast:
      return { .cloud_floor = cover (0.82f),
               .fog_density = times (1.5f),
               .fog_tint = { 0.72f, 0.75f, 0.78f },
               .fog_tint_amount = greyed (0.5f),
               .sunlight = times (0.45f),
               .skylight = times (1.6f) };
    case SkyKind::mist:
      return { .cloud_floor = cover (0.72f),
               .fog_density = times (2.6f),
               .fog_tint = { 0.74f, 0.78f, 0.79f },
               .fog_tint_amount = greyed (0.7f),
               .sunlight = times (0.5f),
               .skylight = times (1.7f),
               .mist = share (1.0f) };
    case SkyKind::drizzle:
      return { .cloud_floor = cover (0.94f),
               .fog_density = times (2.2f),
               .fog_tint = { 0.64f, 0.68f, 0.70f },
               .fog_tint_amount = greyed (0.85f),
               .sunlight = times (0.25f),
               .skylight = times (1.9f),
               .mist = share (0.6f),
               .rain = times (1.0f) };
    case SkyKind::rain:
      return { .cloud_floor = cover (0.97f),
               .fog_density = times (2.7f),
               .fog_tint = { 0.55f, 0.59f, 0.62f },
               .fog_tint_amount = greyed (0.9f),
               .sunlight = times (0.14f),
               .skylight = times (1.75f),
               .mist = share (0.5f),
               .rain = times (1.8f) };
    case SkyKind::storm:
      return { .cloud_floor = cover (1.0f),
               .fog_density = times (2.5f),
               .fog_tint = { 0.34f, 0.37f, 0.42f },
               .fog_tint_amount = greyed (0.95f),
               .sunlight = times (0.05f),
               .skylight = times (1.25f),
               .mist = share (0.3f),
               .rain = times (2.5f),
               .lightning = share (1.0f) };
    }
    return {};
  }

  Weather
  mix_weather (const Weather& a, const Weather& b, blend_weight_t weight) {
    const float t = weight.numerical_value_in (one);
    auto lerp = [t] (auto x, auto y) { return x + (y - x) * t; };
    return { .cloud_floor = lerp (a.cloud_floor, b.cloud_floor),
             .cloud_ceiling = lerp (a.cloud_ceiling, b.cloud_ceiling),
             .fog_density = lerp (a.fog_density, b.fog_density),
             // A tint only matters as far as it is applied, so blend it
             // by how much each side applies.
             .fog_tint = mix_display (
               a.fog_tint_amount > greyed (0.0f) ? a.fog_tint : b.fog_tint,
               b.fog_tint_amount > greyed (0.0f) ? b.fog_tint : a.fog_tint,
               t * blend_weight[one]),
             .fog_tint_amount = lerp (a.fog_tint_amount, b.fog_tint_amount),
             .sunlight = lerp (a.sunlight, b.sunlight),
             .skylight = lerp (a.skylight, b.skylight),
             .mist = lerp (a.mist, b.mist),
             .rain = lerp (a.rain, b.rain),
             .lightning = lerp (a.lightning, b.lightning) };
  }

  // The first spell is fair and lasts the first morning, which the
  // opening is authored under. The salt is chosen for the default world
  // (seed 123): a clear first night, rain the second evening, and mist
  // the dawn after.
  WeatherTimeline::WeatherTimeline (std::uint32_t seed)
      : m_seed (mix_bits (seed ^ 0x2u)),
        m_spells { { clock_time (0.0), SkyKind::fair },
                   { clock_time (12.5), SkyKind::fair } } {
    m_spells[1].kind =
      next_kind (SkyKind::fair, 12.5 * si::hour, roll (m_seed, 1, 0));
  }

  std::size_t WeatherTimeline::spell_at (clock_time_t hours) {
    hours = std::max (hours, clock_time (0.0));
    while (m_spells.back ().start <= hours) {
      const std::size_t i = m_spells.size () - 1;
      const Spell& last = m_spells.back ();
      const Span span = span_of (last.kind);
      const clock_time_t start = last.start + span.least +
                                 (span.most - span.least) * roll (m_seed, i, 1);
      m_spells.push_back (
        { start,
          next_kind (last.kind,
                     fmod (start - first_midnight, weather_day),
                     roll (m_seed, i + 1, 0)) });
    }
    const auto after = std::upper_bound (
      m_spells.begin (),
      m_spells.end (),
      hours,
      [] (clock_time_t h, const Spell& s) { return h < s.start; });
    return std::size_t (after - m_spells.begin ()) - 1;
  }

  SkyKind WeatherTimeline::kind_at (clock_time_t hours) {
    return m_spells[spell_at (hours)].kind;
  }

  Weather WeatherTimeline::at (clock_time_t hours) {
    const std::size_t i = spell_at (hours);
    const Spell& now = m_spells[i];
    const Weather to = weather_of (now.kind);
    if (i == 0)
      return to;
    const blend_weight_t t =
      smooth (float (((std::max (hours, clock_time (0.0)) - now.start) /
                      change_hours (now.kind))
                       .numerical_value_in (one)));
    return mix_weather (weather_of (m_spells[i - 1].kind), to, t);
  }

  proportion_t lightning_flash (proportion_t lightning_q, play_time_t now) {
    const float lightning = lightning_q.numerical_value_in (one);
    const double seconds = seconds_into_play (now);
    if (lightning <= 0.01f)
      return share (0.0f);
    // At most one strike in each second and a half, more of them the
    // fiercer the storm: some one every nine seconds at its height.
    constexpr double slot_seconds = 1.5;
    const double slot = std::floor (seconds / slot_seconds);
    const std::uint64_t index = std::uint64_t (std::int64_t (slot));
    constexpr std::uint64_t seed = 0x7b0a5e11ull;
    if (roll (seed, index, 0) > 0.16 * lightning)
      return share (0.0f);
    const double since =
      seconds - (slot * slot_seconds + 0.6 * roll (seed, index, 1));
    if (since < 0.0)
      return share (0.0f);
    // A stroke flickers: a first flash and a return or two after it.
    const double strength = 0.3 + 0.7 * roll (seed, index, 2);
    const std::array<double, 3> at { 0.0,
                                     0.06 + 0.06 * roll (seed, index, 3),
                                     0.2 + 0.15 * roll (seed, index, 4) };
    const std::array<double, 3> bright {
      1.0, 0.55, roll (seed, index, 5) > 0.45 ? 0.8 : 0.0
    };
    double flash = 0.0;
    for (std::size_t k = 0; k < at.size (); ++k)
      if (since >= at[k])
        flash += bright[k] * std::exp (-(since - at[k]) * 16.0);
    return share (float (std::min (1.0, strength * flash)));
  }
}
