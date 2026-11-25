#include <moppe/environment.hh>
#include <moppe/game/blob_shadow.hh>
#include <moppe/game/bus.hh>
#include <moppe/game/caddy_render.hh>
#include <moppe/game/companionship.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/fishing.hh>
#include <moppe/game/fleet_render.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/glider_render.hh>
#include <moppe/game/hud.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/sling.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/walker_render.hh>
#include <moppe/game/world_ground.hh>
#include <moppe/mov/boat.hh>
#include <moppe/mov/car.hh>

#include <algorithm>
#include <iostream>
#include <set>

namespace moppe::game {
  namespace {
    namespace companionship_parts {
      position_t point_in_frame (const position_t& origin,
                                 const direction_t& forward,
                                 const direction_t& up,
                                 const displacement_t& local) {
        const Vec3& fwd = direction_value (forward);
        const Vec3& u = direction_value (up);
        const Vec3 left = normalized (cross (u, fwd));
        const Vec3& off = displacement_value (local);
        return position (position_value (origin) + left * off[0] + u * off[1] +
                         fwd * off[2]);
      }

      SlingPose sling_pose_of (const CompanionSnapshot& theirs) {
        SlingPose sling;
        const HelicopterPose& heli = theirs.fleet.helicopter;
        sling.hook =
          point_in_frame (heli.position, heli.forward, heli.up, sling_hook ());
        const MachineKind& kind = machine_kind (theirs.sling_load);
        visit (theirs.fleet, theirs.sling_load, [&] (Machine, const auto& p) {
          direction_t fwd {};
          direction_t up {};
          if constexpr (requires { p.render_orientation; }) {
            fwd = p.render_orientation;
            up = p.render_normal;
          } else {
            fwd = p.forward;
            up = p.up;
          }
          sling.ring = point_in_frame (p.position, fwd, up, kind.ring);
          for (std::size_t i = 0; i < kind.legs.size (); ++i)
            sling.legs[i] = point_in_frame (p.position, fwd, up, kind.legs[i]);
        });
        sling.taut = theirs.sling_taut;
        return sling;
      }

      void stamp_peer_snow (RemotePeer& peer,
                            const CompanionSnapshot& theirs,
                            SnowPack& snow,
                            const WorldGround& ground) {
        const Doing mode = theirs.mode;
        const std::optional<Machine> driven = driven_machine (mode);
        const bool foot =
          mode == Doing::on_foot () && theirs.walker && !theirs.lying_down ();
        position_t at = position (Vec3 ());
        direction_t heading = unit_direction (Vec3 (0, 0, 1));
        bool contact = false;
        if (foot) {
          at = theirs.walker->position;
          heading = theirs.walker->heading;
          contact = theirs.walker->grounded && !theirs.walker->swimming;
        } else if (driven && machine_kind (*driven).medium == Medium::land) {
          visit (theirs.fleet, *driven, [&] (Machine, const auto& pose) {
            at = pose.position;
            if constexpr (requires { pose.render_orientation; })
              heading = pose.render_orientation;
            else
              heading = pose.forward;
          });
          const meters_t elev =
            terrain::surface_elevation_value (ground.elevation (plan (at))) *
            u::m;
          contact = static_cast<float> (position_value (at)[1]) * u::m <=
                    elev + 1.4f * u::m;
        }
        const proportion_t cover =
          contact ? ground.snow_cover (plan (at)) : 0.0f * proportion[one];
        press_snow_tracks (snow, peer.snow, mode, at, heading, contact, cover);
      }
    }
  }

  void Companionship::notice (std::string words) {
    last_notice = std::move (words);
    notice_at = link ? link->now () : link_time (0.0);
    std::cerr << "moppe: multiplayer: " << last_notice << std::endl;
  }

  bool Companionship::seek (const std::string& address,
                            std::string* error_out) {
    disconnect ();
    target_address = address;
    last_error.clear ();
    CompanionLink::Config config { .address = address };
    std::string error;
    link = CompanionLink::open (config, error);
    if (!link) {
      last_error = error;
      if (error_out)
        *error_out = error;
      std::cerr << "moppe: multiplayer: " << error << std::endl;
      return false;
    }
    pose_clock = link_time (0.0);
    pose_heard = link_time (-1.0);
    stepped = 0.0 * si::second;
    std::cerr << "moppe: multiplayer: " << link->transport_description ()
              << std::endl;
    return true;
  }

  void Companionship::disconnect () {
    link.reset ();
    target_address.clear ();
    last_error.clear ();
    forget ();
  }

  void Companionship::recompute_outfits () {
    if (!link) {
      outfit = Outfit::blue_jacket;
      return;
    }
    std::vector<std::uint64_t> ids;
    ids.reserve (peers.size () + 1);
    ids.push_back (link->player ());
    for (const RemotePeer& peer : peers)
      ids.push_back (peer.player);
    std::ranges::sort (ids);
    const auto outfit_for = [&] (std::uint64_t id) {
      const auto it = std::ranges::find (ids, id);
      const std::size_t rank =
        it == ids.end () ? 0 : static_cast<std::size_t> (it - ids.begin ());
      return static_cast<Outfit> (rank % outfit_count);
    };
    outfit = outfit_for (link->player ());
    for (RemotePeer& peer : peers)
      peer.outfit = outfit_for (peer.player);
  }

  std::optional<Outfit>
  Companionship::outfit_of (std::uint64_t player_id) const {
    for (const RemotePeer& peer : peers)
      if (peer.player == player_id)
        return peer.outfit;
    return std::nullopt;
  }

  CompanionSnapshot Companionship::snapshot_of (const GameSession& session,
                                                const WorldSummary& world,
                                                bool day_turning) {
    const GameLogicState& state = session.logic ();
    const seconds_t sim_time =
      static_cast<float> (seconds_into_play (state.m_total_time)) * u::s;
    CompanionSnapshot mine;
    mine.play_time = state.m_total_time;
    mine.world = world.tag;
    mine.world_info = world;
    mine.day_hours = day_turning ? state.m_day_hours : clock_time (-1.0);
    mine.mode = state.m_doing;
    mine.ride_target = passenger_in (state.m_doing) ? state.m_ride_peer : 0;
    mine.fleet = poses (session.fleet ());
    mine.sling_hooked = state.m_sling.hooked;
    mine.sling_load = state.m_sling.load;
    mine.sling_taut = state.m_sling.stretch > -0.05f * u::m;
    mine.bus_clock = bus_time (state);
    if (state.m_doing == Doing::driving (Machine::boat))
      mine.helm_rod =
        rod_pose (state.m_fishing.line, state.m_total_time, sim_time);
    if (state.m_doing == Doing::gliding ())
      mine.glider = glider_pose (session.glider ());
    if (state.m_doing == Doing::on_foot ()) {
      mine.walker = walker_pose (session.walker (), state.m_look_pitch);
      if (session.hauling ())
        mine.walker->rope_end = session.rope_end ();
      mine.walker->rod =
        rod_pose (state.m_fishing.line, state.m_total_time, sim_time);
    }
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
    mine.pan_cooked =
      pan_cooked (camp.pan, state.m_total_time).numerical_value_in (one) *
      proportion[one];
    mine.pan_count = static_cast<std::uint8_t> (camp.pan.count);
    std::copy (std::begin (camp.pan.mushrooms),
               std::end (camp.pan.mushrooms),
               std::begin (mine.pan_mushrooms));
    mine.pan_has_fish = camp.pan.has_fish;
    mine.pan_fish = camp.pan.fish;
    mine.pan_eggs = static_cast<std::uint8_t> (camp.pan.eggs);
    mine.pick_used = static_cast<std::uint32_t> (state.m_basket.used);
    return mine;
  }

  void Companionship::hear_of_picks (RemotePeer& peer,
                                     const CompanionSnapshot& theirs,
                                     MushroomPatch& mushrooms) {
    if (theirs.pick_count < peer.picks.size ())
      peer.picks.clear ();
    constexpr std::uint32_t unknown = UINT32_MAX;
    if (theirs.pick_count > mushrooms.mushroom_count ())
      return;
    peer.picks.resize (theirs.pick_count, unknown);
    for (std::size_t i = 0; i < theirs.picks.size (); ++i) {
      const std::size_t ordinal = theirs.picks_from + i;
      const std::uint32_t index = theirs.picks[i];
      if (ordinal >= peer.picks.size () || index >= mushrooms.mushroom_count ())
        continue;
      peer.picks[ordinal] = index;
      mushrooms.take (index);
    }
  }

  void Companionship::ride_along (GameSession& session, InputFrame& input) {
    GameLogicState& state = session.logic ();
    const std::uint64_t my_id = link ? link->player () : 0;
    const bool someone_in_our_boat =
      std::ranges::any_of (active, [my_id] (const CompanionSnapshot& peer) {
        return passenger_in (peer.mode) == Machine::boat &&
               (peer.ride_target == my_id || peer.ride_target == 0);
      });
    session.boat ().set_passenger (someone_in_our_boat);

    const auto find_host = [&] (std::uint64_t id) -> const CompanionSnapshot* {
      for (const CompanionSnapshot& peer : active)
        if (peer.player == id)
          return &peer;
      return nullptr;
    };

    if (const std::optional<Machine> seat = passenger_in (state.m_doing)) {
      const CompanionSnapshot* host = find_host (state.m_ride_peer);
      const bool aground = host &&
                           machine_kind (*seat).medium == Medium::water &&
                           host->mode != Doing::driving (*seat);
      if (!host || input.toggle_mount || aground) {
        const Vec3 left =
          normalized (cross (direction_value (state.m_seat_up),
                             direction_value (state.m_seat_heading)));
        const bool water = machine_kind (*seat).medium == Medium::water;
        session.walker ().spawn (
          moppe::position (position_value (state.m_seat_position) -
                           left * (water ? 1.1f : 1.4f)),
          state.m_seat_heading,
          velocity (velocity_value (state.m_seat_velocity) *
                    (water ? 0.5f : 0.35f)));
        state.m_ride_peer = 0;
        session.set_mode (Doing::on_foot ());
        input.toggle_mount = false;
        return;
      }
      keep_seat (state, *host);
    } else if (!active.empty () && !rode_along &&
               moppe::environment ("MOPPE_RIDE_ALONG")) {
      rode_along = true;
      const CompanionSnapshot& host = active.front ();
      const std::optional<Machine> driven = driven_machine (host.mode);
      take_seat (session,
                 host,
                 driven && machine_kind (*driven).passenger_seat
                   ? *driven
                   : Machine::jeep);
    }
  }

  void Companionship::keep_time (GameSession& session) {
    if (!link)
      return;
    const RemotePeer* leader = nullptr;
    for (const RemotePeer& peer : peers) {
      if (!peer.snapshot)
        continue;
      if (!leader) {
        leader = &peer;
        continue;
      }
      const play_time_t peer_bus =
        peer.snapshot->bus_clock + peer.track.delay ();
      const play_time_t lead_bus =
        leader->snapshot->bus_clock + leader->track.delay ();
      if (peer_bus > lead_bus + 1.0 * si::second ||
          (abs (peer_bus - lead_bus) <= 1.0 * si::second &&
           peer.player > leader->player))
        leader = &peer;
    }
    if (leader && leader->snapshot)
      keep_bus_time (session.logic (),
                     *leader->snapshot,
                     leader->track.delay (),
                     link->player ());
  }

  void Companionship::meet_strangers (const GameSession& session,
                                      seconds_t interval) {
    strangers.clear ();
    for (RemotePeer& peer : peers) {
      if (!peer.snapshot) {
        peer.strangers_were.reset ();
        continue;
      }
      const CompanionSnapshot& theirs = *peer.snapshot;
      std::array<position_t, machine_count> were {};
      for (int i = 0; i < machine_count; ++i) {
        const Machine machine = static_cast<Machine> (i);
        MachineBody body;
        body.machine = machine;
        bool has_authoritative_velocity = false;
        visit (theirs.fleet, machine, [&] (Machine, const auto& pose) {
          body.position = pose.position;
          if constexpr (requires { pose.render_orientation; }) {
            body.forward = pose.render_orientation;
            body.up = pose.render_normal;
          } else {
            body.forward = pose.forward;
            body.up = pose.up;
          }
          if constexpr (requires { pose.velocity; }) {
            body.velocity = pose.velocity;
            has_authoritative_velocity = true;
          }
          if constexpr (requires { pose.trailer_position; }) {
            body.towing = true;
            body.towed_position = pose.trailer_position;
            body.towed_forward = pose.trailer_forward;
            body.towed_up = pose.trailer_up;
            body.towed_mass = session.tractor ().trailer_mass ();
          }
        });
        body.mass = session.machine_body (machine).mass;
        body.ours = false;
        if (!has_authoritative_velocity && peer.strangers_were &&
            interval > 0.0f * si::second) {
          const Vec3 moved = position_value (body.position) -
                             position_value ((*peer.strangers_were)[i]);
          if (length2 (moved) < 25.0f)
            body.velocity =
              moppe::velocity (moved * (1.0f / seconds_value (interval)));
        }
        were[i] = body.position;
        strangers.push_back (body);
      }
      peer.strangers_were = were;
    }
  }

  std::vector<CompanionSnapshot>
  Companionship::hear (MushroomPatch& mushrooms, const WorldSummary& world) {
    std::vector<CompanionSnapshot> newly_met;
    if (!link)
      return newly_met;

    if (const std::string err = link->last_error ();
        !err.empty () && err != last_error) {
      last_error = err;
      notice ("Connection lost: " + err);
    }

    arrived.clear ();
    link->set_world (world);
    link->receive (arrived);
    const link_time_t now = link->now ();
    pose_clock =
      pose_heard < link_time (0.0)
        ? now
        : pose_clock +
            (stepped > 0.0 * si::second ? stepped : now - pose_heard);
    pose_heard = now;
    stepped = 0.0 * si::second;

    if (link->heard_elsewhere () && arrived.empty () && peers.empty () &&
        !elsewhere) {
      elsewhere = true;
      const std::optional<WorldSummary> other = link->elsewhere_world ();
      elsewhere_note = other ? describe_world_mismatch (world, *other)
                             : "Someone is playing, but in another world";
      notice (elsewhere_note);
    }

    bool roster_changed = false;
    for (CompanionSnapshot& theirs : arrived) {
      elsewhere = false;
      elsewhere_note.clear ();
      RemotePeer* peer = nullptr;
      for (RemotePeer& existing : peers)
        if (existing.player == theirs.player) {
          peer = &existing;
          break;
        }
      if (!peer) {
        if (peers.size () >= max_companions)
          continue;
        std::set<std::uint32_t> used_slots;
        for (const RemotePeer& existing : peers)
          used_slots.insert (existing.slot);
        std::uint32_t free_slot = 1;
        while (used_slots.contains (free_slot))
          ++free_slot;
        peers.push_back (RemotePeer {
          .player = theirs.player,
          .slot = free_slot,
          .address = link->address_of (theirs.player),
        });
        peer = &peers.back ();
        roster_changed = true;
        newly_met.push_back (theirs);
      } else {
        if (!peer->track.present (now))
          peer->picks.clear ();
        if (const std::string addr = link->address_of (theirs.player);
            !addr.empty ())
          peer->address = addr;
      }
      hear_of_picks (*peer, theirs, mushrooms);
      peer->hours = theirs.day_hours;
      peer->last_play_time = theirs.play_time;
      peer->track.push (std::move (theirs), now);
    }

    // Retire peers who have fallen silent.
    std::vector<RemotePeer> departed;
    std::erase_if (peers, [&] (RemotePeer& peer) {
      if (peer.track.present (now))
        return false;
      if (peer.announced)
        departed.push_back (std::move (peer));
      roster_changed = true;
      return true;
    });

    if (roster_changed)
      recompute_outfits ();

    for (RemotePeer& peer : peers) {
      if (peer.track.present (now) && !peer.announced) {
        peer.announced = true;
        if (peers.size () == 1)
          notice ("Your companion has arrived");
        else
          notice ("A companion in " + std::string (outfit_name (peer.outfit)) +
                  " has arrived (" + std::to_string (peers.size () + 1) +
                  " players)");
        std::cerr << "moppe: multiplayer: companion #" << peer.slot << " ("
                  << outfit_name (peer.outfit) << ") at " << peer.address
                  << std::endl;
      }
    }
    for (const RemotePeer& gone : departed) {
      if (peers.empty ())
        notice ("Your companion has gone");
      else
        notice ("A companion in " + std::string (outfit_name (gone.outfit)) +
                " has gone");
    }

    // Leading day clock across all peers whose day is turning.
    hours = clock_time (-1.0);
    for (const RemotePeer& peer : peers)
      if (peer.hours >= clock_time (0.0) && peer.hours > hours)
        hours = peer.hours;

    return newly_met;
  }

  void Companionship::follow (GameSession& session,
                              const WorldSummary& world,
                              const WorldGround& ground,
                              bool day_turning,
                              seconds_t interval) {
    if (!link)
      return;
    const link_time_t now = link->now ();
    const position_t subject = session.subject_position ();
    const plan_extent_t period = ground.period ();

    active.clear ();
    active_outfits.clear ();
    for (RemotePeer& peer : peers) {
      peer.snapshot = peer.track.advance (now, interval);
      if (!peer.snapshot)
        continue;
      rebase_snapshot (*peer.snapshot, subject, period);
      companionship_parts::stamp_peer_snow (
        peer, *peer.snapshot, session.logic ().m_snow, ground);
      active.push_back (*peer.snapshot);
      active_outfits.push_back (peer.outfit);
    }

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
                            seconds_t time,
                            const GameLogicState& logic,
                            const MushroomPatch& mushrooms,
                            BlobShadow& blob,
                            const map::SurfaceGeometry& surface,
                            const WorldGround& ground) {
    for (const RemotePeer& peer : peers) {
      if (!peer.snapshot)
        continue;
      const CompanionSnapshot& theirs = *peer.snapshot;
      FleetRiders riders {};
      if (const std::optional<Machine> driven = driven_machine (theirs.mode))
        riders[static_cast<std::size_t> (*driven)].driver = peer.outfit;
      if (const std::optional<Machine> seat = passenger_in (logic.m_doing);
          seat && logic.m_ride_peer == peer.player &&
          logic.m_cam_mode != CAM_HELMET)
        riders[static_cast<std::size_t> (*seat)].passenger = outfit;
      for (const RemotePeer& other : peers) {
        if (!other.snapshot || other.player == peer.player)
          continue;
        if (const std::optional<Machine> seat =
              passenger_in (other.snapshot->mode);
            seat && other.snapshot->ride_target == peer.player)
          riders[static_cast<std::size_t> (*seat)].passenger = other.outfit;
      }
      draw_fleet ({ .r = r,
                    .dl = dl,
                    .time = time,
                    .rod = theirs.helm_rod,
                    .blob = &blob,
                    .surface = &surface },
                  theirs.fleet,
                  riders,
                  peer.slot);
      if (theirs.sling_hooked)
        render_sling (dl, companionship_parts::sling_pose_of (theirs));
      if (theirs.hung)
        draw_hammock (dl, theirs.hammock, theirs.hammock_load, theirs.swing);
      if (theirs.hearth_laid)
        draw_hearth (dl, theirs.hearth, theirs.fire_burn);

      basket.clear ();
      for (std::size_t i = theirs.pick_used; i < peer.picks.size (); ++i)
        if (peer.picks[i] < mushrooms.mushroom_count ())
          basket.push_back (mushrooms.site (peer.picks[i]));
      if (theirs.pan_on) {
        pan.clear ();
        for (int i = 0; i < theirs.pan_count; ++i)
          if (theirs.pan_mushrooms[i] < mushrooms.mushroom_count ())
            pan.push_back (mushrooms.site (theirs.pan_mushrooms[i]));
        draw_pan (dl,
                  theirs.pan_place,
                  theirs.pan_yaw.numerical_value_in (si::radian) * si::radian,
                  pan,
                  theirs.pan_cooked.numerical_value_in (one) * doneness[one],
                  theirs.pan_has_fish ? &theirs.pan_fish : nullptr,
                  theirs.pan_eggs);
      }
      if (theirs.walker && theirs.lying_down ()) {
        figure::draw (
          dl,
          pose_resting (
            theirs.hammock,
            theirs.swing,
            resting_gaze (theirs.hammock, theirs.gaze_yaw, theirs.gaze_pitch),
            time),
          peer.outfit);
        Vec3 floor = position_value (hammock_middle (theirs.hammock)) +
                     direction_value (hammock_across (theirs.hammock)) * 0.75f -
                     direction_value (hammock_along (theirs.hammock)) * 0.5f;
        floor[1] = terrain::surface_elevation_value (
          ground.elevation (plan (position (floor))));
        render_basket_set_down (
          dl, position (floor), hammock_along (theirs.hammock), basket);
      } else if (theirs.walker && theirs.mode != Doing::on_the_bus ()) {
        blob.draw (dl,
                   surface,
                   theirs.walker->position +
                     quantity_cast<isq::position_vector> (
                       displacement (Vec3 (0, 0.5f, 0))),
                   0.8f * u::m);
        render_walker (dl, *theirs.walker, time, basket, peer.outfit);
      }
      if (theirs.glider) {
        blob.draw (dl, surface, theirs.glider->position, 3.4f * u::m);
        const std::uint64_t glider_base =
          0x4000ULL + static_cast<std::uint64_t> (peer.slot - 1) * 0x100ULL;
        render_glider (r, dl, *theirs.glider, time, glider_base, peer.outfit);
      }
    }
  }

  void Companionship::cast_shadows (render::Renderer& r,
                                    render::DrawList& caster_dl) const {
    for (const CompanionSnapshot& theirs : active)
      cast_fleet (r, caster_dl, theirs.fleet);
  }

  void Companionship::draw_glows (render::Renderer& r,
                                  const FrameView& frame,
                                  bool vehicle_effects) const {
    for (const RemotePeer& peer : peers) {
      if (!peer.snapshot)
        continue;
      const CompanionSnapshot& theirs = *peer.snapshot;
      if (vehicle_effects) {
        const std::uint64_t flame_base =
          0x3000ULL + static_cast<std::uint64_t> (peer.slot - 1) * 0x100ULL;
        render_vehicle_flames (r,
                               theirs.fleet.bike,
                               frame.camera.position,
                               frame.lighting.time,
                               flame_base);
      }
      if (theirs.hearth_laid)
        draw_fire (r,
                   theirs.hearth,
                   frame.camera.position,
                   theirs.fire_burn,
                   frame.lighting.time);
    }
  }

  std::vector<BoatWakes::PeerCraft> Companionship::peer_crafts () const {
    std::vector<BoatWakes::PeerCraft> crafts;
    crafts.reserve (active.size ());
    for (const CompanionSnapshot& theirs : active)
      crafts.push_back ({ .player = theirs.player,
                          .boat = theirs.fleet.boat,
                          .jet_ski = theirs.fleet.jet_ski });
    return crafts;
  }

  void Companionship::light_fire (FrameView& frame) const {
    FrameLighting& lighting = frame.lighting;
    const Vec3& camera = position_value (frame.camera.position);
    for (const CompanionSnapshot& theirs : active) {
      if (!theirs.hearth_laid || theirs.fire_burn <= 0.01f * proportion[one])
        continue;
      Camp camp;
      camp.hearth = theirs.hearth;
      const Vec3 lamp = position_value (fire_light_position (camp));
      if (lighting.lamp_reach > 0.0f * u::m &&
          length2 (position_value (lighting.lamp_position) - camera) <=
            length2 (lamp - camera))
        continue;
      lighting.lamp_position = position (lamp);
      lighting.lamp_reach = fire_light_reach;
      lighting.lamp_color = scale_display (
        fire_light_color (theirs.fire_burn, lighting.time),
        (1.0f - 0.25f * lighting.daylight.numerical_value_in (one)) *
          scale_factor[one]);
    }
  }

  void Companionship::add_readings (HudState& state, const GameSession&) const {
    if (!link || last_notice.empty ())
      return;
    state.notice = last_notice.c_str ();
    state.notice_age =
      static_cast<float> (
        (link->now () - notice_at).numerical_value_in (si::second)) *
      u::s;
  }
}
