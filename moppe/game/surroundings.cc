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

    std::optional<std::uint32_t> mushroom_at_hand (const GameSession& session,
                                                   const Surroundings& around) {
      if (!around.mushrooms)
        return std::nullopt;
      const Walker& walker = session.walker ();
      return around.mushrooms->within_reach (walker.position (),
                                             walker.heading ());
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
      hours += around.day_pace * dt * logic.m_day_haste * si::hour;
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

    // A walker reaches for the mushroom at hand: an edible one goes in
    // the basket, a poisonous one is left where it grows.
    void reach_for_mushroom (GameSession& session,
                             const Surroundings& around,
                             const InputFrame& input) {
      GameLogicState& logic = session.logic ();
      if (logic.m_mode != M_FOOT)
        return;
      const Vec3 feet = session.walker ().position ();
      // F still remounts the bike when it is close enough; otherwise it
      // picks too, for the touch screen's single action button.
      const bool bike_near =
        length2 (feet - session.bike ().position ()) < 5.0f * 5.0f ||
        session.mount_within_reach ().has_value () || session.hauling ();
      if (!input.deploy_glider && !(input.toggle_mount && !bike_near))
        return;
      const std::optional<std::uint32_t> found =
        mushroom_at_hand (session, around);
      Basket& basket = logic.m_basket;
      if (!found) {
        // Into a nest box for an egg, as many as the basket takes.
        const std::optional<std::size_t> nest = egg_at_hand (session, around);
        if (!nest || basket.eggs >= basket_holds_eggs)
          return;
        --logic.m_nests[*nest].eggs;
        ++basket.eggs;
        basket.last_reach_time = logic.m_total_time;
        basket.last_egg = true;
        basket.last_refused = false;
        return;
      }
      const MushroomSite& site = around.mushrooms->site (*found);
      basket.last_reach_time = logic.m_total_time;
      basket.last_egg = false;
      basket.last_kind = site.kind;
      basket.last_refused = !mushroom_edible (site.kind);
      if (basket.last_refused)
        return;
      basket.picked.push_back (*found);
      ++basket.count[static_cast<int> (site.kind)];
    }

    // The pair of trees the walker stands between, if a hammock would
    // hang there; looked for again only once they have moved.
    void seek_hammock_site (GameSession& session, const Surroundings& around) {
      GameLogicState& logic = session.logic ();
      const Vec3 feet = session.walker ().position ();
      if (logic.m_hammock_sought_from &&
          length2 (feet - *logic.m_hammock_sought_from) < 0.3f * 0.3f)
        return;
      logic.m_hammock_sought_from = feet;
      const std::vector<mov::Trunk> near =
        around.trunks ? around.trunks->gather (feet, 8.0f)
                      : std::vector<mov::Trunk> ();
      const map::SurfaceGeometry& surface = *around.surface;
      logic.m_hammock_at_hand =
        hammock_site_at (near, feet, [&surface] (float x, float z) {
          return ground_height_at (surface, Vec3 (x, 0.0f, z));
        });
    }

    // By a fire, the pick key sets a pan of mushrooms from the basket on
    // its stones; once they are fried it eats them, which mends the
    // walker -- less so when they were left to burn.
    void cook_supper (GameSession& session,
                      const Surroundings& around,
                      InputFrame& input) {
      GameLogicState& logic = session.logic ();
      Camp& camp = logic.m_camp;
      Basket& basket = logic.m_basket;
      const play_time_t now = logic.m_total_time;
      if (logic.m_mode == M_FOOT && input.deploy_glider &&
          !mushroom_at_hand (session, around)) {
        switch (supper_at_hand (session, around)) {
        case HudState::Cook::fry: {
          Pan pan = pan_set_on (*fire_at_hand (session, around),
                                session.walker ().position ());
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
              --basket.count[static_cast<int> (
                around.mushrooms->site (index).kind)];
          }
          camp.pan = pan;
          input.deploy_glider = false;
          break;
        }
        case HudState::Cook::eat:
        case HudState::Cook::eat_burnt:
          camp.ate_cooked = pan_cooked (camp.pan, now);
          camp.ate_count = camp.pan.count;
          camp.ate_eggs = camp.pan.eggs;
          camp.ate_fish = camp.pan.has_fish;
          camp.ate = camp.pan.fish;
          camp.ate_time = now;
          camp.pan.on = false;
          logic.m_health =
            std::min (100.0f,
                      logic.m_health + (camp.ate_cooked > 1.4f ? 5.0f : 20.0f) *
                                         (camp.ate_count + camp.ate_eggs +
                                          (camp.ate_fish ? 2 : 0)));
          input.deploy_glider = false;
          break;
        case HudState::Cook::frying:
          input.deploy_glider = false;
          break;
        case HudState::Cook::none:
          break;
        }
      }

      // What fries gives off a little steam, and then smoke.
      Pan& pan = camp.pan;
      if (!pan.on || now < pan.next_steam)
        return;
      const float cooked = pan_cooked (pan, now);
      pan.next_steam = now + (cooked > 1.2f ? 0.25 : 0.45) * si::second;
      const float shade =
        cooked > 1.2f ? 0.18f : 0.55f + 0.35f * around.daylight;
      Dust::Style steam;
      steam.size = 0.16f * u::m;
      steam.lifetime = 1.6f * u::s;
      steam.downward_acceleration =
        -0.5f * isq::acceleration[u::m / pow<2> (u::s)];
      steam.spread = 0.25f * one;
      session.dust ().emit (moppe::position (pan.place + Vec3 (0, 0.08f, 0)),
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
      const bool afloat = logic.m_mode == M_BOAT;
      if (!from || (line_out (line) && line.from_boat != afloat)) {
        line.phase = Line::Phase::idle;
        return;
      }
      if (!line_out (line)) {
        if (!input.deploy_glider ||
            !(afloat ? boat_drifting (session, around)
                     : nothing_else_at_hand (session, around)))
          return;
        if (const std::optional<FishingWater> water =
              water_ahead (session, around)) {
          cast_line (
            line, from->at, water->aim, water->waters, water->upland, now);
          line.from_boat = afloat;
          input.deploy_glider = false;
        }
        return;
      }

      const bool fast = line.phase == Line::Phase::casting ||
                        line.phase == Line::Phase::waiting ||
                        line.phase == Line::Phase::biting ||
                        line.phase == Line::Phase::playing;
      bool gone;
      if (afloat) {
        // The line goes where the boat drifts; opening the throttle
        // while the float rides, or making way through the water
        // however it comes about, takes it in.
        line.stand = from->at;
        const mov::Boat& boat = session.boat ();
        Vec3 way = boat.velocity () - current_at (around, boat.position ());
        way[1] = 0.0f;
        Vec3 out = line.bobber - from->at;
        out[1] = 0.0f;
        gone = length2 (way) > 1.5f * 1.5f ||
               length2 (out) > meters_value (longest_cast + 4.0f * u::m) *
                                 meters_value (longest_cast + 4.0f * u::m) ||
               (std::abs (input_value (input.drive)) > 0.1f &&
                (line.phase == Line::Phase::waiting ||
                 line.phase == Line::Phase::biting));
      } else {
        Vec3 moved = from->at - line.stand;
        moved[1] = 0.0f;
        gone = length2 (moved) >
               meters_value (fishing_leash) * meters_value (fishing_leash);
      }
      if (fast && gone) {
        if (line.phase == Line::Phase::playing) {
          fishing.creel.last = line.fish;
          fishing.creel.last_lost = true;
          fishing.creel.last_time = now;
          line.bobber = fish_in_water (line, now);
        }
        line.phase = Line::Phase::empty;
        line.since = now;
      }

      const Line::Phase before = line.phase;
      if (tend_line (fishing, now, input.deploy_glider))
        input.deploy_glider = false;
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
          line.bobber + current_at (around, line.bobber) * (carried * dt);
        Vec3 out = next - line.stand;
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
          line.bobber = next;
        }
      }

      // The water answers: where the float lands, where it is taken
      // under, about a fish fought in, and where it leaves the water.
      if (before == Line::Phase::casting && line.phase == Line::Phase::waiting)
        splash (session, around, line.bobber, 4, 0.0f);
      if (before == Line::Phase::waiting && line.phase == Line::Phase::biting)
        splash (session, around, line.bobber, 3, 0.2f);
      if (line.phase == Line::Phase::playing && now >= line.next_splash) {
        line.next_splash = now + 0.18 * si::second;
        splash (session, around, fish_in_water (line, now), 2, 0.5f);
      }
      if (before == Line::Phase::playing && line.phase == Line::Phase::landed)
        splash (session, around, line.bobber, 8, 1.0f);
    }

    // Sparks fly up from a burning fire and its smoke drifts off; a
    // doused one smoulders a while.
    void feed_fire (GameSession& session, const Surroundings& around) {
      GameLogicState& logic = session.logic ();
      Camp& camp = logic.m_camp;
      const play_time_t now = logic.m_total_time;
      const float burn = fire_burn (camp, now);
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
          moppe::position (camp.hearth +
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
          moppe::position (camp.hearth + Vec3 (0, 0.9f, 0)),
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
                    beacon_burn (theirs.lit, theirs.out, now));
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
      if (logic.m_mode != M_FOOT) {
        if (camp.resting) {
          camp.resting = false;
          camp.rest_time = now;
        }
      } else if (camp.resting) {
        const bool rise = input.deploy_glider || input.toggle_mount ||
                          input_value (input.boost) > 0.5f;
        if (rise && now - camp.rest_time > 0.8 * si::second) {
          camp.resting = false;
          camp.rest_time = now;
        } else {
          push = input_value (input.turn);
          camp.gaze_yaw =
            std::clamp (camp.gaze_yaw + input.look_yaw, -2.6f, 2.6f);
          camp.gaze_pitch =
            std::clamp (camp.gaze_pitch + input.look_pitch, -0.2f, 1.5f);
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
        const Walker& walker = session.walker ();
        const Vec3 feet = walker.position ();
        if (input.hang_hammock) {
          if (beside_hammock (session)) {
            camp.hung = false;
          } else if (logic.m_hammock_at_hand) {
            camp.hung = true;
            camp.hammock = *logic.m_hammock_at_hand;
            camp.swing = 0.0f;
            camp.swing_rate = 0.5f;
          }
        }
        const PlacesPlan& places = places_of (around);
        if (const std::optional<std::size_t> beacon =
              beacon_at_hand (session, around);
            input.light_fire && beacon) {
          // A sanctuary's fire is lit on its platform, and shows from
          // the summits that see it.
          Beacon& b = logic.m_beacons[*beacon];
          if (b.burning () || !their_beacon (logic, *beacon))
            (b.burning () ? b.out : b.lit) = now;
          std::cerr << "moppe: places: the beacon on sanctuary " << *beacon
                    << (b.burning () ? " lit, seen from " : " put out")
                    << (b.burning ()
                          ? std::to_string (
                              places.sanctuaries[*beacon].seen_from.size ())
                          : std::string ())
                    << (b.burning () ? " others" : "") << std::endl;
        } else if (input.light_fire) {
          Vec3 to_fire = feet - camp.hearth;
          to_fire[1] = 0.0f;
          if (camp.fire && length2 (to_fire) < 3.0f * 3.0f) {
            camp.fire = false;
            camp.fire_time = now;
          } else if (const std::optional<Vec3> ring =
                       hearth_at_hand (places, feet)) {
            // An old fire ring takes the fire.
            camp.fire = true;
            camp.hearth = *ring;
            camp.fire_time = now;
            camp.next_spark = camp.next_smoke = now;
          } else if (const std::optional<Vec3> hearth =
                       hearth_ahead (session, around)) {
            camp.fire = true;
            camp.hearth = *hearth;
            camp.fire_time = now;
            camp.next_spark = camp.next_smoke = now;
          }
        }
        const bool picking = mushroom_at_hand (session, around).has_value ();
        if (input.deploy_glider && !picking && beside_hammock (session)) {
          camp.resting = true;
          camp.rest_time = now;
          camp.swing_rate += 0.8f;
          camp.gaze_yaw = 0.0f;
          camp.gaze_pitch = 1.1f;
          input = {};
        }
      }
      // The hammock is a pendulum, damped more when no one is in it.
      camp.swing_rate +=
        (-6.8f * camp.swing - (camp.resting ? 0.30f : 0.9f) * camp.swing_rate +
         1.5f * push) *
        dt;
      camp.swing = std::clamp (camp.swing + camp.swing_rate * dt, -0.6f, 0.6f);
      logic.m_day_haste +=
        (haste - logic.m_day_haste) * std::min (1.0f, 1.5f * dt);
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
           hammock_distance (camp.hammock, session.walker ().position ()) <
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
    if (logic.m_mode != M_FOOT || logic.m_camp.resting)
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
    consider (camp.hearth, fire_burn (camp, logic.m_total_time));
    if (around.companion && around.companion->hearth_laid)
      consider (around.companion->hearth, around.companion->fire_burn);
    const PlacesPlan& places = places_of (around);
    for (std::size_t i = 0;
         i < places.sanctuaries.size () && i < logic.m_beacon_burn.size ();
         ++i)
      consider (places.sanctuaries[i].hearth, logic.m_beacon_burn[i]);
    return found;
  }

  bool beside_pan (const GameSession& session) {
    const GameLogicState& logic = session.logic ();
    const Pan& pan = logic.m_camp.pan;
    if (!pan.on || logic.m_mode != M_FOOT || logic.m_camp.resting)
      return false;
    Vec3 to = session.walker ().position () - pan.place;
    to[1] = 0.0f;
    return length2 (to) < meters_value (pan_reach) * meters_value (pan_reach);
  }

  HudState::Cook supper_at_hand (const GameSession& session,
                                 const Surroundings& around) {
    const GameLogicState& logic = session.logic ();
    const Camp& camp = logic.m_camp;
    const Basket& basket = logic.m_basket;
    if (beside_pan (session)) {
      const float cooked = pan_cooked (camp.pan, logic.m_total_time);
      return cooked < 1.0f   ? HudState::Cook::frying
             : cooked > 1.4f ? HudState::Cook::eat_burnt
                             : HudState::Cook::eat;
    }
    if (!camp.pan.on &&
        (basket.used < basket.picked.size () || basket.eggs > 0 ||
         logic.m_fishing.creel.kept () > 0) &&
        fire_at_hand (session, around))
      return HudState::Cook::fry;
    return HudState::Cook::none;
  }

  std::optional<std::size_t> beacon_at_hand (const GameSession& session,
                                             const Surroundings& around) {
    const GameLogicState& logic = session.logic ();
    if (logic.m_mode != M_FOOT || logic.m_camp.resting)
      return std::nullopt;
    return sanctuary_at_hand (places_of (around),
                              session.walker ().position ());
  }

  bool their_beacon (const GameLogicState& logic, std::size_t i) {
    return i < logic.m_their_beacons.size () &&
           logic.m_their_beacons[i].burning ();
  }

  std::optional<std::size_t> egg_at_hand (const GameSession& session,
                                          const Surroundings& around) {
    const GameLogicState& logic = session.logic ();
    if (logic.m_mode != M_FOOT || logic.m_camp.resting)
      return std::nullopt;
    const PlacesPlan& places = places_of (around);
    return nest_within_reach (around.nests,
                              logic.m_nests,
                              session.walker ().position (),
                              session.walker ().heading (),
                              places.period_x,
                              places.period_z);
  }

  std::optional<Angler> angler (const GameSession& session) {
    const GameLogicState& logic = session.logic ();
    if (logic.m_mode == M_FOOT && !logic.m_camp.resting) {
      const Walker& walker = session.walker ();
      return Angler { walker.position (), walker.heading () };
    }
    if (logic.m_mode == M_BOAT) {
      const mov::Boat& boat = session.boat ();
      return Angler { boat.helm_seat (),
                      Quaternion::rotate (boat.forward (),
                                          Vec3 (0, 1, 0),
                                          -logic.m_look_yaw * u::rad) };
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
    if (session.logic ().m_mode == M_BOAT) {
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

  bool nothing_else_at_hand (const GameSession& session,
                             const Surroundings& around) {
    return !mushroom_at_hand (session, around) &&
           !egg_at_hand (session, around) &&
           supper_at_hand (session, around) == HudState::Cook::none &&
           !beside_hammock (session);
  }

  void tend_by_hand (GameSession& session,
                     const Surroundings& around,
                     InputFrame& input,
                     seconds_t dt) {
    pass_time (session, around, dt);
    go_fishing (session, around, input, dt);
    cook_supper (session, around, input);
    tend_camp (session, around, input, dt);
    reach_for_mushroom (session, around, input);
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
        moving = (subject - *logic.m_last_subject) * (1.0f / dt);
      logic.m_last_subject = subject;
      if (length2 (moving) > 60.0f * 60.0f)
        moving = Vec3 ();
      presences.push_back ({ .position = subject,
                             .velocity = moving,
                             .machine = logic.m_mode != M_FOOT });
      if (around.companion && around.companion->walker)
        presences.push_back (
          { .position = around.companion->walker->position,
            .velocity = around.companion->walker->velocity });
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
