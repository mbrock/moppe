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

    // How long a spell of each kind lasts, in hours, at least and at most.
    struct Span {
      double least, most;
    };
    Span span_of (SkyKind kind) {
      switch (kind) {
      case SkyKind::clear:
        return { 4.0, 10.0 };
      case SkyKind::fair:
        return { 4.0, 9.0 };
      case SkyKind::overcast:
        return { 3.0, 7.0 };
      case SkyKind::mist:
        return { 3.0, 4.5 };
      case SkyKind::drizzle:
        return { 3.0, 5.0 };
      case SkyKind::rain:
        return { 3.0, 4.5 };
      case SkyKind::storm:
        return { 3.0, 3.5 };
      }
      return { 4.0, 8.0 };
    }

    // How long the sky takes to turn into a spell of this kind.
    double change_hours (SkyKind kind) {
      switch (kind) {
      case SkyKind::storm:
        return 1.0;
      case SkyKind::mist:
        return 1.5;
      default:
        return 2.0;
      }
    }

    // What follows a spell, weighed by the hour the next begins at:
    // rain comes only out of an overcast, thunder mostly by night, and
    // mist mostly toward dawn after a clear night.
    SkyKind next_kind (SkyKind kind, double clock, double dice) {
      const bool night = clock >= 19.0 || clock < 4.0;
      const bool dawn = clock >= 2.0 && clock < 7.5;
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

    float smooth (float t) {
      t = std::clamp (t, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
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
      return { .cloud_floor = 0.0f, .cloud_ceiling = 0.3f };
    case SkyKind::fair:
      return { .cloud_floor = 0.2f, .cloud_ceiling = 0.72f };
    case SkyKind::overcast:
      return { .cloud_floor = 0.82f,
               .fog_density = 1.5f,
               .fog_tint = { 0.72f, 0.75f, 0.78f },
               .fog_tint_amount = 0.5f,
               .sunlight = 0.45f,
               .skylight = 1.6f };
    case SkyKind::mist:
      return { .cloud_floor = 0.72f,
               .fog_density = 2.6f,
               .fog_tint = { 0.74f, 0.78f, 0.79f },
               .fog_tint_amount = 0.7f,
               .sunlight = 0.5f,
               .skylight = 1.7f,
               .mist = 1.0f };
    case SkyKind::drizzle:
      return { .cloud_floor = 0.94f,
               .fog_density = 2.2f,
               .fog_tint = { 0.64f, 0.68f, 0.70f },
               .fog_tint_amount = 0.85f,
               .sunlight = 0.25f,
               .skylight = 1.9f,
               .mist = 0.6f,
               .rain = 1.0f };
    case SkyKind::rain:
      return { .cloud_floor = 0.97f,
               .fog_density = 2.7f,
               .fog_tint = { 0.55f, 0.59f, 0.62f },
               .fog_tint_amount = 0.9f,
               .sunlight = 0.14f,
               .skylight = 1.75f,
               .mist = 0.5f,
               .rain = 1.8f };
    case SkyKind::storm:
      return { .cloud_floor = 1.0f,
               .fog_density = 2.5f,
               .fog_tint = { 0.34f, 0.37f, 0.42f },
               .fog_tint_amount = 0.95f,
               .sunlight = 0.05f,
               .skylight = 1.25f,
               .mist = 0.3f,
               .rain = 2.5f,
               .lightning = 1.0f };
    }
    return {};
  }

  Weather mix_weather (const Weather& a, const Weather& b, float t) {
    auto lerp = [t] (float x, float y) { return x + (y - x) * t; };
    return { .cloud_floor = lerp (a.cloud_floor, b.cloud_floor),
             .cloud_ceiling = lerp (a.cloud_ceiling, b.cloud_ceiling),
             .fog_density = lerp (a.fog_density, b.fog_density),
             // A tint only matters as far as it is applied, so blend it
             // by how much each side applies.
             .fog_tint =
               mix_display (a.fog_tint_amount > 0.0f ? a.fog_tint : b.fog_tint,
                            b.fog_tint_amount > 0.0f ? b.fog_tint : a.fog_tint,
                            t),
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
        m_spells { { 0.0, SkyKind::fair }, { 12.5, SkyKind::fair } } {
    m_spells[1].kind = next_kind (SkyKind::fair, 12.5, roll (m_seed, 1, 0));
  }

  std::size_t WeatherTimeline::spell_at (double hours) {
    hours = std::max (hours, 0.0);
    while (m_spells.back ().start <= hours) {
      const std::size_t i = m_spells.size () - 1;
      const Spell& last = m_spells.back ();
      const Span span = span_of (last.kind);
      const double start = last.start + span.least +
                           (span.most - span.least) * roll (m_seed, i, 1);
      m_spells.push_back ({ start,
                            next_kind (last.kind,
                                       std::fmod (start, 24.0),
                                       roll (m_seed, i + 1, 0)) });
    }
    const auto after = std::upper_bound (
      m_spells.begin (), m_spells.end (), hours, [] (double h, const Spell& s) {
        return h < s.start;
      });
    return std::size_t (after - m_spells.begin ()) - 1;
  }

  SkyKind WeatherTimeline::kind_at (double hours) {
    return m_spells[spell_at (hours)].kind;
  }

  Weather WeatherTimeline::at (double hours) {
    const std::size_t i = spell_at (hours);
    const Spell& now = m_spells[i];
    const Weather to = weather_of (now.kind);
    if (i == 0)
      return to;
    const float t = smooth (
      float ((std::max (hours, 0.0) - now.start) / change_hours (now.kind)));
    return mix_weather (weather_of (m_spells[i - 1].kind), to, t);
  }

  float lightning_flash (float lightning, double seconds) {
    if (lightning <= 0.01f)
      return 0.0f;
    // At most one strike in each second and a half, more of them the
    // fiercer the storm: some one every nine seconds at its height.
    constexpr double slot_seconds = 1.5;
    const double slot = std::floor (seconds / slot_seconds);
    const std::uint64_t index = std::uint64_t (std::int64_t (slot));
    constexpr std::uint64_t seed = 0x7b0a5e11ull;
    if (roll (seed, index, 0) > 0.16 * lightning)
      return 0.0f;
    const double since =
      seconds - (slot * slot_seconds + 0.6 * roll (seed, index, 1));
    if (since < 0.0)
      return 0.0f;
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
    return float (std::min (1.0, strength * flash));
  }
}
