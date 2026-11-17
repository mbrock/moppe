#include <moppe/game/fishing.hh>

#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace fishing_parts {
    constexpr radians_t tau = 6.2831853f * u::rad;
    constexpr radians_t pi = 3.14159265f * u::rad;
    const direction_t world_up = unit_direction (Vec3 (0, 1, 0));
    const direction_t east = unit_direction (Vec3 (1, 0, 0));
    const direction_t north = unit_direction (Vec3 (0, 0, 1));

    // The rod, butt to tip, and how much of it is behind the hand.
    constexpr meters_t rod_length = 2.7f * u::m;
    constexpr meters_t rod_butt = 0.32f * u::m;
    // The fish are drawn larger than life, as the mushrooms are, so that
    // they show at the end of a line.
    constexpr scale_factor_t fish_drawn = 1.5f * scale_factor[one];

    // Of `a` and `b`, `t` of the way from one to the other: numbers,
    // lengths, places, or ways.
    template <typename V>
    V mix (V a, V b, float t) {
      return a + (b - a) * t;
    }

    template <typename V>
    V mix (V a, V b, proportion_t t) {
      return mix (a, b, t.numerical_value_in (one));
    }

    // What moves a place by an offset (docs/typed-quantities.md).
    auto shift (const displacement_t& d) {
      return quantity_cast<isq::position_vector> (d);
    }

    // Seen from the side: the same with no rise or fall.
    template <typename V>
    V flat (V v) {
      v.numerical_value_ref_in (V::unit)[1] = 0.0f;
      return v;
    }

    // How far a straight offset reaches.
    meters_t length_of (const displacement_t& d) {
      return length (displacement_value (d)) * u::m;
    }

    // The way `v` points, or `otherwise` when it is too short to say.
    template <Quantity Q>
    direction_t unit_or (const Q& v, const direction_t& otherwise) {
      const Vec3& raw = v.numerical_value_ref_in (Q::unit);
      return length2 (raw) > 1e-8f ? unit_direction (normalized (raw))
                                   : otherwise;
    }

    // The way `v` points; it must have some length.
    template <Quantity Q>
    direction_t unit_of (const Q& v) {
      return unit_direction (normalized (v.numerical_value_ref_in (Q::unit)));
    }

    // `v` with its part along `way` taken out.
    template <Quantity Q>
    Q square_off (const Q& v, const direction_t& way) {
      const Vec3& raw = v.numerical_value_ref_in (Q::unit);
      return Q (raw - direction_value (way) * dot (raw, direction_value (way)),
                Q::reference);
    }

    float within_unit (float x) {
      return std::clamp (x, 0.0f, 1.0f);
    }

    // The seed of a line's present chance at a fish.
    std::uint32_t chance (const Line& line) {
      return lattice_hash (line.casts, 0x6a09e667U, 0xf15U);
    }

    // A thin tube of `sides` faces through `points`.
    void strand (render::DrawList& dl,
                 const position_t* points,
                 int count,
                 meters_t radius,
                 int sides) {
      for (int i = 0; i + 1 < count; ++i) {
        const position_t a = points[i], b = points[i + 1];
        const direction_t axis = unit_or (b - a, world_up);
        // Square to the axis: across it from the up, or from the east
        // when it stands upright.
        const auto level = cross (axis, world_up);
        const direction_t u = length2 (level.numerical_value_in (one)) < 1e-6f
                                ? unit_of (cross (axis, east))
                                : unit_of (level);
        const direction_t v = quantity_cast<direction> (cross (axis, u));
        for (int s = 0; s < sides; ++s) {
          const radians_t t0 = tau * s / sides;
          const radians_t t1 = tau * (s + 1) / sides;
          const direction_t n0 = u * libm::cos (t0) + v * libm::sin (t0);
          const direction_t n1 = u * libm::cos (t1) + v * libm::sin (t1);
          const auto put = [&] (const position_t& p, const direction_t& n) {
            dl.normal (n);
            dl.vertex (p + shift (along (n, radius)));
          };
          put (a, n0);
          put (b, n0);
          put (b, n1);
          put (a, n0);
          put (b, n1);
          put (a, n1);
        }
      }
    }

    // A small ball of `radius` at `centre`, its upper half in `top` and
    // its lower in `bottom`: the float.
    void ball (render::DrawList& dl,
               const position_t& centre,
               meters_t radius,
               DisplayColor top,
               DisplayColor bottom) {
      constexpr int rings = 6;
      constexpr int sides = 8;
      const auto at = [&] (int ring, int side) {
        const radians_t polar = pi * ring / rings;
        const radians_t around = tau * side / sides;
        return unit_direction (Vec3 (libm::sin (polar) * libm::cos (around),
                                     libm::cos (polar),
                                     libm::sin (polar) * libm::sin (around)));
      };
      for (int r = 0; r < rings; ++r) {
        dl.color (r < rings / 2 ? top : bottom);
        for (int s = 0; s < sides; ++s) {
          const direction_t n00 = at (r, s), n01 = at (r, s + 1);
          const direction_t n10 = at (r + 1, s), n11 = at (r + 1, s + 1);
          const auto put = [&] (const direction_t& n) {
            dl.normal (n);
            dl.vertex (centre + shift (along (n, radius)));
          };
          put (n00);
          put (n10);
          put (n11);
          put (n00);
          put (n11);
          put (n01);
        }
      }
    }

    struct Look {
      DisplayColor back, belly, fin;
      // Height of the body for its length, and how it is marked.
      proportion_t depth;
      bool bars = false;
      bool spots = false;
      bool pale_spots = false;
      bool sail = false;
    };

    // What each kind is called and looks like.
    struct FishKindRow {
      const char* name;
      Look look;
    };

    // By FishKind.
    constexpr FishKindRow fish_kinds[] = {
      { "perch",
        { { 0.33f, 0.40f, 0.16f },
          { 0.86f, 0.84f, 0.66f },
          { 0.85f, 0.38f, 0.12f },
          0.30f * proportion[one],
          true } },
      { "pike",
        { { 0.22f, 0.30f, 0.13f },
          { 0.82f, 0.80f, 0.62f },
          { 0.40f, 0.36f, 0.18f },
          0.17f * proportion[one],
          false,
          false,
          true } },
      { "roach",
        { { 0.30f, 0.36f, 0.38f },
          { 0.86f, 0.87f, 0.86f },
          { 0.80f, 0.25f, 0.16f },
          0.30f * proportion[one] } },
      { "brown trout",
        { { 0.40f, 0.33f, 0.16f },
          { 0.88f, 0.78f, 0.50f },
          { 0.45f, 0.36f, 0.20f },
          0.23f * proportion[one],
          false,
          true } },
      { "grayling",
        { { 0.36f, 0.38f, 0.40f },
          { 0.80f, 0.80f, 0.82f },
          { 0.45f, 0.30f, 0.40f },
          0.22f * proportion[one],
          false,
          true,
          false,
          true } },
      { "arctic char",
        { { 0.24f, 0.30f, 0.30f },
          { 0.92f, 0.42f, 0.18f },
          { 0.90f, 0.40f, 0.20f },
          0.22f * proportion[one],
          false,
          false,
          true } },
      { "herring",
        { { 0.20f, 0.34f, 0.42f },
          { 0.88f, 0.90f, 0.92f },
          { 0.60f, 0.66f, 0.70f },
          0.20f * proportion[one] } },
      { "cod",
        { { 0.42f, 0.38f, 0.22f },
          { 0.88f, 0.86f, 0.76f },
          { 0.45f, 0.40f, 0.26f },
          0.22f * proportion[one],
          false,
          true } },
    };
    static_assert (std::size (fish_kinds) == fish_kind_count);

    const Look& look_of (FishKind kind) {
      return fish_kinds[static_cast<std::size_t> (kind)].look;
    }

    DisplayColor blend (DisplayColor a, DisplayColor b, float t) {
      return DisplayColor (mix (a.red, b.red, t),
                           mix (a.green, b.green, t),
                           mix (a.blue, b.blue, t));
    }

    // Fried, a fish goes golden brown, and then black.
    DisplayColor cook (DisplayColor raw, quantity<doneness[one], float> done) {
      const float cooked = done.numerical_value_in (one);
      const DisplayColor golden (0.66f, 0.44f, 0.20f);
      const DisplayColor burnt (0.09f, 0.07f, 0.06f);
      DisplayColor c = blend (raw, golden, 0.8f * within_unit (cooked));
      return blend (c, burnt, within_unit (cooked - 1.0f));
    }
  }

  const char* fish_name (FishKind kind) {
    return fishing_parts::fish_kinds[static_cast<std::size_t> (kind)].name;
  }

  Fish fish_for (Waters waters, bool upland, std::uint32_t seed) {
    struct Odds {
      FishKind kind;
      probability_t weight;
      meters_t least, most;
    };
    static constexpr Odds still[] = {
      { FishKind::perch, 0.48f * probability[one], 0.14f * u::m, 0.36f * u::m },
      { FishKind::roach, 0.32f * probability[one], 0.12f * u::m, 0.27f * u::m },
      { FishKind::pike, 0.20f * probability[one], 0.40f * u::m, 0.95f * u::m },
    };
    static constexpr Odds cold[] = {
      { FishKind::arctic_char,
        0.55f * probability[one],
        0.18f * u::m,
        0.42f * u::m },
      { FishKind::brown_trout,
        0.25f * probability[one],
        0.20f * u::m,
        0.45f * u::m },
      { FishKind::perch, 0.20f * probability[one], 0.14f * u::m, 0.30f * u::m },
    };
    static constexpr Odds running[] = {
      { FishKind::brown_trout,
        0.50f * probability[one],
        0.18f * u::m,
        0.48f * u::m },
      { FishKind::grayling,
        0.38f * probability[one],
        0.20f * u::m,
        0.42f * u::m },
      { FishKind::perch, 0.12f * probability[one], 0.14f * u::m, 0.30f * u::m },
    };
    static constexpr Odds sea[] = {
      { FishKind::herring,
        0.45f * probability[one],
        0.15f * u::m,
        0.26f * u::m },
      { FishKind::perch, 0.30f * probability[one], 0.15f * u::m, 0.34f * u::m },
      { FishKind::cod, 0.25f * probability[one], 0.30f * u::m, 0.75f * u::m },
    };
    const Odds* odds = waters == Waters::running ? running
                       : waters == Waters::sea   ? sea
                       : upland                  ? cold
                                                 : still;
    probability_t pick = hash_lane (seed, 1) * probability[one];
    int i = 0;
    while (i < 2 && pick >= odds[i].weight) {
      pick -= odds[i].weight;
      ++i;
    }
    // Most are small; now and then a big one.
    const proportion_t size =
      std::pow (hash_lane (seed, 2), 1.8f) * proportion[one];
    return { .kind = odds[i].kind,
             .length = odds[i].least + (odds[i].most - odds[i].least) *
                                         size.numerical_value_in (one),
             .seed = seed };
  }

  seconds_t bite_wait (std::uint32_t seed) {
    return (4.0f + 12.0f * hash_lane (seed, 3)) * u::s;
  }

  seconds_t play_seconds (const Fish& fish) {
    // A bigger fish fights longer: four seconds more a metre.
    return 2.2f * u::s + 4.0f * u::s / u::m * fish.length;
  }

  void cast_line (Line& line,
                  const position_t& stand,
                  const position_t& aim,
                  Waters waters,
                  bool upland,
                  play_time_t now) {
    using namespace fishing_parts;
    ++line.casts;
    line.phase = Line::Phase::casting;
    line.since = now;
    line.stand = stand;
    line.aim = aim;
    line.bobber = aim;
    line.waters = waters;
    line.upland = upland;
    line.fish = fish_for (waters, upland, chance (line));
    line.bite_at = now + duration_t (cast_seconds + bite_wait (chance (line)));
    line.next_splash = now + duration_t (cast_seconds);
  }

  bool tend_line (Fishing& fishing, play_time_t now, bool strike) {
    using namespace fishing_parts;
    Line& line = fishing.line;
    Creel& creel = fishing.creel;
    const seconds_t age = seconds_between (now, line.since);
    const auto begin = [&] (Line::Phase phase) {
      line.phase = phase;
      line.since = now;
    };
    switch (line.phase) {
    case Line::Phase::idle:
      return false;
    case Line::Phase::casting:
      if (age >= cast_seconds)
        begin (Line::Phase::waiting);
      return strike;
    case Line::Phase::waiting:
      if (strike) {
        begin (Line::Phase::empty);
        return true;
      }
      if (now >= line.bite_at)
        begin (Line::Phase::biting);
      return false;
    case Line::Phase::biting:
      if (strike) {
        begin (Line::Phase::playing);
        return true;
      }
      if (age > strike_seconds) {
        // It got away with the bait; another may come.
        creel.last = line.fish;
        creel.last_lost = true;
        creel.last_time = now;
        ++line.casts;
        line.fish = fish_for (line.waters, line.upland, chance (line));
        line.bite_at = now + duration_t (bite_wait (chance (line)));
        begin (Line::Phase::waiting);
      }
      return false;
    case Line::Phase::playing:
      if (age > play_seconds (line.fish)) {
        line.bobber = fish_in_water (line, now);
        creel.fish.push_back (line.fish);
        creel.last = line.fish;
        creel.last_lost = false;
        creel.last_time = now;
        begin (Line::Phase::landed);
      }
      return strike;
    case Line::Phase::landed:
      if (age > landed_seconds)
        begin (Line::Phase::idle);
      return strike;
    case Line::Phase::empty:
      if (age > wind_seconds)
        begin (Line::Phase::idle);
      return strike;
    }
    return false;
  }

  position_t fish_in_water (const Line& line, play_time_t now) {
    using namespace fishing_parts;
    if (line.phase != Line::Phase::playing)
      return line.bobber;
    const seconds_t t = seconds_between (now, line.since);
    const proportion_t done =
      within_unit ((t / play_seconds (line.fish)).numerical_value_in (one)) *
      proportion[one];
    const float d = done.numerical_value_in (one);
    // It runs from side to side as it is wound in to a few metres out.
    const displacement_t in = flat (line.stand - line.bobber);
    const direction_t side = unit_or (cross (in, world_up), east);
    return line.bobber + shift (in * (0.6f * smoothstep (0.0f, 1.0f, d))) +
           shift (along (side,
                         (1.0f - d) * 1.1f *
                           std::sin (2.3f * seconds_value (t)) * u::m)) -
           shift (along (world_up, 0.05f * u::m));
  }

  std::optional<RodPose>
  rod_pose (const Line& line, play_time_t now, seconds_t clock) {
    using namespace fishing_parts;
    // The bob and the twist run on the plain seconds.
    const float time = seconds_value (clock);
    const float age = seconds_value (seconds_between (now, line.since));
    RodPose rod;
    rod.water = line.bobber;
    switch (line.phase) {
    case Line::Phase::idle:
      return std::nullopt;
    case Line::Phase::casting: {
      // Back over the shoulder, a whip forward, and down to the water as
      // the float flies out.
      rod.raise =
        (age < 0.4f    ? mix (0.6f, 1.95f, smoothstep (0.0f, 0.4f, age))
         : age < 0.55f ? mix (1.95f, 0.25f, (age - 0.4f) / 0.15f)
                       : mix (0.25f, 0.55f, smoothstep (0.55f, 0.9f, age))) *
        inclination[u::rad];
      const float flown = fishing_parts::within_unit (
        (age - 0.45f) / (seconds_value (cast_seconds) - 0.45f));
      rod.water = line.aim;
      rod.out = flown * proportion[one];
      rod.arc = 2.6f * flown * (1.0f - flown) * u::m;
      rod.tight = 0.6f * (1.0f - flown) * proportion[one];
      break;
    }
    case Line::Phase::waiting: {
      rod.raise =
        (0.55f + 0.03f * std::sin (0.7f * time)) * inclination[u::rad];
      // Before the take, a few nibbles.
      const seconds_t coming = seconds_between (line.bite_at, now);
      const float nibble = coming < 2.2f * u::s && coming > 0.0f * u::s
                             ? 0.035f *
                                 std::max (0.0f, std::sin (11.0f * time)) *
                                 std::max (0.0f, std::sin (2.9f * time))
                             : 0.0f;
      position_value (rod.water)[1] += 0.012f * std::sin (2.1f * time) - nibble;
      break;
    }
    case Line::Phase::biting:
      // Pulled under, jerking.
      rod.raise = 0.48f * inclination[u::rad];
      position_value (rod.water)[1] -= 0.12f + 0.04f * std::sin (17.0f * time);
      rod.bend = (0.25f + 0.1f * std::sin (13.0f * time)) * proportion[one];
      rod.tight = 0.8f * proportion[one];
      break;
    case Line::Phase::playing:
      rod.raise =
        (0.95f + 0.12f * std::sin (4.7f * time)) * inclination[u::rad];
      rod.bend = (0.6f + 0.18f * std::sin (7.3f * time)) * proportion[one];
      rod.tight = 1.0f * proportion[one];
      rod.water = fish_in_water (line, now);
      rod.fish_on = true;
      rod.fish = line.fish;
      rod.wriggle = std::sin (9.0f * time) * wriggle[one];
      break;
    case Line::Phase::landed: {
      // Lifted out, swung in, and the rod lowered to bring it to eye
      // level.
      const float lifted = smoothstep (0.0f, 0.6f, age);
      const float lowered = smoothstep (0.7f, 1.4f, age);
      rod.raise =
        mix (mix (0.95f, 1.2f, lifted), 0.4f, lowered) * inclination[u::rad];
      rod.bend = mix (0.5f, 0.15f, lifted) * proportion[one];
      rod.out = (1.0f - lifted) * proportion[one];
      rod.arc = 0.8f * libm::sin (pi * lifted) * u::m;
      rod.tight = 1.0f * proportion[one];
      rod.fish_on = true;
      rod.fish = line.fish;
      rod.wriggle =
        std::sin (11.0f * time) * mix (1.0f, 0.4f, lifted) * wriggle[one];
      break;
    }
    case Line::Phase::empty: {
      const float wound = smoothstep (0.0f, seconds_value (wind_seconds), age);
      rod.raise = mix (0.55f, 1.1f, wound) * inclination[u::rad];
      rod.out = (1.0f - wound) * proportion[one];
      rod.arc = 0.6f * libm::sin (pi * wound) * u::m;
      rod.tight = 0.7f * proportion[one];
      break;
    }
    }
    return rod;
  }

  position_t rod_hand (const AvatarSkeleton& k, radians_t raise) {
    using namespace fishing_parts;
    const direction_t chest_right =
      unit_or (cross (k.chest_up, k.chest_forward), k.right);
    // Out before the belly; swung back, up by the shoulder.
    const proportion_t back =
      smoothstep (1.2f, 1.9f, radians_value (raise)) * proportion[one];
    return k.shoulder[1] +
           shift (
             along (k.chest_forward, mix (0.36f * u::m, 0.12f * u::m, back))) +
           shift (along (k.chest_up, mix (-0.24f * u::m, 0.12f * u::m, back))) +
           shift (along (chest_right, 0.04f * u::m));
  }

  void draw_rod (render::DrawList& dl,
                 const AvatarSkeleton& k,
                 const RodPose& rod,
                 seconds_t time) {
    using namespace fishing_parts;
    const position_t hand = k.wrist[1];
    // The rod points out over the water the line runs to, which need not
    // be the way the angler faces: one at a boat's helm casts over the
    // side.
    const direction_t ahead =
      unit_or (flat (rod.water - hand), unit_or (flat (k.forward), north));
    const direction_t right = unit_or (cross (world_up, ahead), east);
    const direction_t rod_way =
      unit_of (ahead * libm::cos (rod.raise) +
               world_up * libm::sin (rod.raise) + right * 0.10f);
    const position_t butt = hand - shift (along (rod_way, rod_butt));
    const position_t straight_tip =
      hand + shift (along (rod_way, rod_length - rod_butt));
    const position_t hang =
      straight_tip - shift (along (world_up, 0.45f * u::m));
    const position_t water_end = rod.water + shift (along (world_up, rod.arc));
    const position_t end = mix (hang, water_end, rod.out);

    // The rod bends toward what pulls it, and droops a little anyway.
    const displacement_t pull =
      square_off<displacement_t> (end - straight_tip, rod_way);
    const direction_t toward = unit_or (pull, -world_up);
    const proportion_t bend = 0.04f * proportion[one] + 0.30f * rod.bend;
    const meters_t bent = quantity_cast<isq::length> (bend * rod_length);
    constexpr int pieces = 8;
    position_t points[pieces + 1];
    for (int i = 0; i <= pieces; ++i) {
      const float s = static_cast<float> (i) / pieces;
      points[i] = butt + shift (along (rod_way, rod_length * s)) +
                  shift (along (toward, bent * s * s));
    }
    const position_t tip = points[pieces];

    render::DrawState solid;
    solid.cull = false;
    dl.state (solid);
    dl.lit (true);
    dl.fogged (true);
    dl.begin (render::Prim::Triangles);
    // A cork grip, then the blank tapering to the tip.
    dl.color (0.62f, 0.48f, 0.30f);
    {
      const position_t grip[2] = {
        butt, hand + shift (along (rod_way, 0.08f * u::m))
      };
      strand (dl, grip, 2, 0.016f * u::m, 6);
    }
    dl.color (0.20f, 0.13f, 0.08f);
    for (int i = 1; i < pieces; ++i) {
      const float s = static_cast<float> (i) / pieces;
      strand (dl, points + i, 2, mix (0.010f * u::m, 0.004f * u::m, s), 5);
    }
    // The reel under the grip.
    dl.color (0.55f, 0.56f, 0.58f);
    {
      const position_t under = hand + shift (along (rod_way, 0.05f * u::m)) -
                               shift (along (world_up, 0.05f * u::m));
      const position_t spool[2] = {
        under - shift (along (right, 0.025f * u::m)),
        under + shift (along (right, 0.025f * u::m))
      };
      strand (dl, spool, 2, 0.032f * u::m, 8);
    }

    // The line, sagging as much as it is slack.
    constexpr int stretch = 14;
    position_t line[stretch + 1];
    const meters_t span = length_of (end - tip);
    const meters_t sag =
      (0.08f * (1.0f - rod.tight.numerical_value_in (one)) + 0.005f) * span;
    for (int i = 0; i <= stretch; ++i) {
      const float s = static_cast<float> (i) / stretch;
      line[i] = mix (tip, end, s) -
                shift (along (world_up, sag * 4.0f * s * (1.0f - s)));
    }
    dl.color (0.86f, 0.88f, 0.84f);
    strand (dl, line, stretch + 1, 0.006f * u::m, 4);

    if (rod.fish_on) {
      // Out of the water it hangs from its mouth; in it, it heads away.
      const direction_t away = unit_or (flat (end - k.pelvis), ahead);
      const direction_t along_fish =
        unit_or (mix<direction_t> (-world_up, away, rod.out), away);
      const direction_t side = unit_or (cross (along_fish, world_up), right);
      const direction_t back =
        unit_or (mix<direction_t> (side * -1.0f, world_up, rod.out), world_up);
      draw_fish (dl,
                 rod.fish,
                 end,
                 along_fish,
                 back,
                 1.0f * scale_factor[one],
                 rod.wriggle);
    } else {
      // The float: red over white, with a little stem.
      const position_t bob = end + shift (along (world_up, 0.04f * u::m));
      ball (dl,
            bob,
            0.065f * u::m,
            DisplayColor (0.90f, 0.12f, 0.08f),
            DisplayColor (0.92f, 0.92f, 0.88f));
      dl.color (0.90f, 0.12f, 0.08f);
      const position_t stem[2] = {
        bob, bob + shift (along (world_up, 0.11f * u::m))
      };
      strand (dl, stem, 2, 0.008f * u::m, 4);
    }
    (void)time;
    dl.end ();
    dl.state (render::DrawState ());
  }

  void draw_fish (render::DrawList& dl,
                  const Fish& fish,
                  const position_t& mouth,
                  const direction_t& along_way,
                  const direction_t& back_way,
                  scale_factor_t scale_q,
                  wriggle_t wriggle_q,
                  quantity<doneness[one], float> cooked) {
    using namespace fishing_parts;
    const Look look = look_of (fish.kind);
    const float drawn = fish_drawn.numerical_value_in (one);
    const float scale = scale_q.numerical_value_in (one);
    const float wriggle = wriggle_q.numerical_value_in (one);
    const direction_t ahead = unit_or (along_way, north);
    const direction_t back = unit_or (square_off (back_way, ahead), world_up);
    const direction_t side = unit_of (cross (ahead, back));
    const meters_t body = fish.length * drawn * scale * 0.84f;
    const meters_t depth =
      fish.length * drawn * scale * look.depth.numerical_value_in (one);
    const meters_t width = depth * 0.42f;
    // The section's slope, as the normals take it.
    const float depth_m = meters_value (depth);
    const float width_m = meters_value (width);

    // The spine, wriggling more toward the tail.
    const auto spine = [&] (float u) {
      return mouth + shift (along (ahead, body * u)) +
             shift (along (side, 0.10f * body * wriggle * std::pow (u, 1.6f)));
    };
    // Its section's half height and half width, blunt at the nose and
    // narrowing to the tail's root.
    const auto girth = [&] (float u) {
      const float swell =
        std::sqrt (std::max (u, 0.0f)) * std::pow (1.0f - u, 0.6f) / 0.469f;
      return 0.14f + 0.86f * swell * (1.0f - 0.1f * u);
    };
    constexpr int rings = 10;
    constexpr int sides = 10;
    const auto colour_at = [&] (float u, radians_t a, const direction_t& n) {
      const float upness = dot (n, back).numerical_value_in (one);
      DisplayColor c =
        blend (look.belly, look.back, smoothstep (-0.35f, 0.45f, upness));
      if (look.bars && upness > -0.2f && libm::sin (u * pi * 11.0f) > 0.45f &&
          u > 0.15f && u < 0.9f)
        c = blend (c, DisplayColor (0.08f, 0.10f, 0.05f), 0.6f);
      if ((look.spots || look.pale_spots) && u > 0.12f && upness > -0.3f) {
        const auto cell =
          static_cast<std::uint32_t> (u * 23.0f) * 31U +
          static_cast<std::uint32_t> (radians_value ((a + pi) * 3.0f));
        if (hash_lane (fish.seed + cell, 7) < 0.28f)
          c = blend (c,
                     look.pale_spots ? DisplayColor (0.85f, 0.82f, 0.62f)
                                     : DisplayColor (0.10f, 0.07f, 0.05f),
                     0.55f);
      }
      return cook (c, cooked);
    };
    const auto ring_point = [&] (float u, radians_t a, direction_t& normal) {
      const float g = girth (u);
      const float c = libm::cos (a), s = libm::sin (a);
      normal = unit_or (side * (c / width_m) + back * (s / depth_m) -
                          ahead * (0.6f * (0.5f - u)),
                        back);
      return spine (u) + shift (along (side, c * width * g)) +
             shift (along (back, s * depth * g));
    };
    for (int r = 0; r < rings; ++r) {
      const float u0 = static_cast<float> (r) / rings;
      const float u1 = static_cast<float> (r + 1) / rings;
      for (int s = 0; s < sides; ++s) {
        const radians_t a0 = tau * s / sides;
        const radians_t a1 = tau * (s + 1) / sides;
        direction_t n00, n01, n10, n11;
        const position_t p00 = ring_point (u0, a0, n00);
        const position_t p01 = ring_point (u0, a1, n01);
        const position_t p10 = ring_point (u1, a0, n10);
        const position_t p11 = ring_point (u1, a1, n11);
        const auto put = [&] (const position_t& p,
                              const direction_t& n,
                              float u,
                              radians_t a) {
          dl.color (colour_at (u, a, n));
          dl.normal (n);
          dl.vertex (p);
        };
        put (p00, n00, u0, a0);
        put (p10, n10, u1, a0);
        put (p11, n11, u1, a1);
        put (p00, n00, u0, a0);
        put (p11, n11, u1, a1);
        put (p01, n01, u0, a1);
      }
    }
    // The tail, forked, and the fins on the back.
    dl.color (cook (look.fin, cooked));
    const auto fin =
      [&] (const position_t& a, const position_t& b, const position_t& c) {
        dl.normal (side);
        dl.vertex (a);
        dl.vertex (b);
        dl.vertex (c);
      };
    const position_t root = spine (1.0f);
    const direction_t tail_along = unit_or (spine (1.0f) - spine (0.9f), ahead);
    const meters_t tail = 0.30f * body;
    const position_t fork = root + shift (along (tail_along, tail * 0.7f));
    fin (root,
         root + shift (along (tail_along, tail)) +
           shift (along (back, 0.55f * depth)),
         fork);
    fin (root,
         fork,
         root + shift (along (tail_along, tail)) -
           shift (along (back, 0.55f * depth)));
    const float sail = look.sail ? 1.0f : 0.45f;
    fin (spine (0.35f) + shift (along (back, 0.45f * depth)),
         spine (0.40f) + shift (along (back, depth * (0.5f + 0.5f * sail))),
         spine (look.sail ? 0.70f : 0.62f) +
           shift (along (back, 0.40f * depth)));
    // Pectorals by the gills.
    const auto aside = [&] (const direction_t& way, meters_t by, float more) {
      return shift (along (way, by) * more);
    };
    fin (spine (0.22f) + aside (side, width, 0.8f) -
           shift (along (back * 0.1f, depth)),
         spine (0.36f) + aside (side, width, 1.9f) -
           shift (along (back * 0.35f, depth)),
         spine (0.30f) + aside (side, width, 0.8f) -
           shift (along (back * 0.3f, depth)));
    fin (spine (0.22f) - aside (side, width, 0.8f) -
           shift (along (back * 0.1f, depth)),
         spine (0.36f) - aside (side, width, 1.9f) -
           shift (along (back * 0.35f, depth)),
         spine (0.30f) - aside (side, width, 0.8f) -
           shift (along (back * 0.3f, depth)));
    // An eye each side.
    dl.color (0.04f, 0.04f, 0.04f);
    for (const float hand : { 1.0f, -1.0f }) {
      const position_t eye = spine (0.09f) +
                             shift (along (side, hand * width * 0.75f)) +
                             shift (along (back, 0.22f * depth));
      const meters_t e = 0.10f * depth + 0.004f * u::m;
      dl.normal (side * hand);
      dl.vertex (eye + shift (along (back, e)));
      dl.vertex (eye + shift (along (ahead, e)));
      dl.vertex (eye - shift (along (back, e)));
      dl.vertex (eye + shift (along (back, e)));
      dl.vertex (eye - shift (along (back, e)));
      dl.vertex (eye - shift (along (ahead, e)));
    }
  }
}
