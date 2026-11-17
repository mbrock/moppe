// The other player (companion.hh): finding them, the snapshots exchanged
// with them, their machines as ours meet them, riding along in their jeep or
// boat, and drawing them.

#include <moppe/environment.hh>
#include <moppe/game/caddy_render.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/game.hh>
#include <moppe/game/glider_render.hh>
#include <moppe/game/helicopter_render.hh>
#include <moppe/game/jet_ski_render.hh>
#include <moppe/game/long_john_render.hh>
#include <moppe/game/mountain_bike_render.hh>
#include <moppe/game/tractor_render.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/walker_render.hh>

namespace moppe {
  namespace game {
    void MoppeGame::seek_company (const std::string& address) {
      CompanionLink::Config config { .address = address };
      std::string error;
      m_company.link = CompanionLink::open (config, error);
      if (!m_company.link) {
        std::cerr << "moppe: multiplayer: " << error << std::endl;
        return;
      }
      std::cerr << "moppe: multiplayer: listening on port "
                << m_company.link->port ()
                << (address.empty () ? ", calling on the local network"
                                     : ", calling " + address)
                << std::endl;
    }

    std::uint32_t MoppeGame::this_world () const {
      return world_tag (recipe_cache_identity (recipe ()));
    }

    CompanionSnapshot MoppeGame::my_snapshot () const {
      const GameLogicState& state = logic ();
      CompanionSnapshot mine;
      mine.world = this_world ();
      mine.day_hours =
        m_sky.day ().turning ? hours_since_midnight (state.m_day_hours) : -1.0;
      mine.mode = state.m_mode;
      mine.bike = vehicle_pose (session ().bike ());
      mine.jeep = car_pose (session ().jeep ());
      mine.jeep_velocity = session ().jeep ().velocity ();
      mine.helicopter = helicopter_pose (session ().helicopter ());
      mine.tractor = tractor_pose (session ().tractor ());
      mine.caddy = car_pose (session ().caddy ());
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
      mine.hearth_laid = hearth_laid (camp);
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
      if (theirs.pick_count < m_company.picks.size ())
        m_company.picks.clear ();
      constexpr std::uint32_t unknown = UINT32_MAX;
      // No one has picked more mushrooms than grow.
      if (theirs.pick_count > m_mushrooms.mushroom_count ())
        return;
      m_company.picks.resize (theirs.pick_count, unknown);
      for (std::size_t i = 0; i < theirs.picks.size (); ++i) {
        const std::size_t ordinal = theirs.picks_from + i;
        const std::uint32_t index = theirs.picks[i];
        if (ordinal >= m_company.picks.size () ||
            index >= m_mushrooms.mushroom_count ())
          continue;
        m_company.picks[ordinal] = index;
        m_mushrooms.take (index);
      }
    }

    void MoppeGame::make_room (const CompanionSnapshot& theirs) {
      if (logic ().m_mode != M_FOOT)
        return;
      const double here = m_company.link->now ();
      if (here > theirs.sent ||
          (here == theirs.sent && m_company.link->player () < theirs.player))
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
      place[1] = world_ground ().height (place) + 1.2f;
      bike.reset (place);
      bike.set_heading (heading);
      Vec3 aside = place - m_spawn_position;
      aside[1] = 0.0f;
      m_parking.set_aside (aside);
      m_spawn_position = place;
      m_parking.park_jeep (parking_lot ());
      m_parking.park_tractor (parking_lot ());
      m_parking.park_mountain_bike (parking_lot ());
      m_parking.park_long_john (parking_lot ());
      m_parking.park_helicopter (parking_lot (), theirs.helicopter.position);
      if (logic ().m_mode != M_BOAT)
        m_parking.park_boat (parking_lot ());
      if (logic ().m_mode != M_JET_SKI)
        m_parking.park_jet_ski (parking_lot ());
      m_parking.park_caddy (parking_lot ());
      if (!beside)
        return;
      session ().walker ().spawn (moppe::position (place + side * 1.8f),
                                  heading);
      logic ().m_fp_eye = session ().walker ().eye_position ();
      if (m_opening.active ())
        m_opening.replace_arrival (arrival ());
    }

    MoppeGame::Seat MoppeGame::seat_within_reach () const {
      if (!m_company.companion || logic ().m_mode != M_FOOT)
        return Seat::none;
      const Vec3 feet = session ().walker ().position ();
      const float mine =
        std::min ({ length2 (feet - session ().jeep ().position ()),
                    length2 (feet - session ().bike ().position ()),
                    length2 (feet - session ().boat ().position ()) });
      const float jeep = length2 (feet - m_company.companion->jeep.position);
      float boat = 1e30f;
      if (m_company.companion->mode == M_BOAT)
        boat = length2 (feet - Companionship::boat_seat (*m_company.companion));
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
      session ().boat ().set_passenger (
        m_company.companion && m_company.companion->mode == M_PASSENGER &&
        m_company.companion->in_boat);
      if (state.m_mode == M_PASSENGER) {
        if (!m_company.companion || input.toggle_mount) {
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
      } else if (m_company.companion && !m_company.rode_along &&
                 moppe::environment ("MOPPE_RIDE_ALONG")) {
        // For captures: take the seat as soon as there is one, from
        // wherever this player stands.
        m_company.rode_along = true;
        state.m_seat_boat = m_company.companion->mode == M_BOAT;
        session ().clear_controls ();
        session ().set_mode (M_PASSENGER);
      }
      if (state.m_mode != M_PASSENGER)
        return;
      if (state.m_seat_boat) {
        state.m_seat_position = Companionship::boat_seat (*m_company.companion);
        state.m_seat_heading = m_company.companion->boat.forward;
        state.m_seat_up = m_company.companion->boat.up;
        state.m_seat_velocity = m_company.companion->boat_velocity;
        return;
      }
      state.m_seat_position = Companionship::jeep_seat (*m_company.companion);
      state.m_seat_heading = m_company.companion->jeep.forward;
      state.m_seat_up = m_company.companion->jeep.up;
      state.m_seat_velocity = m_company.companion->jeep_velocity;
    }

    void MoppeGame::meet_strangers (seconds_t interval) {
      const float elapsed = seconds_value (interval);
      m_company.strangers.clear ();
      if (!m_company.companion) {
        m_company.strangers_were.reset ();
        return;
      }
      const CompanionSnapshot& theirs = *m_company.companion;
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
        if (m_company.strangers_were && elapsed > 0.0f) {
          const Vec3 moved = position - (*m_company.strangers_were)[i];
          if (length2 (moved) < 25.0f)
            body.velocity = moved * (1.0f / elapsed);
        }
        m_company.strangers.push_back (body);
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
      for (const MachineBody& body : m_company.strangers)
        were[static_cast<std::size_t> (body.machine)] = body.position;
      m_company.strangers_were = were;
      m_company.strangers[static_cast<std::size_t> (Machine::jeep)].velocity =
        theirs.jeep_velocity;
      m_company.strangers[static_cast<std::size_t> (Machine::boat)].velocity =
        theirs.boat_velocity;
      m_company.strangers[static_cast<std::size_t> (Machine::jet_ski)]
        .velocity = theirs.jet_ski_velocity;
    }

    void MoppeGame::keep_company (seconds_t interval) {
      const float elapsed = seconds_value (interval);
      if (!m_company.link)
        return;
      m_company.arrived.clear ();
      m_company.link->set_world (this_world ());
      m_company.link->receive (m_company.arrived);
      const double now = m_company.link->now ();
      // The poses told are the simulation's, which moves on in whole
      // steps, now two to a frame and now three. They are stamped by a
      // clock that moves with it, so that the other machine sees even
      // motion however unevenly the frames fall.
      m_company.pose_clock =
        m_company.pose_heard < 0.0
          ? now
          : m_company.pose_clock + (m_company.stepped > 0.0
                                      ? m_company.stepped
                                      : now - m_company.pose_heard);
      m_company.pose_heard = now;
      m_company.stepped = 0.0;
      const bool was_present = m_company.track.present (now);
      // Someone in another world is only told of, while no one here
      // keeps company.
      if (m_company.link->heard_elsewhere () && m_company.arrived.empty () &&
          !was_present && !m_company.elsewhere) {
        m_company.elsewhere = true;
        m_company.notice ("Someone is playing, but in another world");
        std::cerr << "moppe: multiplayer: launch both games with the "
                     "same --seed and terrain options"
                  << std::endl;
      }
      for (CompanionSnapshot& theirs : m_company.arrived) {
        m_company.elsewhere = false;
        if (!m_company.track.present (now))
          m_company.picks.clear ();
        hear_of_picks (theirs);
        m_company.hours = theirs.day_hours;
        if (!m_company.met) {
          m_company.met = true;
          // So that the two can be told apart, and each machine agrees
          // on who is who, the player with the greater number changes
          // into the green jacket.
          m_company.outfit = m_company.link->player () > theirs.player
                               ? Outfit::green_jacket
                               : Outfit::blue_jacket;
          make_room (theirs);
        }
        m_company.track.push (std::move (theirs), now);
      }
      const bool present = m_company.track.present (now);
      if (present && !was_present) {
        m_company.notice ("Your companion has arrived");
        std::cerr << "moppe: multiplayer: companion at "
                  << m_company.link->companion_address () << std::endl;
      } else if (!present && was_present) {
        m_company.notice ("Your companion has gone");
        m_company.hours = -1.0;
        m_company.met = false;
      }
      m_company.companion = m_company.track.advance (now, elapsed);
      meet_strangers (interval);

      if (!m_company.link->due ())
        return;
      CompanionSnapshot mine = my_snapshot ();
      const std::vector<std::uint32_t>& picked = logic ().m_basket.picked;
      mine.pick_count = static_cast<std::uint32_t> (picked.size ());
      mine.picks_from = companion_pick_window_start (
        picked.size (), m_company.snapshots_sent++);
      for (std::size_t i = mine.picks_from;
           i < picked.size () && mine.picks.size () < companion_pick_window;
           ++i)
        mine.picks.push_back (picked[i]);
      m_company.link->send (std::move (mine), m_company.pose_clock);
    }

    void MoppeGame::draw_companion (render::Renderer& r,
                                    render::DrawList& dl,
                                    float time) {
      if (!m_company.companion)
        return;
      const CompanionSnapshot& theirs = *m_company.companion;
      m_blob.draw (dl, surface (), theirs.bike.position, 2.2f);
      render_vehicle (r,
                      dl,
                      theirs.bike,
                      theirs.mode == M_BIKE,
                      0x3000,
                      m_company.their_outfit ());
      m_blob.draw (dl, surface (), theirs.jeep.position, 3.4f);
      // This player beside them, unless seeing through their own eyes.
      render_jeep (r,
                   dl,
                   theirs.jeep,
                   { .driver = theirs.mode == M_JEEP,
                     .passenger = logic ().m_mode == M_PASSENGER &&
                                  !logic ().m_seat_boat &&
                                  logic ().m_cam_mode != CAM_HELMET,
                     .driver_outfit = m_company.their_outfit (),
                     .passenger_outfit = m_company.outfit },
                   0x6000);
      m_blob.draw (dl, surface (), theirs.helicopter.position, 4.5f);
      render_helicopter (r, theirs.helicopter, 0x7000);
      m_blob.draw (dl, surface (), theirs.tractor.position, 2.8f);
      m_blob.draw (dl, surface (), theirs.tractor.trailer_position, 2.4f);
      render_tractor (r,
                      dl,
                      theirs.tractor,
                      theirs.mode == M_TRACTOR
                        ? std::optional<Outfit> (m_company.their_outfit ())
                        : std::nullopt,
                      0x9000);
      m_blob.draw (dl, surface (), theirs.caddy.position, 3.6f);
      render_caddy (r,
                    dl,
                    theirs.caddy,
                    { .driver = theirs.mode == M_CADDY,
                      .driver_outfit = m_company.their_outfit () },
                    0xB000);
      render_boat (r,
                   dl,
                   theirs.boat,
                   theirs.mode == M_BOAT
                     ? std::optional<Outfit> (m_company.their_outfit ())
                     : std::nullopt,
                   0xF800,
                   std::nullopt,
                   time,
                   logic ().m_mode == M_PASSENGER && logic ().m_seat_boat &&
                       logic ().m_cam_mode != CAM_HELMET
                     ? std::optional<Outfit> (m_company.outfit)
                     : std::nullopt);
      m_blob.draw (dl, surface (), theirs.mountain_bike.position, 1.3f);
      render_mountain_bike (
        r,
        dl,
        theirs.mountain_bike,
        theirs.mode == M_MOUNTAIN_BIKE
          ? std::optional<Outfit> (m_company.their_outfit ())
          : std::nullopt,
        0xE000);
      m_blob.draw (dl, surface (), theirs.long_john.position, 2.0f);
      render_long_john (r,
                        dl,
                        theirs.long_john,
                        theirs.mode == M_LONG_JOHN
                          ? std::optional<Outfit> (m_company.their_outfit ())
                          : std::nullopt,
                        0xE100);
      render_jet_ski (r,
                      dl,
                      theirs.jet_ski,
                      theirs.mode == M_JET_SKI
                        ? std::optional<Outfit> (m_company.their_outfit ())
                        : std::nullopt,
                      0xE200);
      if (theirs.hung)
        draw_hammock (dl, theirs.hammock, theirs.hammock_load, theirs.swing);
      if (theirs.hearth_laid)
        draw_hearth (dl, theirs.hearth, theirs.fire_burn);

      m_company.basket.clear ();
      for (std::size_t i = theirs.pick_used; i < m_company.picks.size (); ++i)
        if (m_company.picks[i] < m_mushrooms.mushroom_count ())
          m_company.basket.push_back (m_mushrooms.site (m_company.picks[i]));
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
          m_company.their_outfit ());
        Vec3 floor = hammock_middle (theirs.hammock) +
                     hammock_across (theirs.hammock) * 0.75f -
                     hammock_along (theirs.hammock) * 0.5f;
        floor[1] = world_ground ().height (floor);
        render_basket_set_down (
          dl, floor, hammock_along (theirs.hammock), m_company.basket);
      } else if (theirs.walker) {
        m_blob.draw (
          dl, surface (), theirs.walker->position + Vec3 (0, 0.5f, 0), 0.8f);
        render_walker (dl,
                       *theirs.walker,
                       time,
                       m_company.basket,
                       m_company.their_outfit ());
      }
      if (theirs.glider) {
        m_blob.draw (dl, surface (), theirs.glider->position, 3.4f);
        render_glider (
          r, dl, *theirs.glider, time, 0x4000, m_company.their_outfit ());
      }
    }

    void MoppeGame::light_companion_fire (FrameView& frame) const {
      if (!m_company.companion || !m_company.companion->hearth_laid ||
          m_company.companion->fire_burn <= 0.01f)
        return;
      Camp theirs;
      theirs.hearth = m_company.companion->hearth;
      const Vec3 lamp = fire_light_position (theirs);
      FrameLighting& lighting = frame.lighting;
      if (lighting.lamp_reach > 0.0f &&
          length2 (lighting.lamp_position - frame.camera.position) <=
            length2 (lamp - frame.camera.position))
        return;
      lighting.lamp_position = lamp;
      lighting.lamp_reach = meters_value (fire_light_reach);
      lighting.lamp_color = scale_display (
        fire_light_color (m_company.companion->fire_burn, lighting.time),
        1.0f - 0.25f * lighting.daylight);
    }

    void MoppeGame::add_company_readings (HudState& state) const {
      state.can_ride_along = can_ride_along ();
      if (!m_company.link || !m_company.last_notice)
        return;
      state.notice = m_company.last_notice;
      state.notice_age_s =
        static_cast<float> (m_company.link->now () - m_company.notice_at);
    }
  }
}
