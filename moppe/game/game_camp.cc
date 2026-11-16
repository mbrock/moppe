// What the walker does by hand: picking mushrooms, the camp's hammock and
// fire, supper in the pan, and fishing from the bank or the boat.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    void MoppeGame::reach_for_mushroom (const InputFrame& input) {
      if (logic ().m_mode != M_FOOT)
        return;
      const Vec3 feet = session ().walker ().position ();
      // F still remounts the bike when it is close enough; otherwise it
      // picks too, for the touch screen's single action button.
      const bool bike_near =
        length2 (feet - session ().bike ().position ()) < 5.0f * 5.0f ||
        session ().mount_within_reach () != GameSession::Mount::none ||
        session ().hauling ();
      if (!input.deploy_glider && !(input.toggle_mount && !bike_near))
        return;
      const std::optional<std::uint32_t> found =
        m_mushrooms.within_reach (feet, session ().walker ().heading ());
      if (!found) {
        // Into a nest box for an egg, as many as the basket takes.
        const std::optional<std::size_t> nest = egg_at_hand ();
        Basket& basket = logic ().m_basket;
        if (!nest || basket.eggs >= basket_holds_eggs)
          return;
        --logic ().m_nests[*nest].eggs;
        ++basket.eggs;
        basket.last_reach_time = logic ().m_total_time;
        basket.last_egg = true;
        basket.last_refused = false;
        return;
      }
      const MushroomSite& site = m_mushrooms.site (*found);
      m_mushrooms.follow (logic ().m_basket);
      Basket& basket = logic ().m_basket;
      basket.last_reach_time = logic ().m_total_time;
      basket.last_egg = false;
      basket.last_kind = site.kind;
      basket.last_refused = !mushroom_edible (site.kind);
      if (basket.last_refused)
        return;
      basket.picked.push_back (*found);
      ++basket.count[static_cast<int> (site.kind)];
      m_mushrooms.follow (basket);
    }

    void MoppeGame::seek_hammock_site () {
      const Vec3 feet = session ().walker ().position ();
      if (m_hammock_sought &&
          length2 (feet - m_hammock_sought_from) < 0.3f * 0.3f)
        return;
      m_hammock_sought = true;
      m_hammock_sought_from = feet;
      const std::vector<mov::Trunk> near = m_trunk_field.gather (feet, 8.0f);
      m_hammock_at_hand = hammock_site_at (near, feet, ground_reader ());
    }

    bool MoppeGame::beside_hammock () const {
      const Camp& camp = logic ().m_camp;
      return camp.hung &&
             hammock_distance (camp.hammock, session ().walker ().position ()) <
               1.5f;
    }

    std::optional<Vec3> MoppeGame::hearth_ahead () const {
      const Walker& walker = session ().walker ();
      Vec3 ahead = walker.heading ();
      ahead[1] = 0.0f;
      if (length2 (ahead) < 1e-6f)
        return std::nullopt;
      ahead = normalized (ahead);
      Vec3 hearth = walker.position () + ahead * 1.3f;
      hearth[1] = ground_height (hearth);
      if (hearth[1] < world ().water_level.numerical_value_in (u::m) + 0.2f)
        return std::nullopt;
      const Vec3 side (ahead[2], 0.0f, -ahead[0]);
      for (const Vec3& off :
           { ahead * 0.5f, ahead * -0.5f, side * 0.5f, side * -0.5f })
        if (std::fabs (ground_height (hearth + off) - hearth[1]) > 0.22f)
          return std::nullopt;
      return hearth;
    }

    std::optional<Vec3> MoppeGame::fire_at_hand () const {
      if (logic ().m_mode != M_FOOT || logic ().m_camp.resting)
        return std::nullopt;
      const Vec3 feet = session ().walker ().position ();
      const Camp& camp = logic ().m_camp;
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
      consider (camp.hearth, fire_burn (camp, logic ().m_total_time));
      if (m_companion && m_companion->hearth_laid)
        consider (m_companion->hearth, m_companion->fire_burn);
      for (std::size_t i = 0;
           i < m_places.sanctuaries.size () && i < m_beacon_burn.size ();
           ++i)
        consider (m_places.sanctuaries[i].hearth, m_beacon_burn[i]);
      return found;
    }

    bool MoppeGame::beside_pan () const {
      const Pan& pan = logic ().m_camp.pan;
      if (!pan.on || logic ().m_mode != M_FOOT || logic ().m_camp.resting)
        return false;
      Vec3 to = session ().walker ().position () - pan.place;
      to[1] = 0.0f;
      return length2 (to) < pan_reach * pan_reach;
    }

    HudState::Cook MoppeGame::supper_at_hand () const {
      const Camp& camp = logic ().m_camp;
      const Basket& basket = logic ().m_basket;
      if (beside_pan ()) {
        const float cooked = pan_cooked (camp.pan, logic ().m_total_time);
        return cooked < 1.0f   ? HudState::Cook::frying
               : cooked > 1.4f ? HudState::Cook::eat_burnt
                               : HudState::Cook::eat;
      }
      if (!camp.pan.on &&
          (basket.used < basket.picked.size () || basket.eggs > 0 ||
           logic ().m_fishing.creel.kept () > 0) &&
          fire_at_hand ())
        return HudState::Cook::fry;
      return HudState::Cook::none;
    }

    void MoppeGame::cook_supper (InputFrame& input) {
      Camp& camp = logic ().m_camp;
      Basket& basket = logic ().m_basket;
      const double now = logic ().m_total_time;
      if (logic ().m_mode == M_FOOT && input.deploy_glider &&
          !m_mushrooms
             .within_reach (session ().walker ().position (),
                            session ().walker ().heading ())
             .has_value ()) {
        switch (supper_at_hand ()) {
        case HudState::Cook::fry: {
          Pan pan =
            pan_set_on (*fire_at_hand (), session ().walker ().position ());
          pan.since = now;
          pan.next_steam = now + 1.0;
          // A fish first, if there is one, and mushrooms beside it.
          Creel& creel = logic ().m_fishing.creel;
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
            if (index < m_mushrooms.mushroom_count ())
              --basket.count[static_cast<int> (m_mushrooms.site (index).kind)];
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
          logic ().m_health = std::min (
            100.0f,
            logic ().m_health +
              (camp.ate_cooked > 1.4f ? 5.0f : 20.0f) *
                (camp.ate_count + camp.ate_eggs + (camp.ate_fish ? 2 : 0)));
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
      pan.next_steam = now + (cooked > 1.2f ? 0.25 : 0.45);
      const float shade =
        cooked > 1.2f ? 0.18f : 0.55f + 0.35f * m_sky.daylight;
      Dust::Style steam;
      steam.size = 0.16f * u::m;
      steam.lifetime = 1.6f * u::s;
      steam.downward_acceleration =
        -0.5f * isq::acceleration[u::m / pow<2> (u::s)];
      steam.spread = 0.25f * one;
      session ().dust ().emit (moppe::position (pan.place + Vec3 (0, 0.08f, 0)),
                               velocity (Vec3 (0, 0.5f, 0)),
                               1,
                               DisplayColor (shade, shade, shade),
                               steam);
    }

    std::optional<MoppeGame::Angler> MoppeGame::angler () const {
      if (logic ().m_mode == M_FOOT && !logic ().m_camp.resting) {
        const Walker& walker = session ().walker ();
        return Angler { walker.position (), walker.heading () };
      }
      if (logic ().m_mode == M_BOAT) {
        const mov::Boat& boat = session ().boat ();
        return Angler { boat.helm_seat (),
                        Quaternion::rotate (boat.forward (),
                                            Vec3 (0, 1, 0),
                                            -logic ().m_look_yaw * u::rad) };
      }
      return std::nullopt;
    }

    Vec3 MoppeGame::current_at (const Vec3& at) const {
      Vec3 current = spatial::sample<terrain::water_velocity> (
                       generated_world ().water_surface (),
                       moppe::position (Vec3 (at[0], 0.0f, at[2])))
                       .numerical_value_in (u::m / u::s);
      current[1] = 0.0f;
      return current;
    }

    bool MoppeGame::boat_drifting () const {
      const mov::Boat& boat = session ().boat ();
      Vec3 way = boat.velocity () - current_at (boat.position ());
      way[1] = 0.0f;
      return boat.afloat () && std::abs (boat.state ().drive) < 0.1f &&
             length2 (way) < 0.8f * 0.8f;
    }

    std::optional<MoppeGame::FishingWater> MoppeGame::water_ahead () const {
      const std::optional<Angler> from = angler ();
      if (!from)
        return std::nullopt;
      Vec3 ahead = from->ahead;
      ahead[1] = 0.0f;
      if (length2 (ahead) < 1e-6f)
        return std::nullopt;
      ahead = normalized (ahead);
      if (logic ().m_mode == M_BOAT) {
        Vec3 bow = session ().boat ().forward ();
        bow[1] = 0.0f;
        if (length2 (bow) < 1e-6f || dot (ahead, normalized (bow)) < -0.17f)
          return std::nullopt;
      }
      const terrain::WaterSheets& sheets = generated_world ().water_surface ();
      const float sea = world ().water_level.numerical_value_in (u::m);
      for (float reach = longest_cast; reach >= shortest_cast; reach -= 0.5f) {
        Vec3 at = from->at + ahead * reach;
        const float ground = ground_height (at);
        const float level = still_water_at (world (), surface (), &sheets, at)
                              .numerical_value_in (u::m);
        if (level - ground < fishing_depth)
          continue;
        at[1] = level;
        FishingWater found { .aim = at, .upland = level > sea + 150.0f };
        const position_t where = moppe::position (Vec3 (at[0], 0.0f, at[2]));
        const float sheet = terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (sheets, where));
        if (sheet > ground && sheet > sea + 0.05f)
          found.waters =
            length (current_at (at)) > 0.15f ? Waters::running : Waters::still;
        else
          found.waters = Waters::sea;
        return found;
      }
      return std::nullopt;
    }

    bool MoppeGame::nothing_else_at_hand () const {
      const Walker& walker = session ().walker ();
      return !m_mushrooms.within_reach (walker.position (), walker.heading ())
                .has_value () &&
             !egg_at_hand () && supper_at_hand () == HudState::Cook::none &&
             !beside_hammock ();
    }

    void MoppeGame::splash (const Vec3& at, int drops, float vigour) {
      const float shade = 0.25f + 0.65f * m_sky.daylight;
      Dust::Style spray;
      spray.size = 0.07f * u::m;
      spray.lifetime = 0.7f * u::s;
      spray.downward_acceleration =
        6.0f * isq::acceleration[u::m / pow<2> (u::s)];
      spray.spread = (0.5f + 0.5f * vigour) * one;
      spray.flake = true;
      session ().dust ().emit (moppe::position (at + Vec3 (0, 0.03f, 0)),
                               velocity (Vec3 (0, 1.2f + 1.4f * vigour, 0)),
                               drops,
                               DisplayColor (shade, shade * 1.02f, shade),
                               spray);
    }

    void MoppeGame::go_fishing (InputFrame& input, float dt) {
      Fishing& fishing = logic ().m_fishing;
      Line& line = fishing.line;
      const double now = logic ().m_total_time;
      const std::optional<Angler> from = angler ();
      const bool afloat = logic ().m_mode == M_BOAT;
      if (!from || (line_out (line) && line.from_boat != afloat)) {
        line.phase = Line::Phase::idle;
        return;
      }
      if (!line_out (line)) {
        if (!input.deploy_glider ||
            !(afloat ? boat_drifting () : nothing_else_at_hand ()))
          return;
        if (const std::optional<FishingWater> water = water_ahead ()) {
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
        const mov::Boat& boat = session ().boat ();
        Vec3 way = boat.velocity () - current_at (boat.position ());
        way[1] = 0.0f;
        Vec3 out = line.bobber - from->at;
        out[1] = 0.0f;
        gone = length2 (way) > 1.5f * 1.5f ||
               length2 (out) > (longest_cast + 4.0f) * (longest_cast + 4.0f) ||
               (std::abs (input_value (input.drive)) > 0.1f &&
                (line.phase == Line::Phase::waiting ||
                 line.phase == Line::Phase::biting));
      } else {
        Vec3 moved = from->at - line.stand;
        moved[1] = 0.0f;
        gone = length2 (moved) > fishing_leash * fishing_leash;
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

      const terrain::WaterSheets& sheets = generated_world ().water_surface ();
      const auto level_at = [&] (const Vec3& at) {
        return water_surface_at (
                 world (), surface (), &sheets, at, logic ().m_total_time)
          .numerical_value_in (u::m);
      };
      // A river carries the float downstream while there is water under
      // it and line to give: held back by a line to the bank, but along
      // with the boat it is cast from, which the river carries too.
      if (line.phase == Line::Phase::waiting) {
        const float carried = line.from_boat ? 0.9f : 0.45f;
        Vec3 next = line.bobber + current_at (line.bobber) * (carried * dt);
        Vec3 out = next - line.stand;
        out[1] = 0.0f;
        const float level = level_at (next);
        if (level - ground_height (next) > 0.2f &&
            length (out) < longest_cast + 2.5f) {
          next[1] = level;
          line.bobber = next;
        }
      }

      // The water answers: where the float lands, where it is taken
      // under, about a fish fought in, and where it leaves the water.
      if (before == Line::Phase::casting && line.phase == Line::Phase::waiting)
        splash (line.bobber, 4, 0.0f);
      if (before == Line::Phase::waiting && line.phase == Line::Phase::biting)
        splash (line.bobber, 3, 0.2f);
      if (line.phase == Line::Phase::playing && now >= line.next_splash) {
        line.next_splash = now + 0.18;
        splash (fish_in_water (line, now), 2, 0.5f);
      }
      if (before == Line::Phase::playing && line.phase == Line::Phase::landed)
        splash (line.bobber, 8, 1.0f);
    }

    void MoppeGame::tend_camp (InputFrame& input, float dt) {
      Camp& camp = logic ().m_camp;
      const double now = logic ().m_total_time;
      float haste = 1.0f;
      float push = 0.0f;
      if (logic ().m_mode != M_FOOT) {
        if (camp.resting) {
          camp.resting = false;
          camp.rest_time = now;
        }
      } else if (camp.resting) {
        const bool rise = input.deploy_glider || input.toggle_mount ||
                          input_value (input.boost) > 0.5f;
        if (rise && now - camp.rest_time > 0.8) {
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
              1.0f + (RESTING_HASTE - 1.0f) *
                       smoothstep (
                         2.5f, 7.0f, static_cast<float> (now - camp.rest_time));
        }
        const bool cycle = input.cycle_camera;
        input = {};
        input.cycle_camera = cycle;
      } else {
        seek_hammock_site ();
        const Walker& walker = session ().walker ();
        const Vec3 feet = walker.position ();
        if (input.hang_hammock) {
          if (beside_hammock ()) {
            camp.hung = false;
          } else if (m_hammock_at_hand) {
            camp.hung = true;
            camp.hammock = *m_hammock_at_hand;
            camp.swing = 0.0f;
            camp.swing_rate = 0.5f;
          }
        }
        if (const std::optional<std::size_t> beacon = beacon_at_hand ();
            input.light_fire && beacon) {
          // A sanctuary's fire is lit on its platform, and shows from
          // the summits that see it.
          Beacon& b = logic ().m_beacons[*beacon];
          if (b.burning () || !their_beacon (*beacon))
            (b.burning () ? b.out : b.lit) = now;
          std::cerr << "moppe: places: the beacon on sanctuary " << *beacon
                    << (b.burning () ? " lit, seen from " : " put out")
                    << (b.burning ()
                          ? std::to_string (
                              m_places.sanctuaries[*beacon].seen_from.size ())
                          : std::string ())
                    << (b.burning () ? " others" : "") << std::endl;
        } else if (input.light_fire) {
          Vec3 to_fire = feet - camp.hearth;
          to_fire[1] = 0.0f;
          if (camp.fire && length2 (to_fire) < 3.0f * 3.0f) {
            camp.fire = false;
            camp.fire_time = now;
          } else if (const std::optional<Vec3> ring =
                       hearth_at_hand (m_places, feet)) {
            // An old fire ring takes the fire.
            camp.fire = true;
            camp.hearth = *ring;
            camp.fire_time = now;
            camp.next_spark = camp.next_smoke = now;
          } else if (const std::optional<Vec3> hearth = hearth_ahead ()) {
            camp.fire = true;
            camp.hearth = *hearth;
            camp.fire_time = now;
            camp.next_spark = camp.next_smoke = now;
          }
        }
        const bool picking =
          m_mushrooms.within_reach (feet, walker.heading ()).has_value ();
        if (input.deploy_glider && !picking && beside_hammock ()) {
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
      logic ().m_day_haste +=
        (haste - logic ().m_day_haste) * std::min (1.0f, 1.5f * dt);
      feed_fire ();
      tend_beacons ();
    }

    void MoppeGame::feed_fire () {
      Camp& camp = logic ().m_camp;
      const double now = logic ().m_total_time;
      const float burn = fire_burn (camp, now);
      const bool smouldering = !camp.fire && now - camp.fire_time < 8.0;
      if (burn <= 0.02f && !smouldering)
        return;
      std::uniform_real_distribution<float> unit (0.0f, 1.0f);
      std::mt19937& rng = logic ().m_fx_rng;
      const auto scatter = [&] (float reach) {
        return (unit (rng) - 0.5f) * 2.0f * reach;
      };
      if (burn > 0.3f && now >= camp.next_spark) {
        camp.next_spark = now + 0.10 + 0.55 * unit (rng);
        Dust::Style spark;
        spark.size = 0.05f * u::m;
        spark.lifetime = (1.2f + unit (rng)) * u::s;
        spark.downward_acceleration =
          -0.6f * isq::acceleration[u::m / pow<2> (u::s)];
        spark.spread = 0.5f * one;
        spark.additive = true;
        session ().dust ().emit (
          moppe::position (camp.hearth +
                           Vec3 (scatter (0.12f), 0.4f, scatter (0.12f))),
          velocity (
            Vec3 (scatter (0.5f), 1.3f + 1.4f * unit (rng), scatter (0.5f))),
          unit (rng) < 0.3f ? 2 : 1,
          DisplayColor (1.0f, 0.62f, 0.22f),
          spark);
      }
      if (now >= camp.next_smoke) {
        camp.next_smoke = now + (smouldering ? 0.22 : 0.5);
        // Smoke shows by the light there is to see it in.
        const float shade = 0.06f + 0.36f * m_sky.daylight;
        Dust::Style smoke;
        smoke.size = 0.5f * u::m;
        smoke.lifetime = 4.0f * u::s;
        smoke.downward_acceleration =
          -0.35f * isq::acceleration[u::m / pow<2> (u::s)];
        smoke.spread = 0.45f * one;
        session ().dust ().emit (
          moppe::position (camp.hearth + Vec3 (0, 0.9f, 0)),
          velocity (Vec3 (0.35f, 0.7f, 0.12f)),
          1,
          DisplayColor (shade * 1.06f, shade, shade * 0.95f),
          smoke);
      }
    }

    void MoppeGame::draw_camp (render::DrawList& dl) {
      const Camp& camp = logic ().m_camp;
      const double now = logic ().m_total_time;
      if (camp.hung)
        draw_hammock (dl, camp.hammock, hammock_load (camp, now), camp.swing);
      if (camp.fire_time > -50.0)
        draw_hearth (dl, camp.hearth, fire_burn (camp, now));
      if (camp.pan.on) {
        m_pan_contents.clear ();
        for (int i = 0; i < camp.pan.count; ++i)
          if (camp.pan.mushrooms[i] < m_mushrooms.mushroom_count ())
            m_pan_contents.push_back (m_mushrooms.site (camp.pan.mushrooms[i]));
        draw_pan (dl,
                  camp.pan.place,
                  camp.pan.yaw,
                  m_pan_contents,
                  pan_cooked (camp.pan, now),
                  camp.pan.has_fish ? &camp.pan.fish : nullptr,
                  camp.pan.eggs);
      }
    }

    bool MoppeGame::lying_down () const {
      const Camp& camp = logic ().m_camp;
      return camp.hung && logic ().m_mode == M_FOOT &&
             hammock_load (camp, logic ().m_total_time) > 0.5f;
    }

    void MoppeGame::draw_sleeper (render::DrawList& dl,
                                  bool through_own_eyes,
                                  float time) {
      const Camp& camp = logic ().m_camp;
      if (!through_own_eyes)
        figure::draw (
          dl,
          pose_resting (
            camp.hammock,
            camp.swing,
            resting_gaze (camp.hammock, camp.gaze_yaw, camp.gaze_pitch),
            time),
          m_outfit);
      // The basket waits on the ground under the foot of the hammock.
      Vec3 floor = hammock_middle (camp.hammock) +
                   hammock_across (camp.hammock) * 0.75f -
                   hammock_along (camp.hammock) * 0.5f;
      floor[1] = ground_height (floor);
      render_basket_set_down (
        dl, floor, hammock_along (camp.hammock), m_basket_contents);
    }
  }
}
