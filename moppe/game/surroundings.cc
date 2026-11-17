// What the walker does by hand, as part of the session's step: the clock,
// picking mushrooms and eggs, the camp's hammock and fire, supper in the
// pan, fishing from the bank or the boat, the beacons, and the animals and
// hens about (surroundings.hh).

#include <moppe/game/surroundings.hh>

#include <moppe/game/camp.hh>
#include <moppe/game/companion.hh>
#include <moppe/game/fishing.hh>
#include <moppe/game/game_session.hh>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <string>

namespace moppe::game {
  namespace {
    const PlacesPlan& places_of (const Surroundings& around) {
      static const PlacesPlan none;
      return around.places ? *around.places : none;
    }

    float ground_at (const Surroundings& around, const Vec3& at) {
      return ground_height_at (*around.surface, at);
    }

    void splash (GameSession& session,
                 const Surroundings& around,
                 const Vec3& at,
                 int drops,
                 float vigour) {
      const float shade = 0.25f + 0.65f * around.daylight;
      Dust::Style spray;
      spray.size = 0.07f * u::m;
      spray.lifetime = 0.7f * u::s;
      spray.downward_acceleration =
        6.0f * isq::acceleration[u::m / pow<2> (u::s)];
      spray.spread = (0.5f + 0.5f * vigour) * one;
      spray.flake = true;
      session.dust ().emit (moppe::position (at + Vec3 (0, 0.03f, 0)),
                            velocity (Vec3 (0, 1.2f + 1.4f * vigour, 0)),
                            drops,
                            DisplayColor (shade, shade * 1.02f, shade),
                            spray);
    }

    // The clock runs with play, and faster for someone lying down to
    // watch it.
    void pass_time (GameSession& session,
                    const Surroundings& around,
                    seconds_t interval) {
      const float dt = seconds_value (interval);
      GameLogicState& logic = session.logic ();
      clock_time_t& hours = logic.m_day_hours;
      hours += around.day_pace * dt *
               logic.m_day_haste.numerical_value_in (one) * si::hour;
      // Two players share one sky: whoever's clock is ahead leads, as
      // when one of them lies down to let the hours run. A little ahead
      // is caught up smoothly, a long way at once.
      if (!around.day_turning || !around.companion_clock)
        return;
      const auto behind = *around.companion_clock - hours;
      if (behind > 0.25 * si::hour)
        hours += behind;
      else if (behind > 0.0 * si::hour)
        hours += std::min (behind, behind * (6.0 * dt));
    }

    // The pair of trees the walker stands between, if a hammock would
    // hang there; looked for again only once they have moved.
    void seek_hammock_site (GameSession& session, const Surroundings& around) {
      GameLogicState& logic = session.logic ();
      const Vec3 feet = session.walker ().position ();
      if (logic.m_hammock_sought_from &&
          length2 (feet - position_value (*logic.m_hammock_sought_from)) <
            0.3f * 0.3f)
        return;
      logic.m_hammock_sought_from = position (feet);
      const std::vector<mov::Trunk> near =
        around.trunks ? around.trunks->gather (feet, 8.0f)
                      : std::vector<mov::Trunk> ();
      const map::SurfaceGeometry& surface = *around.surface;
      logic.m_hammock_at_hand = hammock_site_at (
        near, moppe::position (feet), [&surface] (const plan_position_t& at) {
          return spatial::sample<terrain::surface_elevation> (
            surface, at_height (at, 0.0f * u::m));
        });
    }

    // What fries in the pan gives off a little steam, and then smoke.
    void steam_pan (GameSession& session, const Surroundings& around) {
      GameLogicState& logic = session.logic ();
      Camp& camp = logic.m_camp;
      const play_time_t now = logic.m_total_time;
      Pan& pan = camp.pan;
      if (!pan.on || now < pan.next_steam)
        return;
      const float cooked = pan_cooked (pan, now).numerical_value_in (one);
      pan.next_steam = now + (cooked > 1.2f ? 0.25 : 0.45) * si::second;
      const float shade =
        cooked > 1.2f ? 0.18f : 0.55f + 0.35f * around.daylight;
      Dust::Style steam;
      steam.size = 0.16f * u::m;
      steam.lifetime = 1.6f * u::s;
      steam.downward_acceleration =
        -0.5f * isq::acceleration[u::m / pow<2> (u::s)];
      steam.spread = 0.25f * one;
      session.dust ().emit (
        moppe::position (position_value (pan.place) + Vec3 (0, 0.08f, 0)),
        velocity (Vec3 (0, 0.5f, 0)),
        1,
        DisplayColor (shade, shade, shade),
        steam);
    }

    // On foot by open water, or at the helm of a boat that only floats,
    // the pick key casts a line, and while it is out the key is the
    // line's: it strikes when something takes the float under, and
    // otherwise winds the line in. Walking off, or getting the boat under
    // way, winds it in too; while a fish is fought in and held up, the
    // walker stands and the boat's engine rests.
    void go_fishing (GameSession& session,
                     const Surroundings& around,
                     InputFrame& input,
                     seconds_t interval) {
      const float dt = seconds_value (interval);
      GameLogicState& logic = session.logic ();
      Fishing& fishing = logic.m_fishing;
      Line& line = fishing.line;
      const play_time_t now = logic.m_total_time;
      const std::optional<Angler> from = angler (session);
      const bool afloat = logic.m_doing == Doing::driving (Machine::boat);
      if (!from || (line_out (line) && line.from_boat != afloat)) {
        line.phase = Line::Phase::idle;
        return;
      }
      if (!line_out (line))
        return;

      const bool fast = line.phase == Line::Phase::casting ||
                        line.phase == Line::Phase::waiting ||
                        line.phase == Line::Phase::biting ||
                        line.phase == Line::Phase::playing;
      bool gone;
      if (afloat) {
        // The line goes where the boat drifts; opening the throttle
        // while the float rides, or making way through the water
        // however it comes about, takes it in.
        line.stand = position (from->at);
        const mov::Boat& boat = session.boat ();
        Vec3 way = boat.velocity () - current_at (around, boat.position ());
        way[1] = 0.0f;
        Vec3 out = position_value (line.bobber) - from->at;
        out[1] = 0.0f;
        gone = length2 (way) > 1.5f * 1.5f ||
               length2 (out) > meters_value (longest_cast + 4.0f * u::m) *
                                 meters_value (longest_cast + 4.0f * u::m) ||
               (std::abs (input_value (input.drive)) > 0.1f &&
                (line.phase == Line::Phase::waiting ||
                 line.phase == Line::Phase::biting));
      } else {
        Vec3 moved = from->at - position_value (line.stand);
        moved[1] = 0.0f;
        gone = length2 (moved) >
               meters_value (fishing_leash) * meters_value (fishing_leash);
      }
      if (fast && gone) {
        if (line.phase == Line::Phase::playing) {
          fishing.creel.last = line.fish;
          fishing.creel.last_lost = true;
          fishing.creel.last_time = now;
          line.bobber = position (fish_in_water (line, now));
        }
        line.phase = Line::Phase::empty;
        line.since = now;
      }

      const Line::Phase before = line.phase;
      tend_line (fishing, now, false);
      if (line.phase == Line::Phase::casting ||
          line.phase == Line::Phase::playing ||
          line.phase == Line::Phase::landed) {
        input.drive = 0.0f * one;
        input.turn = 0.0f * one;
        input.boost = 0.0f * one;
        input.run = false;
      }

      // A river carries the float downstream while there is water under
      // it and line to give: held back by a line to the bank, but along
      // with the boat it is cast from, which the river carries too.
      if (line.phase == Line::Phase::waiting) {
        const float carried = line.from_boat ? 0.9f : 0.45f;
        Vec3 next =
          position_value (line.bobber) +
          current_at (around, position_value (line.bobber)) * (carried * dt);
        Vec3 out = next - position_value (line.stand);
        out[1] = 0.0f;
        const float level = water_surface_at (*around.world,
                                              *around.surface,
                                              around.water,
                                              next,
                                              seconds_into_play (now))
                              .numerical_value_in (u::m);
        if (level - ground_at (around, next) > 0.2f &&
            length (out) < meters_value (longest_cast + 2.5f * u::m)) {
          next[1] = level;
          line.bobber = position (next);
        }
      }

      // The water answers: where the float lands, where it is taken
      // under, about a fish fought in, and where it leaves the water.
      if (before == Line::Phase::casting && line.phase == Line::Phase::waiting)
        splash (session, around, position_value (line.bobber), 4, 0.0f);
      if (before == Line::Phase::waiting && line.phase == Line::Phase::biting)
        splash (session, around, position_value (line.bobber), 3, 0.2f);
      if (line.phase == Line::Phase::playing && now >= line.next_splash) {
        line.next_splash = now + 0.18 * si::second;
        splash (session, around, fish_in_water (line, now), 2, 0.5f);
      }
      if (before == Line::Phase::playing && line.phase == Line::Phase::landed)
        splash (session, around, position_value (line.bobber), 8, 1.0f);
    }

    // Sparks fly up from a burning fire and its smoke drifts off; a
    // doused one smoulders a while.
    void feed_fire (GameSession& session, const Surroundings& around) {
      GameLogicState& logic = session.logic ();
      Camp& camp = logic.m_camp;
      const play_time_t now = logic.m_total_time;
      const float burn = fire_burn (camp, now).numerical_value_in (one);
      const bool smouldering =
        !camp.fire && now - camp.fire_time < 8.0 * si::second;
      if (burn <= 0.02f && !smouldering)
        return;
      std::uniform_real_distribution<float> unit (0.0f, 1.0f);
      std::mt19937& rng = logic.m_fx_rng;
      const auto scatter = [&] (float reach) {
        return (unit (rng) - 0.5f) * 2.0f * reach;
      };
      if (burn > 0.3f && now >= camp.next_spark) {
        camp.next_spark = now + (0.10 + 0.55 * unit (rng)) * si::second;
        Dust::Style spark;
        spark.size = 0.05f * u::m;
        spark.lifetime = (1.2f + unit (rng)) * u::s;
        spark.downward_acceleration =
          -0.6f * isq::acceleration[u::m / pow<2> (u::s)];
        spark.spread = 0.5f * one;
        spark.additive = true;
        session.dust ().emit (
          moppe::position (position_value (camp.hearth) +
                           Vec3 (scatter (0.12f), 0.4f, scatter (0.12f))),
          velocity (
            Vec3 (scatter (0.5f), 1.3f + 1.4f * unit (rng), scatter (0.5f))),
          unit (rng) < 0.3f ? 2 : 1,
          DisplayColor (1.0f, 0.62f, 0.22f),
          spark);
      }
      if (now >= camp.next_smoke) {
        camp.next_smoke = now + (smouldering ? 0.22 : 0.5) * si::second;
        // Smoke shows by the light there is to see it in.
        const float shade = 0.06f + 0.36f * around.daylight;
        Dust::Style smoke;
        smoke.size = 0.5f * u::m;
        smoke.lifetime = 4.0f * u::s;
        smoke.downward_acceleration =
          -0.35f * isq::acceleration[u::m / pow<2> (u::s)];
        smoke.spread = 0.45f * one;
        session.dust ().emit (
          moppe::position (position_value (camp.hearth) + Vec3 (0, 0.9f, 0)),
          velocity (Vec3 (0.35f, 0.7f, 0.12f)),
          1,
          DisplayColor (shade * 1.06f, shade, shade * 0.95f),
          smoke);
      }
    }

    // Each beacon burns as fiercely as the fire either player keeps on
    // it; the other's catch and die down here as they are heard of.
    void tend_beacons (GameSession& session, const Surroundings& around) {
      GameLogicState& logic = session.logic ();
      const play_time_t now = logic.m_total_time;
      const std::vector<Beacon>& beacons = logic.m_beacons;
      logic.m_their_beacons.resize (beacons.size ());
      logic.m_beacon_burn.resize (beacons.size ());
      for (std::size_t i = 0; i < beacons.size (); ++i) {
        Beacon& theirs = logic.m_their_beacons[i];
        const bool lit =
          around.companion && i < 32 && (around.companion->beacons >> i & 1u);
        if (lit != theirs.burning ()) {
          (lit ? theirs.lit : theirs.out) = now;
          std::cerr << "moppe: places: the other player "
                    << (lit ? "lit" : "put out") << " the beacon on sanctuary "
                    << i << std::endl;
        }
        logic.m_beacon_burn[i] =
          std::max (beacon_burn (beacons[i].lit, beacons[i].out, now),
                    beacon_burn (theirs.lit, theirs.out, now)) *
          proportion[one];
      }
    }

    // Hanging and taking down the hammock, lighting and dousing the
    // fire, lying down and getting up; and, while lying there, rocking it
    // and looking about, which is all that a sleeper's controls do.
    void tend_camp (GameSession& session,
                    const Surroundings& around,
                    InputFrame& input,
                    seconds_t interval) {
      const float dt = seconds_value (interval);
      GameLogicState& logic = session.logic ();
      Camp& camp = logic.m_camp;
      const play_time_t now = logic.m_total_time;
      float haste = 1.0f;
      float push = 0.0f;
      if (logic.m_doing != Doing::on_foot ()) {
        if (camp.resting) {
          camp.resting = false;
          camp.rest_time = now;
        }
      } else if (camp.resting) {
        if (input_value (input.boost) > 0.5f) {
          get_up (session);
        } else {
          push = input_value (input.turn);
          camp.gaze_yaw = std::clamp (camp.gaze_yaw + input.look_yaw * u::rad,
                                      -2.6f * u::rad,
                                      2.6f * u::rad);
          camp.gaze_pitch = std::clamp (camp.gaze_pitch + input.look_pitch *
                                                            inclination[u::rad],
                                        -0.2f * inclination[u::rad],
                                        1.5f * inclination[u::rad]);
          // After a quiet moment the hours begin to run; holding back
          // keeps them at their ordinary pace.
          if (input_value (input.drive) > -0.3f)
            haste =
              1.0f +
              (resting_haste - 1.0f) *
                smoothstep (2.5f, 7.0f, seconds_between (now, camp.rest_time));
        }
        const bool cycle = input.cycle_camera;
        input = {};
        input.cycle_camera = cycle;
      } else {
        seek_hammock_site (session, around);
      }
      // The hammock is a pendulum, damped more when no one is in it.
      camp.swing_rate +=
        (-6.8f / (u::s * u::s) * camp.swing -
         (camp.resting ? 0.30f : 0.9f) / u::s * camp.swing_rate +
         1.5f * push * u::rad / (u::s * u::s)) *
        interval;
      camp.swing = std::clamp (
        camp.swing + camp.swing_rate * interval, -0.6f * u::rad, 0.6f * u::rad);
      logic.m_day_haste += (haste * scale_factor[one] - logic.m_day_haste) *
                           std::min (1.0f, 1.5f * dt);
      feed_fire (session, around);
      tend_beacons (session, around);
    }
  }

  float ground_height_at (const map::SurfaceGeometry& surface, const Vec3& at) {
    return terrain::surface_elevation_value (
      spatial::sample<terrain::surface_elevation> (
        surface, moppe::position (Vec3 (at[0], 0.0f, at[2]))));
  }

  bool beside_hammock (const GameSession& session) {
    const Camp& camp = session.logic ().m_camp;
    return camp.hung &&
           hammock_distance (camp.hammock,
                             moppe::position (session.walker ().position ())) <
             1.5f * u::m;
  }

  std::optional<Vec3> hearth_ahead (const GameSession& session,
                                    const Surroundings& around) {
    const Walker& walker = session.walker ();
    Vec3 ahead = walker.heading ();
    ahead[1] = 0.0f;
    if (length2 (ahead) < 1e-6f)
      return std::nullopt;
    ahead = normalized (ahead);
    Vec3 hearth = walker.position () + ahead * 1.3f;
    hearth[1] = ground_at (around, hearth);
    if (hearth[1] < around.world->water_level.numerical_value_in (u::m) + 0.2f)
      return std::nullopt;
    const Vec3 side (ahead[2], 0.0f, -ahead[0]);
    for (const Vec3& off :
         { ahead * 0.5f, ahead * -0.5f, side * 0.5f, side * -0.5f })
      if (std::fabs (ground_at (around, hearth + off) - hearth[1]) > 0.22f)
        return std::nullopt;
    return hearth;
  }

  std::optional<Vec3> fire_at_hand (const GameSession& session,
                                    const Surroundings& around) {
    const GameLogicState& logic = session.logic ();
    if (logic.m_doing != Doing::on_foot () || logic.m_camp.resting)
      return std::nullopt;
    const Vec3 feet = session.walker ().position ();
    const Camp& camp = logic.m_camp;
    std::optional<Vec3> found;
    float nearest = 2.4f * 2.4f;
    const auto consider = [&] (const Vec3& hearth, float burn) {
      Vec3 to = feet - hearth;
      to[1] = 0.0f;
      if (burn > 0.5f && length2 (to) < nearest) {
        nearest = length2 (to);
        found = hearth;
      }
    };
    consider (position_value (camp.hearth),
              fire_burn (camp, logic.m_total_time).numerical_value_in (one));
    if (around.companion && around.companion->hearth_laid)
      consider (position_value (around.companion->hearth),
                around.companion->fire_burn.numerical_value_in (one));
    const PlacesPlan& places = places_of (around);
    for (std::size_t i = 0;
         i < places.sanctuaries.size () && i < logic.m_beacon_burn.size ();
         ++i)
      consider (position_value (places.sanctuaries[i].hearth),
                logic.m_beacon_burn[i].numerical_value_in (one));
    return found;
  }

  bool beside_pan (const GameSession& session) {
    const GameLogicState& logic = session.logic ();
    const Pan& pan = logic.m_camp.pan;
    if (!pan.on || logic.m_doing != Doing::on_foot () || logic.m_camp.resting)
      return false;
    Vec3 to = session.walker ().position () - position_value (pan.place);
    to[1] = 0.0f;
    return length2 (to) < meters_value (pan_reach) * meters_value (pan_reach);
  }

  Supper supper_at_hand (const GameSession& session,
                         const Surroundings& around) {
    const GameLogicState& logic = session.logic ();
    const Camp& camp = logic.m_camp;
    const Basket& basket = logic.m_basket;
    if (beside_pan (session)) {
      const float cooked =
        pan_cooked (camp.pan, logic.m_total_time).numerical_value_in (one);
      return cooked < 1.0f   ? Supper::frying
             : cooked > 1.4f ? Supper::eat_burnt
                             : Supper::eat;
    }
    if (!camp.pan.on &&
        (basket.used < basket.picked.size () || basket.eggs > 0 ||
         logic.m_fishing.creel.kept () > 0) &&
        fire_at_hand (session, around))
      return Supper::fry;
    return Supper::none;
  }

  std::optional<std::size_t> beacon_at_hand (const GameSession& session,
                                             const Surroundings& around) {
    const GameLogicState& logic = session.logic ();
    if (logic.m_doing != Doing::on_foot () || logic.m_camp.resting)
      return std::nullopt;
    return sanctuary_at_hand (places_of (around),
                              moppe::position (session.walker ().position ()));
  }

  bool their_beacon (const GameLogicState& logic, std::size_t i) {
    return i < logic.m_their_beacons.size () &&
           logic.m_their_beacons[i].burning ();
  }

  std::optional<std::size_t> egg_at_hand (const GameSession& session,
                                          const Surroundings& around) {
    const GameLogicState& logic = session.logic ();
    if (logic.m_doing != Doing::on_foot () || logic.m_camp.resting)
      return std::nullopt;
    const PlacesPlan& places = places_of (around);
    return nest_within_reach (around.nests,
                              logic.m_nests,
                              session.walker ().position (),
                              session.walker ().heading (),
                              plan_value (places.period).x (),
                              plan_value (places.period).z ());
  }

  std::optional<Angler> angler (const GameSession& session) {
    const GameLogicState& logic = session.logic ();
    if (logic.m_doing == Doing::on_foot () && !logic.m_camp.resting) {
      const Walker& walker = session.walker ();
      return Angler { walker.position (), walker.heading () };
    }
    if (logic.m_doing == Doing::driving (Machine::boat)) {
      const mov::Boat& boat = session.boat ();
      return Angler { boat.helm_seat (),
                      Quaternion::rotate (
                        boat.forward (), Vec3 (0, 1, 0), -logic.m_look_yaw) };
    }
    return std::nullopt;
  }

  Vec3 current_at (const Surroundings& around, const Vec3& at) {
    if (!around.water)
      return Vec3 ();
    Vec3 current = spatial::sample<terrain::water_velocity> (
                     *around.water, moppe::position (Vec3 (at[0], 0.0f, at[2])))
                     .numerical_value_in (u::m / u::s);
    current[1] = 0.0f;
    return current;
  }

  bool boat_drifting (const GameSession& session, const Surroundings& around) {
    const mov::Boat& boat = session.boat ();
    Vec3 way = boat.velocity () - current_at (around, boat.position ());
    way[1] = 0.0f;
    return boat.afloat () && abs (boat.state ().drive) < 0.1f * one &&
           length2 (way) < 0.8f * 0.8f;
  }

  std::optional<FishingWater> water_ahead (const GameSession& session,
                                           const Surroundings& around) {
    const std::optional<Angler> from = angler (session);
    if (!from || !around.water)
      return std::nullopt;
    Vec3 ahead = from->ahead;
    ahead[1] = 0.0f;
    if (length2 (ahead) < 1e-6f)
      return std::nullopt;
    ahead = normalized (ahead);
    if (session.logic ().m_doing == Doing::driving (Machine::boat)) {
      Vec3 bow = session.boat ().forward ();
      bow[1] = 0.0f;
      if (length2 (bow) < 1e-6f || dot (ahead, normalized (bow)) < -0.17f)
        return std::nullopt;
    }
    const terrain::WaterSheets& sheets = *around.water;
    const float sea = around.world->water_level.numerical_value_in (u::m);
    for (meters_t reach = longest_cast; reach >= shortest_cast;
         reach -= 0.5f * u::m) {
      Vec3 at = from->at + ahead * meters_value (reach);
      const float ground = ground_at (around, at);
      const float level =
        still_water_at (*around.world, *around.surface, &sheets, at)
          .numerical_value_in (u::m);
      if (level - ground < meters_value (fishing_depth))
        continue;
      at[1] = level;
      FishingWater found { .aim = at, .upland = level > sea + 150.0f };
      const position_t where = moppe::position (Vec3 (at[0], 0.0f, at[2]));
      const float sheet = terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (sheets, where));
      if (sheet > ground && sheet > sea + 0.05f)
        found.waters = length (current_at (around, at)) > 0.15f
                         ? Waters::running
                         : Waters::still;
      else
        found.waters = Waters::sea;
      return found;
    }
    return std::nullopt;
  }

  std::optional<std::uint32_t> mushroom_at_hand (const GameSession& session,
                                                 const Surroundings& around) {
    if (!around.mushrooms || session.logic ().m_doing != Doing::on_foot ())
      return std::nullopt;
    const Walker& walker = session.walker ();
    return around.mushrooms->within_reach (position (walker.position ()),
                                           unit_direction (walker.heading ()));
  }

  void pick (GameSession& session,
             const Surroundings& around,
             std::uint32_t mushroom) {
    GameLogicState& logic = session.logic ();
    Basket& basket = logic.m_basket;
    const MushroomSite& site = around.mushrooms->site (mushroom);
    basket.last_reach_time = logic.m_total_time;
    basket.last_egg = false;
    basket.last_kind = site.kind;
    // A poisonous one is left where it grows.
    basket.last_refused = !mushroom_edible (site.kind);
    if (basket.last_refused)
      return;
    basket.picked.push_back (mushroom);
    ++basket.count[static_cast<int> (site.kind)];
  }

  void take_egg (GameSession& session, std::size_t nest) {
    GameLogicState& logic = session.logic ();
    Basket& basket = logic.m_basket;
    if (basket.eggs >= basket_holds_eggs)
      return;
    --logic.m_nests[nest].eggs;
    ++basket.eggs;
    basket.last_reach_time = logic.m_total_time;
    basket.last_egg = true;
    basket.last_refused = false;
  }

  void cook (GameSession& session, const Surroundings& around) {
    GameLogicState& logic = session.logic ();
    Camp& camp = logic.m_camp;
    Basket& basket = logic.m_basket;
    const play_time_t now = logic.m_total_time;
    switch (supper_at_hand (session, around)) {
    case Supper::fry: {
      // A pan set on the fire's stones.
      Pan pan = pan_set_on (moppe::position (*fire_at_hand (session, around)),
                            moppe::position (session.walker ().position ()));
      pan.since = now;
      pan.next_steam = now + 1.0 * si::second;
      // A fish first, if there is one, and mushrooms beside it.
      Creel& creel = logic.m_fishing.creel;
      if (creel.kept () > 0) {
        pan.has_fish = true;
        pan.fish = creel.fish[creel.used++];
      }
      // Then eggs, and mushrooms in the room they leave.
      const int room = Pan::room - (pan.has_fish ? 2 : 0);
      pan.eggs = std::min (basket.eggs, room);
      basket.eggs -= pan.eggs;
      while (pan.count + pan.eggs < room &&
             basket.used < basket.picked.size ()) {
        const std::uint32_t index = basket.picked[basket.used++];
        pan.mushrooms[pan.count++] = index;
        if (around.mushrooms && index < around.mushrooms->mushroom_count ())
          --basket
              .count[static_cast<int> (around.mushrooms->site (index).kind)];
      }
      camp.pan = pan;
      break;
    }
    case Supper::eat:
    case Supper::eat_burnt:
      // Eating mends the walker, less so what was left to burn.
      camp.ate_cooked =
        pan_cooked (camp.pan, now).numerical_value_in (one) * proportion[one];
      camp.ate_count = camp.pan.count;
      camp.ate_eggs = camp.pan.eggs;
      camp.ate_fish = camp.pan.has_fish;
      camp.ate = camp.pan.fish;
      camp.ate_time = now;
      camp.pan.on = false;
      logic.m_health = std::min (
        100.0f * health[one],
        logic.m_health +
          (camp.ate_cooked > 1.4f * proportion[one] ? 5.0f : 20.0f) *
            (camp.ate_count + camp.ate_eggs + (camp.ate_fish ? 2 : 0)) *
            health[one]);
      break;
    case Supper::frying:
    case Supper::none:
      break;
    }
  }

  void cast (GameSession& session, const Surroundings& around) {
    GameLogicState& logic = session.logic ();
    const std::optional<Angler> from = angler (session);
    const std::optional<FishingWater> water = water_ahead (session, around);
    if (!from || !water)
      return;
    Line& line = logic.m_fishing.line;
    cast_line (line,
               from->at,
               water->aim,
               water->waters,
               water->upland,
               logic.m_total_time);
    line.from_boat = logic.m_doing == Doing::driving (Machine::boat);
  }

  void tend_line_by_hand (GameSession& session) {
    GameLogicState& logic = session.logic ();
    tend_line (logic.m_fishing, logic.m_total_time, true);
  }

  void hang_hammock (GameSession& session) {
    GameLogicState& logic = session.logic ();
    if (!logic.m_hammock_at_hand)
      return;
    Camp& camp = logic.m_camp;
    camp.hung = true;
    camp.hammock = *logic.m_hammock_at_hand;
    camp.swing = 0.0f * u::rad;
    camp.swing_rate = 0.5f * u::rad / u::s;
  }

  void take_down_hammock (GameSession& session) {
    session.logic ().m_camp.hung = false;
  }

  void lie_down (GameSession& session) {
    GameLogicState& logic = session.logic ();
    Camp& camp = logic.m_camp;
    camp.resting = true;
    camp.rest_time = logic.m_total_time;
    camp.swing_rate += 0.8f * u::rad / u::s;
    camp.gaze_yaw = 0.0f * u::rad;
    camp.gaze_pitch = 1.1f * inclination[u::rad];
  }

  void get_up (GameSession& session) {
    GameLogicState& logic = session.logic ();
    Camp& camp = logic.m_camp;
    if (logic.m_total_time - camp.rest_time <= 0.8 * si::second)
      return;
    camp.resting = false;
    camp.rest_time = logic.m_total_time;
  }

  void tend_beacon (GameSession& session,
                    const Surroundings& around,
                    std::size_t beacon) {
    // A sanctuary's fire is lit on its platform, and shows from the
    // summits that see it.
    GameLogicState& logic = session.logic ();
    const play_time_t now = logic.m_total_time;
    Beacon& b = logic.m_beacons[beacon];
    if (b.burning () || !their_beacon (logic, beacon))
      (b.burning () ? b.out : b.lit) = now;
    const PlacesPlan& places = places_of (around);
    std::cerr << "moppe: places: the beacon on sanctuary " << beacon
              << (b.burning () ? " lit, seen from " : " put out")
              << (b.burning () ? std::to_string (
                                   places.sanctuaries[beacon].seen_from.size ())
                               : std::string ())
              << (b.burning () ? " others" : "") << std::endl;
  }

  std::optional<Vec3> fire_place (const GameSession& session,
                                  const Surroundings& around) {
    // An old fire ring takes the fire, or else level ground ahead.
    if (const std::optional<position_t> ring = hearth_at_hand (
          places_of (around), moppe::position (session.walker ().position ())))
      return position_value (*ring);
    return hearth_ahead (session, around);
  }

  bool beside_fire (const GameSession& session) {
    const Camp& camp = session.logic ().m_camp;
    Vec3 to_fire = session.walker ().position () - position_value (camp.hearth);
    to_fire[1] = 0.0f;
    return camp.fire && length2 (to_fire) < 3.0f * 3.0f;
  }

  void light_fire (GameSession& session, const Vec3& hearth) {
    GameLogicState& logic = session.logic ();
    Camp& camp = logic.m_camp;
    camp.fire = true;
    camp.hearth = position (hearth);
    camp.fire_time = logic.m_total_time;
    camp.next_spark = camp.next_smoke = logic.m_total_time;
  }

  void douse_fire (GameSession& session) {
    GameLogicState& logic = session.logic ();
    logic.m_camp.fire = false;
    logic.m_camp.fire_time = logic.m_total_time;
  }

  void tend_by_hand (GameSession& session,
                     const Surroundings& around,
                     InputFrame& input,
                     seconds_t dt) {
    pass_time (session, around, dt);
    go_fishing (session, around, input, dt);
    steam_pan (session, around);
    tend_camp (session, around, input, dt);
  }

  void tend_animals (GameSession& session,
                     const Surroundings& around,
                     seconds_t interval) {
    GameLogicState& logic = session.logic ();
    const float dt = seconds_value (interval);
    if (!around.animal_ground || session.animals ().animals ().empty () ||
        dt <= 0.0f)
      return;
    {
      std::vector<Presence> presences;
      const Vec3 subject = session.subject_position ();
      Vec3 moving {};
      if (logic.m_last_subject)
        moving =
          (subject - position_value (*logic.m_last_subject)) * (1.0f / dt);
      logic.m_last_subject = position (subject);
      if (length2 (moving) > 60.0f * 60.0f)
        moving = Vec3 ();
      presences.push_back ({ .position = subject,
                             .velocity = moving,
                             .machine = logic.m_doing != Doing::on_foot () });
      if (around.companion && around.companion->walker)
        presences.push_back (
          { .position = position_value (around.companion->walker->position),
            .velocity = velocity_value (around.companion->walker->velocity) });
      const AnimalGround& ground = *around.animal_ground;
      session.animals ().step (ground,
                               presences,
                               session.camera ().position (),
                               700.0f * u::m,
                               interval);
      session.hens ().step (ground,
                            presences,
                            session.camera ().position (),
                            160.0f * u::m,
                            static_cast<float> (std::fmod (
                              hours_since_midnight (logic.m_day_hours), 24.0)) *
                              si::hour,
                            interval);
    }
    lay_eggs (logic.m_nests, logic.m_day_hours, around.egg_seed);
  }
}
