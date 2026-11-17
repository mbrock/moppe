#include <moppe/environment.hh>
#include <moppe/game/blob_shadow.hh>
#include <moppe/game/bus.hh>
#include <moppe/game/companionship.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/fleet_render.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/glider_render.hh>
#include <moppe/game/hud.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/walker_render.hh>
#include <moppe/game/world_ground.hh>
#include <moppe/mov/boat.hh>
#include <moppe/mov/car.hh>

#include <iostream>

namespace moppe::game {
  void Companionship::notice (const char* words) {
    last_notice = words;
    notice_at = link ? link->now () : 0.0;
    std::cerr << "moppe: multiplayer: " << words << std::endl;
  }

  void Companionship::seek (const std::string& address) {
    CompanionLink::Config config { .address = address };
    std::string error;
    link = CompanionLink::open (config, error);
    if (!link) {
      std::cerr << "moppe: multiplayer: " << error << std::endl;
      return;
    }
    std::cerr << "moppe: multiplayer: listening on port " << link->port ()
              << (address.empty () ? ", calling on the local network"
                                   : ", calling " + address)
              << std::endl;
  }

  CompanionSnapshot Companionship::snapshot_of (const GameSession& session,
                                                std::uint32_t world,
                                                bool day_turning) {
    const GameLogicState& state = session.logic ();
    CompanionSnapshot mine;
    mine.world = world;
    mine.day_hours =
      day_turning ? hours_since_midnight (state.m_day_hours) : -1.0;
    mine.mode = state.m_doing;
    mine.fleet = poses (session.fleet ());
    mine.bus_clock = seconds_into_play (bus_time (state));
    if (state.m_doing == Doing::gliding ())
      mine.glider = glider_pose (session.glider ());
    if (state.m_doing == Doing::on_foot ())
      mine.walker = walker_pose (session.walker (), state.m_look_pitch);
    // On the bus, where they sit, which is all the other game can know of
    // a bus on another clock.
    if (state.m_doing == Doing::on_the_bus ()) {
      mine.bus = state.m_bus;
      mine.bus_seat = state.m_bus_seat;
      mine.walker = walker_pose (session.walker (), state.m_look_pitch);
      mine.walker->position = state.m_seat_position;
      mine.walker->heading = state.m_seat_heading;
      mine.walker->velocity = state.m_seat_velocity;
    }
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

  void Companionship::hear_of_picks (const CompanionSnapshot& theirs,
                                     MushroomPatch& mushrooms) {
    if (theirs.pick_count < picks.size ())
      picks.clear ();
    constexpr std::uint32_t unknown = UINT32_MAX;
    // No one has picked more mushrooms than grow.
    if (theirs.pick_count > mushrooms.mushroom_count ())
      return;
    picks.resize (theirs.pick_count, unknown);
    for (std::size_t i = 0; i < theirs.picks.size (); ++i) {
      const std::size_t ordinal = theirs.picks_from + i;
      const std::uint32_t index = theirs.picks[i];
      if (ordinal >= picks.size () || index >= mushrooms.mushroom_count ())
        continue;
      picks[ordinal] = index;
      mushrooms.take (index);
    }
  }

  void Companionship::ride_along (GameSession& session, InputFrame& input) {
    GameLogicState& state = session.logic ();
    session.boat ().set_passenger (
      companion && passenger_in (companion->mode) == Machine::boat);
    if (const std::optional<Machine> seat = passenger_in (state.m_doing)) {
      // Out when the mount key is pressed, or when they have gone, or
      // when what was ridden in is no longer theirs to drive (a boat
      // they have stepped ashore from) -- a step out to the right of
      // the seat, the passenger's side, or over a boat's gunwale.
      const bool aground = companion &&
                           machine_kind (*seat).medium == Medium::water &&
                           companion->mode != Doing::driving (*seat);
      if (!companion || input.toggle_mount || aground) {
        const Vec3 left =
          normalized (cross (state.m_seat_up, state.m_seat_heading));
        const bool water = machine_kind (*seat).medium == Medium::water;
        session.walker ().spawn (moppe::position (state.m_seat_position -
                                                  left * (water ? 1.1f : 1.4f)),
                                 state.m_seat_heading);
        session.set_mode (Doing::on_foot ());
        input.toggle_mount = false;
        return;
      }
    } else if (companion && !rode_along &&
               moppe::environment ("MOPPE_RIDE_ALONG")) {
      // For captures: take the seat in what they drive (the jeep when
      // they drive nothing with one) as soon as there is one, from
      // wherever this player stands.
      rode_along = true;
      const std::optional<Machine> driven = driven_machine (companion->mode);
      take_seat (session,
                 *companion,
                 driven && machine_kind (*driven).passenger_seat
                   ? *driven
                   : Machine::jeep);
    }
    if (passenger_in (state.m_doing))
      keep_seat (state, *companion);
  }

  void Companionship::keep_time (GameSession& session) {
    if (companion && link)
      keep_bus_time (
        session.logic (), *companion, track.delay (), link->player ());
  }

  void Companionship::meet_strangers (const GameSession& session,
                                      seconds_t interval) {
    const float elapsed = seconds_value (interval);
    strangers.clear ();
    if (!companion) {
      strangers_were.reset ();
      return;
    }
    const CompanionSnapshot& theirs = *companion;
    // In Machine's order, as find_bumps meets them.
    for (int i = 0; i < machine_count; ++i) {
      const Machine machine = static_cast<Machine> (i);
      MachineBody body;
      body.machine = machine;
      visit (theirs.fleet, machine, [&] (Machine, const auto& pose) {
        body.position = moppe::position (pose.position);
        if constexpr (requires { pose.render_orientation; }) {
          body.forward = unit_direction (pose.render_orientation);
          body.up = unit_direction (pose.render_normal);
        } else {
          body.forward = unit_direction (pose.forward);
          body.up = unit_direction (pose.up);
        }
        if constexpr (requires { pose.trailer_position; }) {
          body.towing = true;
          body.towed_position = moppe::position (pose.trailer_position);
          body.towed_forward = unit_direction (pose.trailer_forward);
          body.towed_up = unit_direction (pose.trailer_up);
          body.towed_mass = session.tractor ().trailer_mass ();
        }
      });
      body.mass = session.machine_body (machine).mass;
      body.ours = false;
      if (strangers_were && elapsed > 0.0f) {
        const Vec3 moved =
          position_value (body.position) - (*strangers_were)[i];
        if (length2 (moved) < 25.0f)
          body.velocity = moppe::velocity (moved * (1.0f / elapsed));
      }
      strangers.push_back (body);
    }
    std::array<Vec3, machine_count> were;
    for (const MachineBody& body : strangers)
      were[static_cast<std::size_t> (body.machine)] =
        position_value (body.position);
    strangers_were = were;
    // What says how it moves is believed over what was seen to.
    strangers[static_cast<std::size_t> (Machine::jeep)].velocity =
      moppe::velocity (theirs.fleet.jeep.velocity);
    strangers[static_cast<std::size_t> (Machine::boat)].velocity =
      moppe::velocity (theirs.fleet.boat.velocity);
    strangers[static_cast<std::size_t> (Machine::jet_ski)].velocity =
      moppe::velocity (theirs.fleet.jet_ski.velocity);
  }

  std::optional<CompanionSnapshot>
  Companionship::hear (MushroomPatch& mushrooms, std::uint32_t world) {
    if (!link)
      return std::nullopt;
    std::optional<CompanionSnapshot> newly_met;
    arrived.clear ();
    link->set_world (world);
    link->receive (arrived);
    const double now = link->now ();
    // The poses told are the simulation's, which moves on in whole
    // steps, now two to a frame and now three. They are stamped by a
    // clock that moves with it, so that the other machine sees even
    // motion however unevenly the frames fall.
    pose_clock = pose_heard < 0.0
                   ? now
                   : pose_clock + (stepped > 0.0 ? stepped : now - pose_heard);
    pose_heard = now;
    stepped = 0.0;
    const bool was_present = track.present (now);
    // Someone in another world is only told of, while no one here
    // keeps company.
    if (link->heard_elsewhere () && arrived.empty () && !was_present &&
        !elsewhere) {
      elsewhere = true;
      notice ("Someone is playing, but in another world");
      std::cerr << "moppe: multiplayer: launch both games with the "
                   "same --seed and terrain options"
                << std::endl;
    }
    for (CompanionSnapshot& theirs : arrived) {
      elsewhere = false;
      if (!track.present (now))
        picks.clear ();
      hear_of_picks (theirs, mushrooms);
      hours = theirs.day_hours;
      if (!met) {
        met = true;
        // So that the two can be told apart, and each machine agrees
        // on who is who, the player with the greater number changes
        // into the green jacket.
        outfit = link->player () > theirs.player ? Outfit::green_jacket
                                                 : Outfit::blue_jacket;
        newly_met = theirs;
      }
      track.push (std::move (theirs), now);
    }
    const bool present = track.present (now);
    if (present && !was_present) {
      notice ("Your companion has arrived");
      std::cerr << "moppe: multiplayer: companion at "
                << link->companion_address () << std::endl;
    } else if (!present && was_present) {
      notice ("Your companion has gone");
      hours = -1.0;
      met = false;
    }
    return newly_met;
  }

  void Companionship::follow (const GameSession& session,
                              std::uint32_t world,
                              bool day_turning,
                              seconds_t interval) {
    if (!link)
      return;
    companion = track.advance (link->now (), seconds_value (interval));
    meet_strangers (session, interval);

    if (!link->due ())
      return;
    CompanionSnapshot mine = snapshot_of (session, world, day_turning);
    const std::vector<std::uint32_t>& picked = session.logic ().m_basket.picked;
    mine.pick_count = static_cast<std::uint32_t> (picked.size ());
    mine.picks_from =
      companion_pick_window_start (picked.size (), snapshots_sent++);
    for (std::size_t i = mine.picks_from;
         i < picked.size () && mine.picks.size () < companion_pick_window;
         ++i)
      mine.picks.push_back (picked[i]);
    link->send (std::move (mine), pose_clock);
  }

  void Companionship::draw (render::Renderer& r,
                            render::DrawList& dl,
                            float time,
                            const GameLogicState& logic,
                            const MushroomPatch& mushrooms,
                            BlobShadow& blob,
                            const map::SurfaceGeometry& surface,
                            const WorldGround& ground) {
    if (!companion)
      return;
    const CompanionSnapshot& theirs = *companion;
    // This player beside them, unless seeing through their own eyes.
    FleetRiders riders {};
    if (const std::optional<Machine> driven = driven_machine (theirs.mode))
      riders[static_cast<std::size_t> (*driven)].driver = their_outfit ();
    if (const std::optional<Machine> seat = passenger_in (logic.m_doing);
        seat && logic.m_cam_mode != CAM_HELMET)
      riders[static_cast<std::size_t> (*seat)].passenger = outfit;
    draw_fleet (
      { .r = r, .dl = dl, .time = time, .blob = &blob, .surface = &surface },
      theirs.fleet,
      riders,
      true);
    if (theirs.hung)
      draw_hammock (dl, theirs.hammock, theirs.hammock_load, theirs.swing);
    if (theirs.hearth_laid)
      draw_hearth (dl, theirs.hearth, theirs.fire_burn);

    basket.clear ();
    for (std::size_t i = theirs.pick_used; i < picks.size (); ++i)
      if (picks[i] < mushrooms.mushroom_count ())
        basket.push_back (mushrooms.site (picks[i]));
    if (theirs.pan_on) {
      pan.clear ();
      for (int i = 0; i < theirs.pan_count; ++i)
        if (theirs.pan_mushrooms[i] < mushrooms.mushroom_count ())
          pan.push_back (mushrooms.site (theirs.pan_mushrooms[i]));
      draw_pan (dl, theirs.pan_place, theirs.pan_yaw, pan, theirs.pan_cooked);
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
      floor[1] = ground.height (floor);
      render_basket_set_down (
        dl, floor, hammock_along (theirs.hammock), basket);
    } else if (theirs.walker && theirs.mode != Doing::on_the_bus ()) {
      blob.draw (
        dl, surface, theirs.walker->position + Vec3 (0, 0.5f, 0), 0.8f);
      render_walker (dl, *theirs.walker, time, basket, their_outfit ());
    }
    if (theirs.glider) {
      blob.draw (dl, surface, theirs.glider->position, 3.4f);
      render_glider (r, dl, *theirs.glider, time, 0x4000, their_outfit ());
    }
  }

  void Companionship::light_fire (FrameView& frame) const {
    if (!companion || !companion->hearth_laid || companion->fire_burn <= 0.01f)
      return;
    Camp theirs;
    theirs.hearth = companion->hearth;
    const Vec3 lamp = fire_light_position (theirs);
    FrameLighting& lighting = frame.lighting;
    if (lighting.lamp_reach > 0.0f &&
        length2 (lighting.lamp_position - frame.camera.position) <=
          length2 (lamp - frame.camera.position))
      return;
    lighting.lamp_position = lamp;
    lighting.lamp_reach = meters_value (fire_light_reach);
    lighting.lamp_color =
      scale_display (fire_light_color (companion->fire_burn, lighting.time),
                     1.0f - 0.25f * lighting.daylight);
  }

  void Companionship::add_readings (HudState& state,
                                    const GameSession& session) const {
    if (!link || !last_notice)
      return;
    state.notice = last_notice;
    state.notice_age_s = static_cast<float> (link->now () - notice_at);
  }
}
