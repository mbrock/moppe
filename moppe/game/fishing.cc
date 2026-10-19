#include <moppe/game/fishing.hh>

#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace fishing_parts {
    constexpr float tau = 6.2831853f;
    constexpr float pi = 3.14159265f;
    const Vec3 world_up (0, 1, 0);

    // The rod, butt to tip, and how much of it is behind the hand.
    constexpr float rod_length = 2.7f;
    constexpr float rod_butt = 0.32f;
    // The fish are drawn larger than life, as the mushrooms are, so that
    // they show at the end of a line.
    constexpr float fish_drawn = 1.5f;

    float mix (float a, float b, float t) {
      return a + (b - a) * t;
    }

    Vec3 mix (const Vec3& a, const Vec3& b, float t) {
      return a + (b - a) * t;
    }

    Vec3 flat (Vec3 v) {
      v[1] = 0.0f;
      return v;
    }

    Vec3 unit_or (const Vec3& v, const Vec3& otherwise) {
      return length2 (v) > 1e-8f ? normalized (v) : otherwise;
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
                 const Vec3* points,
                 int count,
                 float radius,
                 int sides) {
      for (int i = 0; i + 1 < count; ++i) {
        const Vec3 a = points[i], b = points[i + 1];
        const Vec3 axis = unit_or (b - a, world_up);
        Vec3 u = cross (axis, world_up);
        if (length2 (u) < 1e-6f)
          u = cross (axis, Vec3 (1, 0, 0));
        u = normalized (u);
        const Vec3 v = cross (axis, u);
        for (int s = 0; s < sides; ++s) {
          const float t0 = tau * s / sides;
          const float t1 = tau * (s + 1) / sides;
          const Vec3 n0 = u * std::cos (t0) + v * std::sin (t0);
          const Vec3 n1 = u * std::cos (t1) + v * std::sin (t1);
          const auto put = [&] (const Vec3& p, const Vec3& n) {
            dl.normal (n);
            dl.vertex (p + n * radius);
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
               const Vec3& centre,
               float radius,
               DisplayColor top,
               DisplayColor bottom) {
      constexpr int rings = 6;
      constexpr int sides = 8;
      const auto at = [&] (int ring, int side) {
        const float polar = pi * ring / rings;
        const float around = tau * side / sides;
        return Vec3 (std::sin (polar) * std::cos (around),
                     std::cos (polar),
                     std::sin (polar) * std::sin (around));
      };
      for (int r = 0; r < rings; ++r) {
        dl.color (r < rings / 2 ? top : bottom);
        for (int s = 0; s < sides; ++s) {
          const Vec3 n00 = at (r, s), n01 = at (r, s + 1);
          const Vec3 n10 = at (r + 1, s), n11 = at (r + 1, s + 1);
          const auto put = [&] (const Vec3& n) {
            dl.normal (n);
            dl.vertex (centre + n * radius);
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
      float depth;
      bool bars = false;
      bool spots = false;
      bool pale_spots = false;
      bool sail = false;
    };

    Look look_of (FishKind kind) {
      switch (kind) {
      case FishKind::perch:
        return { { 0.33f, 0.40f, 0.16f },
                 { 0.86f, 0.84f, 0.66f },
                 { 0.85f, 0.38f, 0.12f },
                 0.30f,
                 true };
      case FishKind::pike:
        return { { 0.22f, 0.30f, 0.13f },
                 { 0.82f, 0.80f, 0.62f },
                 { 0.40f, 0.36f, 0.18f },
                 0.17f,
                 false,
                 false,
                 true };
      case FishKind::roach:
        return { { 0.30f, 0.36f, 0.38f },
                 { 0.86f, 0.87f, 0.86f },
                 { 0.80f, 0.25f, 0.16f },
                 0.30f };
      case FishKind::brown_trout:
        return { { 0.40f, 0.33f, 0.16f },
                 { 0.88f, 0.78f, 0.50f },
                 { 0.45f, 0.36f, 0.20f },
                 0.23f,
                 false,
                 true };
      case FishKind::grayling:
        return { { 0.36f, 0.38f, 0.40f },
                 { 0.80f, 0.80f, 0.82f },
                 { 0.45f, 0.30f, 0.40f },
                 0.22f,
                 false,
                 true,
                 false,
                 true };
      case FishKind::arctic_char:
        return { { 0.24f, 0.30f, 0.30f },
                 { 0.92f, 0.42f, 0.18f },
                 { 0.90f, 0.40f, 0.20f },
                 0.22f,
                 false,
                 false,
                 true };
      case FishKind::herring:
        return { { 0.20f, 0.34f, 0.42f },
                 { 0.88f, 0.90f, 0.92f },
                 { 0.60f, 0.66f, 0.70f },
                 0.20f };
      case FishKind::cod:
        return { { 0.42f, 0.38f, 0.22f },
                 { 0.88f, 0.86f, 0.76f },
                 { 0.45f, 0.40f, 0.26f },
                 0.22f,
                 false,
                 true };
      }
      return {
        { 0.4f, 0.4f, 0.4f }, { 0.8f, 0.8f, 0.8f }, { 0.5f, 0.5f, 0.5f }, 0.25f
      };
    }

    DisplayColor blend (DisplayColor a, DisplayColor b, float t) {
      return DisplayColor (mix (a.red, b.red, t),
                           mix (a.green, b.green, t),
                           mix (a.blue, b.blue, t));
    }

    // Fried, a fish goes golden brown, and then black.
    DisplayColor cook (DisplayColor raw, float cooked) {
      const DisplayColor golden (0.66f, 0.44f, 0.20f);
      const DisplayColor burnt (0.09f, 0.07f, 0.06f);
      DisplayColor c = blend (raw, golden, 0.8f * within_unit (cooked));
      return blend (c, burnt, within_unit (cooked - 1.0f));
    }
  }

  const char* fish_name (FishKind kind) {
    switch (kind) {
    case FishKind::perch:
      return "perch";
    case FishKind::pike:
      return "pike";
    case FishKind::roach:
      return "roach";
    case FishKind::brown_trout:
      return "brown trout";
    case FishKind::grayling:
      return "grayling";
    case FishKind::arctic_char:
      return "arctic char";
    case FishKind::herring:
      return "herring";
    case FishKind::cod:
      return "cod";
    }
    return "fish";
  }

  Fish fish_for (Waters waters, bool upland, std::uint32_t seed) {
    struct Odds {
      FishKind kind;
      float weight;
      float least, most; // metres
    };
    static constexpr Odds still[] = {
      { FishKind::perch, 0.48f, 0.14f, 0.36f },
      { FishKind::roach, 0.32f, 0.12f, 0.27f },
      { FishKind::pike, 0.20f, 0.40f, 0.95f },
    };
    static constexpr Odds cold[] = {
      { FishKind::arctic_char, 0.55f, 0.18f, 0.42f },
      { FishKind::brown_trout, 0.25f, 0.20f, 0.45f },
      { FishKind::perch, 0.20f, 0.14f, 0.30f },
    };
    static constexpr Odds running[] = {
      { FishKind::brown_trout, 0.50f, 0.18f, 0.48f },
      { FishKind::grayling, 0.38f, 0.20f, 0.42f },
      { FishKind::perch, 0.12f, 0.14f, 0.30f },
    };
    static constexpr Odds sea[] = {
      { FishKind::herring, 0.45f, 0.15f, 0.26f },
      { FishKind::perch, 0.30f, 0.15f, 0.34f },
      { FishKind::cod, 0.25f, 0.30f, 0.75f },
    };
    const Odds* odds = waters == Waters::running ? running
                       : waters == Waters::sea   ? sea
                       : upland                  ? cold
                                                 : still;
    float pick = hash_lane (seed, 1);
    int i = 0;
    while (i < 2 && pick >= odds[i].weight) {
      pick -= odds[i].weight;
      ++i;
    }
    // Most are small; now and then a big one.
    const float size = std::pow (hash_lane (seed, 2), 1.8f);
    return { .kind = odds[i].kind,
             .length = fishing_parts::mix (odds[i].least, odds[i].most, size),
             .seed = seed };
  }

  float bite_wait (std::uint32_t seed) {
    return 4.0f + 12.0f * hash_lane (seed, 3);
  }

  float play_seconds (const Fish& fish) {
    return 2.2f + 4.0f * fish.length;
  }

  void cast_line (Line& line,
                  const Vec3& stand,
                  const Vec3& aim,
                  Waters waters,
                  bool upland,
                  double now) {
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
    line.bite_at = now + cast_seconds + bite_wait (chance (line));
    line.next_splash = now + cast_seconds;
  }

  bool tend_line (Fishing& fishing, double now, bool strike) {
    using namespace fishing_parts;
    Line& line = fishing.line;
    Creel& creel = fishing.creel;
    const float age = static_cast<float> (now - line.since);
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
        line.bite_at = now + bite_wait (chance (line));
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

  Vec3 fish_in_water (const Line& line, double now) {
    using namespace fishing_parts;
    if (line.phase != Line::Phase::playing)
      return line.bobber;
    const float done = fishing_parts::within_unit (
      static_cast<float> (now - line.since) / play_seconds (line.fish));
    // It runs from side to side as it is wound in to a few metres out.
    const Vec3 in = flat (line.stand - line.bobber);
    const Vec3 side = unit_or (cross (in, world_up), Vec3 (1, 0, 0));
    const float t = static_cast<float> (now - line.since);
    return line.bobber + in * (0.6f * smoothstep (0.0f, 1.0f, done)) +
           side * ((1.0f - done) * 1.1f * std::sin (2.3f * t)) -
           world_up * 0.05f;
  }

  std::optional<RodPose> rod_pose (const Line& line, double now, float time) {
    using namespace fishing_parts;
    const float age = static_cast<float> (now - line.since);
    RodPose rod;
    rod.water = line.bobber;
    switch (line.phase) {
    case Line::Phase::idle:
      return std::nullopt;
    case Line::Phase::casting: {
      // Back over the shoulder, a whip forward, and down to the water as
      // the float flies out.
      rod.raise = age < 0.4f ? mix (0.6f, 1.95f, smoothstep (0.0f, 0.4f, age))
                  : age < 0.55f
                    ? mix (1.95f, 0.25f, (age - 0.4f) / 0.15f)
                    : mix (0.25f, 0.55f, smoothstep (0.55f, 0.9f, age));
      const float flown =
        fishing_parts::within_unit ((age - 0.45f) / (cast_seconds - 0.45f));
      rod.water = line.aim;
      rod.out = flown;
      rod.arc = 2.6f * flown * (1.0f - flown);
      rod.tight = 0.6f * (1.0f - flown);
      break;
    }
    case Line::Phase::waiting: {
      rod.raise = 0.55f + 0.03f * std::sin (0.7f * time);
      // Before the take, a few nibbles.
      const float coming = static_cast<float> (line.bite_at - now);
      const float nibble = coming < 2.2f && coming > 0.0f
                             ? 0.035f *
                                 std::max (0.0f, std::sin (11.0f * time)) *
                                 std::max (0.0f, std::sin (2.9f * time))
                             : 0.0f;
      rod.water[1] += 0.012f * std::sin (2.1f * time) - nibble;
      break;
    }
    case Line::Phase::biting:
      // Pulled under, jerking.
      rod.raise = 0.48f;
      rod.water[1] -= 0.12f + 0.04f * std::sin (17.0f * time);
      rod.bend = 0.25f + 0.1f * std::sin (13.0f * time);
      rod.tight = 0.8f;
      break;
    case Line::Phase::playing:
      rod.raise = 0.95f + 0.12f * std::sin (4.7f * time);
      rod.bend = 0.6f + 0.18f * std::sin (7.3f * time);
      rod.tight = 1.0f;
      rod.water = fish_in_water (line, now);
      rod.fish_on = true;
      rod.fish = line.fish;
      rod.wriggle = std::sin (9.0f * time);
      break;
    case Line::Phase::landed: {
      // Lifted out, swung in, and the rod lowered to bring it to eye
      // level.
      const float lifted = smoothstep (0.0f, 0.6f, age);
      const float lowered = smoothstep (0.7f, 1.4f, age);
      rod.raise = mix (mix (0.95f, 1.2f, lifted), 0.4f, lowered);
      rod.bend = mix (0.5f, 0.15f, lifted);
      rod.out = 1.0f - lifted;
      rod.arc = 0.8f * std::sin (pi * lifted);
      rod.tight = 1.0f;
      rod.fish_on = true;
      rod.fish = line.fish;
      rod.wriggle = std::sin (11.0f * time) * mix (1.0f, 0.4f, lifted);
      break;
    }
    case Line::Phase::empty: {
      const float wound = smoothstep (0.0f, wind_seconds, age);
      rod.raise = mix (0.55f, 1.1f, wound);
      rod.out = 1.0f - wound;
      rod.arc = 0.6f * std::sin (pi * wound);
      rod.tight = 0.7f;
      break;
    }
    }
    return rod;
  }

  Vec3 rod_hand (const AvatarSkeleton& k, float raise) {
    using namespace fishing_parts;
    const Vec3 chest_right =
      unit_or (cross (k.chest_up, k.chest_forward), k.right);
    // Out before the belly; swung back, up by the shoulder.
    const float back = smoothstep (1.2f, 1.9f, raise);
    return k.shoulder[1] + k.chest_forward * mix (0.36f, 0.12f, back) +
           k.chest_up * mix (-0.24f, 0.12f, back) + chest_right * 0.04f;
  }

  void draw_rod (render::DrawList& dl,
                 const AvatarSkeleton& k,
                 const RodPose& rod,
                 float time) {
    using namespace fishing_parts;
    const Vec3 ahead = unit_or (flat (k.forward), Vec3 (0, 0, 1));
    const Vec3 right = unit_or (cross (world_up, ahead), Vec3 (1, 0, 0));
    const Vec3 hand = k.wrist[1];
    const Vec3 along =
      normalized (ahead * std::cos (rod.raise) +
                  world_up * std::sin (rod.raise) + right * 0.10f);
    const Vec3 butt = hand - along * rod_butt;
    const float reach = rod_length - rod_butt;
    const Vec3 straight_tip = hand + along * reach;
    const Vec3 hang = straight_tip - world_up * 0.45f;
    const Vec3 water_end = rod.water + world_up * rod.arc;
    const Vec3 end = mix (hang, water_end, rod.out);

    // The rod bends toward what pulls it, and droops a little anyway.
    Vec3 pull = end - straight_tip;
    pull = pull - along * dot (pull, along);
    const Vec3 toward = unit_or (pull, -world_up);
    const float bend = 0.04f + 0.30f * rod.bend;
    constexpr int pieces = 8;
    Vec3 points[pieces + 1];
    for (int i = 0; i <= pieces; ++i) {
      const float s = static_cast<float> (i) / pieces;
      points[i] =
        butt + along * (rod_length * s) + toward * (bend * rod_length * s * s);
    }
    const Vec3 tip = points[pieces];

    render::DrawState solid;
    solid.cull = false;
    dl.state (solid);
    dl.lit (true);
    dl.fogged (true);
    dl.begin (render::Prim::Triangles);
    // A cork grip, then the blank tapering to the tip.
    dl.color (0.62f, 0.48f, 0.30f);
    {
      const Vec3 grip[2] = { butt, hand + along * 0.08f };
      strand (dl, grip, 2, 0.016f, 6);
    }
    dl.color (0.20f, 0.13f, 0.08f);
    for (int i = 1; i < pieces; ++i) {
      const float s = static_cast<float> (i) / pieces;
      strand (dl, points + i, 2, mix (0.010f, 0.004f, s), 5);
    }
    // The reel under the grip.
    dl.color (0.55f, 0.56f, 0.58f);
    {
      const Vec3 under = hand + along * 0.05f - world_up * 0.05f;
      const Vec3 spool[2] = { under - right * 0.025f, under + right * 0.025f };
      strand (dl, spool, 2, 0.032f, 8);
    }

    // The line, sagging as much as it is slack.
    constexpr int stretch = 14;
    Vec3 line[stretch + 1];
    const float span = length (end - tip);
    const float sag = (0.08f * (1.0f - rod.tight) + 0.005f) * span;
    for (int i = 0; i <= stretch; ++i) {
      const float s = static_cast<float> (i) / stretch;
      line[i] = mix (tip, end, s) - world_up * (sag * 4.0f * s * (1.0f - s));
    }
    dl.color (0.86f, 0.88f, 0.84f);
    strand (dl, line, stretch + 1, 0.006f, 4);

    if (rod.fish_on) {
      // Out of the water it hangs from its mouth; in it, it heads away.
      const Vec3 away = unit_or (flat (end - k.pelvis), ahead);
      const Vec3 along_fish = unit_or (mix (-world_up, away, rod.out), away);
      const Vec3 side = unit_or (cross (along_fish, world_up), right);
      const Vec3 back =
        unit_or (mix (side * -1.0f, world_up, rod.out), world_up);
      draw_fish (dl, rod.fish, end, along_fish, back, 1.0f, rod.wriggle);
    } else {
      // The float: red over white, with a little stem.
      const Vec3 bob = end + world_up * 0.04f;
      ball (dl,
            bob,
            0.065f,
            DisplayColor (0.90f, 0.12f, 0.08f),
            DisplayColor (0.92f, 0.92f, 0.88f));
      dl.color (0.90f, 0.12f, 0.08f);
      const Vec3 stem[2] = { bob, bob + world_up * 0.11f };
      strand (dl, stem, 2, 0.008f, 4);
    }
    (void)time;
    dl.end ();
    dl.state (render::DrawState ());
  }

  void draw_fish (render::DrawList& dl,
                  const Fish& fish,
                  const Vec3& mouth,
                  const Vec3& along_in,
                  const Vec3& back_in,
                  float scale,
                  float wriggle,
                  float cooked) {
    using namespace fishing_parts;
    const Look look = look_of (fish.kind);
    const Vec3 along = unit_or (along_in, Vec3 (0, 0, 1));
    Vec3 back = back_in - along * dot (back_in, along);
    back = unit_or (back, world_up);
    const Vec3 side = normalized (cross (along, back));
    const float body = fish.length * fish_drawn * scale * 0.84f;
    const float depth = fish.length * fish_drawn * scale * look.depth;
    const float width = depth * 0.42f;

    // The spine, wriggling more toward the tail.
    const auto spine = [&] (float u) {
      return mouth + along * (body * u) +
             side * (0.10f * body * wriggle * std::pow (u, 1.6f));
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
    const auto colour_at = [&] (float u, float a, const Vec3& n) {
      const float upness = dot (n, back);
      DisplayColor c =
        blend (look.belly, look.back, smoothstep (-0.35f, 0.45f, upness));
      if (look.bars && upness > -0.2f && std::sin (u * pi * 11.0f) > 0.45f &&
          u > 0.15f && u < 0.9f)
        c = blend (c, DisplayColor (0.08f, 0.10f, 0.05f), 0.6f);
      if ((look.spots || look.pale_spots) && u > 0.12f && upness > -0.3f) {
        const auto cell = static_cast<std::uint32_t> (u * 23.0f) * 31U +
                          static_cast<std::uint32_t> ((a + pi) * 3.0f);
        if (hash_lane (fish.seed + cell, 7) < 0.28f)
          c = blend (c,
                     look.pale_spots ? DisplayColor (0.85f, 0.82f, 0.62f)
                                     : DisplayColor (0.10f, 0.07f, 0.05f),
                     0.55f);
      }
      return cook (c, cooked);
    };
    const auto ring_point = [&] (float u, float a, Vec3& normal) {
      const float g = girth (u);
      const float c = std::cos (a), s = std::sin (a);
      normal = unit_or (side * (c / width) + back * (s / depth) -
                          along * (0.6f * (0.5f - u)),
                        back);
      return spine (u) + side * (c * width * g) + back * (s * depth * g);
    };
    for (int r = 0; r < rings; ++r) {
      const float u0 = static_cast<float> (r) / rings;
      const float u1 = static_cast<float> (r + 1) / rings;
      for (int s = 0; s < sides; ++s) {
        const float a0 = tau * s / sides;
        const float a1 = tau * (s + 1) / sides;
        Vec3 n00, n01, n10, n11;
        const Vec3 p00 = ring_point (u0, a0, n00);
        const Vec3 p01 = ring_point (u0, a1, n01);
        const Vec3 p10 = ring_point (u1, a0, n10);
        const Vec3 p11 = ring_point (u1, a1, n11);
        const auto put = [&] (const Vec3& p, const Vec3& n, float u, float a) {
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
    const auto fin = [&] (const Vec3& a, const Vec3& b, const Vec3& c) {
      dl.normal (side);
      dl.vertex (a);
      dl.vertex (b);
      dl.vertex (c);
    };
    const Vec3 root = spine (1.0f);
    const Vec3 tail_along = unit_or (spine (1.0f) - spine (0.9f), along);
    const float tail = 0.30f * body;
    const Vec3 fork = root + tail_along * (tail * 0.7f);
    fin (root, root + tail_along * tail + back * (0.55f * depth), fork);
    fin (root, fork, root + tail_along * tail - back * (0.55f * depth));
    const float sail = look.sail ? 1.0f : 0.45f;
    fin (spine (0.35f) + back * (0.45f * depth),
         spine (0.40f) + back * (depth * (0.5f + 0.5f * sail)),
         spine (look.sail ? 0.70f : 0.62f) + back * (0.40f * depth));
    // Pectorals by the gills.
    fin (spine (0.22f) + side * width * 0.8f - back * 0.1f * depth,
         spine (0.36f) + side * width * 1.9f - back * 0.35f * depth,
         spine (0.30f) + side * width * 0.8f - back * 0.3f * depth);
    fin (spine (0.22f) - side * width * 0.8f - back * 0.1f * depth,
         spine (0.36f) - side * width * 1.9f - back * 0.35f * depth,
         spine (0.30f) - side * width * 0.8f - back * 0.3f * depth);
    // An eye each side.
    dl.color (0.04f, 0.04f, 0.04f);
    for (const float hand : { 1.0f, -1.0f }) {
      const Vec3 eye =
        spine (0.09f) + side * (hand * width * 0.75f) + back * (0.22f * depth);
      const float e = 0.10f * depth + 0.004f;
      dl.normal (side * hand);
      dl.vertex (eye + back * e);
      dl.vertex (eye + along * e);
      dl.vertex (eye - back * e);
      dl.vertex (eye + back * e);
      dl.vertex (eye - back * e);
      dl.vertex (eye - along * e);
    }
  }
}
