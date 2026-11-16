// The other player (companion.hh): finding them, the snapshots exchanged
// with them, their machines as ours meet them, riding along in their jeep or
// boat, and drawing them.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    void MoppeGame::seek_company (const std::string& address) {
      CompanionLink::Config config { .address = address };
      if (const char* port = moppe::environment ("MOPPE_MULTIPLAYER_PORT"))
        config.port = static_cast<std::uint16_t> (::atoi (port));
      std::string error;
      m_link = CompanionLink::open (config, error);
      if (!m_link) {
        std::cerr << "moppe: multiplayer: " << error << std::endl;
        return;
      }
      std::cerr << "moppe: multiplayer: listening on port " << m_link->port ()
                << (address.empty () ? ", calling on the local network"
                                     : ", calling " + address)
                << std::endl;
    }

    std::uint32_t MoppeGame::this_world () const {
      return world_tag (static_cast<std::uint32_t> (recipe ().seed ().value),
                        recipe ().resolution (),
                        static_cast<int> (recipe ().generation_profile ()));
    }

    void MoppeGame::forget_companion () {
      m_track.clear ();
      m_companion.reset ();
      m_companion_picks.clear ();
      m_companion_hours = -1.0;
      m_companion_met = false;
      m_companion_elsewhere = false;
    }

    void MoppeGame::notice (const char* words) {
      m_notice = words;
      m_notice_at = m_link ? m_link->now () : 0.0;
      std::cerr << "moppe: multiplayer: " << words << std::endl;
    }

    CompanionSnapshot MoppeGame::my_snapshot () const {
      const GameLogicState& state = logic ();
      CompanionSnapshot mine;
      mine.world = this_world ();
      mine.day_hours = m_day.turning ? state.m_day_hours : -1.0;
      mine.mode = state.m_mode;
      mine.bike = vehicle_pose (session ().bike ());
      mine.jeep = jeep_pose (session ().jeep ());
      mine.jeep_velocity = session ().jeep ().velocity ();
      mine.helicopter = helicopter_pose (session ().helicopter ());
      mine.tractor = tractor_pose (session ().tractor ());
      mine.caddy = caddy_pose (session ().caddy ());
      mine.mountain_bike = vehicle_pose (session ().mountain_bike ());
      mine.long_john = vehicle_pose (session ().long_john ());
      mine.boat = boat_pose (session ().boat ());
      mine.boat_velocity = session ().boat ().velocity ();
      mine.in_boat = state.m_mode == M_PASSENGER && state.m_seat_boat;
      mine.jet_ski = jet_ski_pose (session ().jet_ski ());
      mine.jet_ski_velocity = session ().jet_ski ().velocity ();
      if (state.m_mode == M_GLIDER)
        mine.glider = glider_pose (session ().glider ());
      if (state.m_mode == M_FOOT)
        mine.walker = walker_pose (session ().walker (), state.m_look_pitch);
      const Camp& camp = state.m_camp;
      mine.hung = camp.hung;
      mine.hammock = camp.hammock;
      mine.hammock_load = hammock_load (camp, state.m_total_time);
      mine.swing = camp.swing;
      mine.gaze_yaw = camp.gaze_yaw;
      mine.gaze_pitch = camp.gaze_pitch;
      mine.hearth_laid = camp.fire_time > -50.0;
      mine.hearth = camp.hearth;
      mine.fire_burn = fire_burn (camp, state.m_total_time);
      mine.beacons = 0;
      for (std::size_t i = 0; i < state.m_beacons.size () && i < 32; ++i)
        if (state.m_beacons[i].burning ())
          mine.beacons |= 1u << i;
      mine.pan_on = camp.pan.on;
      mine.pan_place = camp.pan.place;
      mine.pan_yaw = camp.pan.yaw;
      mine.pan_cooked = pan_cooked (camp.pan, state.m_total_time);
      mine.pan_count = static_cast<std::uint8_t> (camp.pan.count);
      std::copy (std::begin (camp.pan.mushrooms),
                 std::end (camp.pan.mushrooms),
                 std::begin (mine.pan_mushrooms));
      mine.pick_used = static_cast<std::uint32_t> (state.m_basket.used);
      return mine;
    }

    void MoppeGame::hear_of_picks (const CompanionSnapshot& theirs) {
      if (theirs.pick_count < m_companion_picks.size ())
        m_companion_picks.clear ();
      constexpr std::uint32_t unknown = UINT32_MAX;
      // No one has picked more mushrooms than grow.
      if (theirs.pick_count > m_mushrooms.mushroom_count ())
        return;
      m_companion_picks.resize (theirs.pick_count, unknown);
      for (std::size_t i = 0; i < theirs.picks.size (); ++i) {
        const std::size_t ordinal = theirs.picks_from + i;
        const std::uint32_t index = theirs.picks[i];
        if (ordinal >= m_companion_picks.size () ||
            index >= m_mushrooms.mushroom_count ())
          continue;
        m_companion_picks[ordinal] = index;
        m_mushrooms.take (index);
      }
    }

    void MoppeGame::make_room (const CompanionSnapshot& theirs) {
      if (m_spectator || logic ().m_mode != M_FOOT)
        return;
      const double here = m_link->now ();
      if (here > theirs.sent ||
          (here == theirs.sent && m_link->player () < theirs.player))
        return;
      mov::Vehicle& bike = session ().bike ();
      Vec3 apart = bike.position () - theirs.bike.position;
      apart[1] = 0.0f;
      if (length2 (apart) > 2.5f * 2.5f)
        return;
      Vec3 heading = bike.orientation ();
      heading[1] = 0.0f;
      heading =
        length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
      const Vec3 side (heading[2], 0, -heading[0]);
      const bool beside = length2 (session ().walker ().position () -
                                   bike.position ()) < 6.0f * 6.0f;
      // Far enough that this jeep, parked beyond the bike, clears
      // theirs.
      Vec3 place = bike.position () - side * 7.0f;
      place[1] = ground_height (place) + 1.2f;
      bike.reset (place);
      bike.set_heading (heading);
      m_park_aside = place - m_spawn_position;
      m_park_aside[1] = 0.0f;
      m_spawn_position = place;
      park_jeep ();
      park_tractor ();
      park_mountain_bike ();
      park_long_john ();
      park_helicopter (theirs.helicopter.position);
      if (logic ().m_mode != M_BOAT)
        park_boat ();
      if (logic ().m_mode != M_JET_SKI)
        park_jet_ski ();
      park_caddy ();
      if (!beside)
        return;
      session ().walker ().spawn (moppe::position (place + side * 1.8f),
                                  heading);
      logic ().m_fp_eye = session ().walker ().eye_position ();
      if (m_opening.active ())
        m_opening.replace_arrival (arrival ());
    }

    Outfit MoppeGame::their_outfit () const {
      return m_outfit == Outfit::blue_jacket ? Outfit::green_jacket
                                             : Outfit::blue_jacket;
    }

    Vec3 MoppeGame::companion_seat (const CompanionSnapshot& theirs) {
      const JeepPose& jeep = theirs.jeep;
      const Vec3 left = normalized (cross (jeep.up, jeep.forward));
      return jeep.position - left * mov::Jeep::seat[0] +
             jeep.up * mov::Jeep::seat[1] + jeep.forward * mov::Jeep::seat[2];
    }

    Vec3 MoppeGame::companion_boat_seat (const CompanionSnapshot& theirs) {
      const BoatPose& boat = theirs.boat;
      const Vec3 left = normalized (cross (boat.up, boat.forward));
      return boat.position + left * mov::Boat::fore[0] +
             boat.up * mov::Boat::fore[1] + boat.forward * mov::Boat::fore[2];
    }

    MoppeGame::Seat MoppeGame::seat_within_reach () const {
      if (!m_companion || m_spectator || logic ().m_mode != M_FOOT)
        return Seat::none;
      const Vec3 feet = session ().walker ().position ();
      const float mine =
        std::min ({ length2 (feet - session ().jeep ().position ()),
                    length2 (feet - session ().bike ().position ()),
                    length2 (feet - session ().boat ().position ()) });
      const float jeep = length2 (feet - m_companion->jeep.position);
      float boat = 1e30f;
      if (m_companion->mode == M_BOAT)
        boat = length2 (feet - companion_boat_seat (*m_companion));
      if (boat < 4.0f * 4.0f && boat < mine && boat < jeep)
        return Seat::boat;
      if (jeep < 4.5f * 4.5f && jeep < mine)
        return Seat::jeep;
      return Seat::none;
    }

    bool MoppeGame::can_ride_along () const {
      return seat_within_reach () != Seat::none;
    }

    void MoppeGame::ride_along (InputFrame& input) {
      GameLogicState& state = logic ();
      session ().boat ().set_passenger (m_companion &&
                                        m_companion->mode == M_PASSENGER &&
                                        m_companion->in_boat);
      if (state.m_mode == M_PASSENGER) {
        if (!m_companion || input.toggle_mount) {
          // Out on the passenger's side, the right; over the boat's
          // gunwale, a step from it.
          const Vec3 left =
            normalized (cross (state.m_seat_up, state.m_seat_heading));
          session ().walker ().spawn (
            moppe::position (state.m_seat_position -
                             left * (state.m_seat_boat ? 1.1f : 1.4f)),
            state.m_seat_heading);
          session ().set_mode (M_FOOT);
          state.m_seat_boat = false;
          input.toggle_mount = false;
          return;
        }
      } else if (input.toggle_mount && can_ride_along ()) {
        state.m_seat_boat = seat_within_reach () == Seat::boat;
        session ().clear_controls ();
        session ().set_mode (M_PASSENGER);
        input.toggle_mount = false;
      } else if (m_companion && !m_rode_along &&
                 moppe::environment ("MOPPE_RIDE_ALONG")) {
        // For captures: take the seat as soon as there is one, from
        // wherever this player stands.
        m_rode_along = true;
        state.m_seat_boat = m_companion->mode == M_BOAT;
        session ().clear_controls ();
        session ().set_mode (M_PASSENGER);
      }
      if (state.m_mode != M_PASSENGER)
        return;
      if (state.m_seat_boat) {
        state.m_seat_position = companion_boat_seat (*m_companion);
        state.m_seat_heading = m_companion->boat.forward;
        state.m_seat_up = m_companion->boat.up;
        state.m_seat_velocity = m_companion->boat_velocity;
        return;
      }
      state.m_seat_position = companion_seat (*m_companion);
      state.m_seat_heading = m_companion->jeep.forward;
      state.m_seat_up = m_companion->jeep.up;
      state.m_seat_velocity = m_companion->jeep_velocity;
    }

    void MoppeGame::meet_strangers (float elapsed) {
      m_strangers.clear ();
      if (!m_companion) {
        m_strangers_were.reset ();
        return;
      }
      const CompanionSnapshot& theirs = *m_companion;
      const auto stranger = [&] (Machine machine,
                                 const Vec3& position,
                                 const Vec3& forward,
                                 const Vec3& up) {
        MachineBody body;
        body.machine = machine;
        body.position = position;
        body.forward = forward;
        body.up = up;
        body.mass = session ().machine_body (machine).mass;
        body.ours = false;
        if (machine == Machine::tractor) {
          body.towing = true;
          body.towed_position = theirs.tractor.trailer_position;
          body.towed_forward = theirs.tractor.trailer_forward;
          body.towed_up = theirs.tractor.trailer_up;
          body.towed_mass = session ().tractor ().trailer_mass ();
        }
        const std::size_t i = static_cast<std::size_t> (machine);
        if (m_strangers_were && elapsed > 0.0f) {
          const Vec3 moved = position - (*m_strangers_were)[i];
          if (length2 (moved) < 25.0f)
            body.velocity = moved * (1.0f / elapsed);
        }
        m_strangers.push_back (body);
      };
      stranger (Machine::bike,
                theirs.bike.position,
                theirs.bike.render_orientation,
                theirs.bike.render_normal);
      stranger (Machine::jeep,
                theirs.jeep.position,
                theirs.jeep.forward,
                theirs.jeep.up);
      stranger (Machine::tractor,
                theirs.tractor.position,
                theirs.tractor.forward,
                theirs.tractor.up);
      stranger (Machine::caddy,
                theirs.caddy.position,
                theirs.caddy.forward,
                theirs.caddy.up);
      stranger (Machine::helicopter,
                theirs.helicopter.position,
                theirs.helicopter.forward,
                theirs.helicopter.up);
      stranger (Machine::mountain_bike,
                theirs.mountain_bike.position,
                theirs.mountain_bike.render_orientation,
                theirs.mountain_bike.render_normal);
      stranger (Machine::boat,
                theirs.boat.position,
                theirs.boat.forward,
                theirs.boat.up);
      stranger (Machine::long_john,
                theirs.long_john.position,
                theirs.long_john.render_orientation,
                theirs.long_john.render_normal);
      stranger (Machine::jet_ski,
                theirs.jet_ski.position,
                theirs.jet_ski.forward,
                theirs.jet_ski.up);
      std::array<Vec3, machine_count> were;
      for (const MachineBody& body : m_strangers)
        were[static_cast<std::size_t> (body.machine)] = body.position;
      m_strangers_were = were;
      m_strangers[static_cast<std::size_t> (Machine::jeep)].velocity =
        theirs.jeep_velocity;
      m_strangers[static_cast<std::size_t> (Machine::boat)].velocity =
        theirs.boat_velocity;
      m_strangers[static_cast<std::size_t> (Machine::jet_ski)].velocity =
        theirs.jet_ski_velocity;
    }

    void MoppeGame::keep_company (float elapsed) {
      if (!m_link)
        return;
      m_arrived.clear ();
      m_link->receive (m_arrived);
      const double now = m_link->now ();
      // The poses told are the simulation's, which moves on in whole
      // steps, now two to a frame and now three. They are stamped by a
      // clock that moves with it, so that the other machine sees even
      // motion however unevenly the frames fall.
      m_pose_clock =
        m_pose_heard < 0.0
          ? now
          : m_pose_clock + (m_stepped > 0.0 ? m_stepped : now - m_pose_heard);
      m_pose_heard = now;
      m_stepped = 0.0;
      const bool was_present = m_track.present (now);
      for (CompanionSnapshot& theirs : m_arrived) {
        if (theirs.world != this_world ()) {
          if (!m_companion_elsewhere) {
            forget_companion ();
            m_companion_elsewhere = true;
            notice ("Someone is playing, but in another world");
            std::cerr << "moppe: multiplayer: launch both games with the "
                         "same --seed and terrain options"
                      << std::endl;
          }
          continue;
        }
        m_companion_elsewhere = false;
        if (!m_track.present (now))
          m_companion_picks.clear ();
        hear_of_picks (theirs);
        m_companion_hours = theirs.day_hours;
        if (!m_companion_met) {
          m_companion_met = true;
          // So that the two can be told apart, and each machine agrees
          // on who is who, the player with the greater number changes
          // into the green jacket.
          m_outfit = m_link->player () > theirs.player ? Outfit::green_jacket
                                                       : Outfit::blue_jacket;
          make_room (theirs);
        }
        m_track.push (std::move (theirs), now);
      }
      const bool present = m_track.present (now);
      if (present && !was_present) {
        notice ("Your companion has arrived");
        std::cerr << "moppe: multiplayer: companion at "
                  << m_link->companion_address () << std::endl;
      } else if (!present && was_present) {
        notice ("Your companion has gone");
        m_companion_hours = -1.0;
        m_companion_met = false;
      }
      m_companion = m_track.advance (now, elapsed);
      meet_strangers (elapsed);

      // A spectator has no one to show.
      if (m_spectator || !m_link->due ())
        return;
      CompanionSnapshot mine = my_snapshot ();
      const std::vector<std::uint32_t>& picked = logic ().m_basket.picked;
      mine.pick_count = static_cast<std::uint32_t> (picked.size ());
      mine.picks_from =
        companion_pick_window_start (picked.size (), m_snapshots_sent++);
      for (std::size_t i = mine.picks_from;
           i < picked.size () && mine.picks.size () < companion_pick_window;
           ++i)
        mine.picks.push_back (picked[i]);
      m_link->send (std::move (mine), m_pose_clock);
    }

    void MoppeGame::draw_companion (render::Renderer& r,
                                    render::DrawList& dl,
                                    float time) {
      if (!m_companion)
        return;
      const CompanionSnapshot& theirs = *m_companion;
      m_blob.draw (dl, surface (), theirs.bike.position, 2.2f);
      render_vehicle (
        r, dl, theirs.bike, theirs.mode == M_BIKE, 0x3000, their_outfit ());
      m_blob.draw (dl, surface (), theirs.jeep.position, 3.4f);
      // This player beside them, unless seeing through their own eyes.
      render_jeep (r,
                   dl,
                   theirs.jeep,
                   { .driver = theirs.mode == M_JEEP,
                     .passenger = logic ().m_mode == M_PASSENGER &&
                                  !logic ().m_seat_boat &&
                                  logic ().m_cam_mode != CAM_HELMET,
                     .driver_outfit = their_outfit (),
                     .passenger_outfit = m_outfit },
                   0x6000);
      m_blob.draw (dl, surface (), theirs.helicopter.position, 4.5f);
      render_helicopter (r, theirs.helicopter, 0x7000);
      m_blob.draw (dl, surface (), theirs.tractor.position, 2.8f);
      m_blob.draw (dl, surface (), theirs.tractor.trailer_position, 2.4f);
      render_tractor (r,
                      dl,
                      theirs.tractor,
                      theirs.mode == M_TRACTOR
                        ? std::optional<Outfit> (their_outfit ())
                        : std::nullopt,
                      0x9000);
      m_blob.draw (dl, surface (), theirs.caddy.position, 3.6f);
      render_caddy (
        r,
        dl,
        theirs.caddy,
        { .driver = theirs.mode == M_CADDY, .driver_outfit = their_outfit () },
        0xB000);
      render_boat (r,
                   dl,
                   theirs.boat,
                   theirs.mode == M_BOAT
                     ? std::optional<Outfit> (their_outfit ())
                     : std::nullopt,
                   0xF800,
                   std::nullopt,
                   time,
                   logic ().m_mode == M_PASSENGER && logic ().m_seat_boat &&
                       logic ().m_cam_mode != CAM_HELMET
                     ? std::optional<Outfit> (m_outfit)
                     : std::nullopt);
      m_blob.draw (dl, surface (), theirs.mountain_bike.position, 1.3f);
      render_mountain_bike (r,
                            dl,
                            theirs.mountain_bike,
                            theirs.mode == M_MOUNTAIN_BIKE
                              ? std::optional<Outfit> (their_outfit ())
                              : std::nullopt,
                            0xE000);
      m_blob.draw (dl, surface (), theirs.long_john.position, 2.0f);
      render_long_john (r,
                        dl,
                        theirs.long_john,
                        theirs.mode == M_LONG_JOHN
                          ? std::optional<Outfit> (their_outfit ())
                          : std::nullopt,
                        0xE100);
      render_jet_ski (r,
                      dl,
                      theirs.jet_ski,
                      theirs.mode == M_JET_SKI
                        ? std::optional<Outfit> (their_outfit ())
                        : std::nullopt,
                      0xE200);
      if (theirs.hung)
        draw_hammock (dl, theirs.hammock, theirs.hammock_load, theirs.swing);
      if (theirs.hearth_laid)
        draw_hearth (dl, theirs.hearth, theirs.fire_burn);

      m_companion_basket.clear ();
      for (std::size_t i = theirs.pick_used; i < m_companion_picks.size (); ++i)
        if (m_companion_picks[i] < m_mushrooms.mushroom_count ())
          m_companion_basket.push_back (
            m_mushrooms.site (m_companion_picks[i]));
      if (theirs.pan_on) {
        m_pan_contents.clear ();
        for (int i = 0; i < theirs.pan_count; ++i)
          if (theirs.pan_mushrooms[i] < m_mushrooms.mushroom_count ())
            m_pan_contents.push_back (
              m_mushrooms.site (theirs.pan_mushrooms[i]));
        draw_pan (dl,
                  theirs.pan_place,
                  theirs.pan_yaw,
                  m_pan_contents,
                  theirs.pan_cooked);
      }
      if (theirs.walker && theirs.lying_down ()) {
        figure::draw (
          dl,
          pose_resting (
            theirs.hammock,
            theirs.swing,
            resting_gaze (theirs.hammock, theirs.gaze_yaw, theirs.gaze_pitch),
            time),
          their_outfit ());
        Vec3 floor = hammock_middle (theirs.hammock) +
                     hammock_across (theirs.hammock) * 0.75f -
                     hammock_along (theirs.hammock) * 0.5f;
        floor[1] = ground_height (floor);
        render_basket_set_down (
          dl, floor, hammock_along (theirs.hammock), m_companion_basket);
      } else if (theirs.walker) {
        m_blob.draw (
          dl, surface (), theirs.walker->position + Vec3 (0, 0.5f, 0), 0.8f);
        render_walker (
          dl, *theirs.walker, time, m_companion_basket, their_outfit ());
      }
      if (theirs.glider) {
        m_blob.draw (dl, surface (), theirs.glider->position, 3.4f);
        render_glider (r, dl, *theirs.glider, time, 0x4000, their_outfit ());
      }
    }

    void MoppeGame::light_companion_fire (FrameView& frame) const {
      if (!m_companion || !m_companion->hearth_laid ||
          m_companion->fire_burn <= 0.01f)
        return;
      Camp theirs;
      theirs.hearth = m_companion->hearth;
      const Vec3 lamp = fire_light_position (theirs);
      FrameLighting& lighting = frame.lighting;
      if (lighting.lamp_reach > 0.0f &&
          length2 (lighting.lamp_position - frame.camera.position) <=
            length2 (lamp - frame.camera.position))
        return;
      lighting.lamp_position = lamp;
      lighting.lamp_reach = meters_value (fire_light_reach);
      lighting.lamp_color =
        scale_display (fire_light_color (m_companion->fire_burn, lighting.time),
                       1.0f - 0.25f * lighting.daylight);
    }

    void MoppeGame::add_company_readings (HudState& state) const {
      state.can_ride_along = can_ride_along ();
      if (!m_link || !m_notice)
        return;
      state.notice = m_notice;
      state.notice_age_s = static_cast<float> (m_link->now () - m_notice_at);
    }
  }
}
