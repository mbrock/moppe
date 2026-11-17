// One reckoning of what each button does just now (affordances.hh).

#include <moppe/game/affordances.hh>

#include <moppe/game/bus.hh>
#include <moppe/game/camp.hh>
#include <moppe/game/companion.hh>
#include <moppe/game/fishing.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/sling.hh>
#include <moppe/game/surroundings.hh>

namespace moppe::game {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace affording {
      using Verb = Affordance::Verb;

      // The fire key: a beacon at hand, else the fire beside the walker,
      // else a new one. It is prompted only for a beacon that would light
      // and beside a hung hammock with no fire laid yet.
      Affordance the_fire (const GameSession& session,
                           const Surroundings& around) {
        const GameLogicState& logic = session.logic ();
        const Camp& camp = logic.m_camp;
        if (const std::optional<std::size_t> beacon =
              beacon_at_hand (session, around)) {
          const bool lit = logic.m_beacons[*beacon].burning ();
          const bool theirs = their_beacon (logic, *beacon);
          return { .verb = Verb::tend_beacon,
                   .button = Button::fire,
                   .prompted = !lit && !theirs,
                   .refused = !lit && theirs,
                   .object = static_cast<std::uint32_t> (*beacon) };
        }
        if (beside_fire (session))
          return { .verb = Verb::douse_fire,
                   .button = Button::fire,
                   .prompted = false };
        const bool camping =
          camp.hung && !hearth_laid (camp) &&
          hammock_distance (camp.hammock, session.walker ().position ()) <
            12.0f * u::m;
        return { .verb = Verb::light_fire,
                 .button = Button::fire,
                 .prompted = camping,
                 .refused = !fire_place (session, around) };
      }

      // What the walker's hands reach for, or the angler's at a boat's
      // helm: the line, a mushroom, supper, the hammock, an egg, the
      // fire, and a cast.
      void by_hand (std::vector<Affordance>& list,
                    const GameSession& session,
                    const Surroundings& around) {
        const GameLogicState& logic = session.logic ();
        const Line& line = logic.m_fishing.line;
        const bool on_foot = logic.m_doing == Doing::on_foot ();
        const bool afloat = logic.m_doing == Doing::driving (Machine::boat);
        const bool angling = angler (session).has_value ();

        // While a line is out the use key is its own.
        if (angling && line_out (line) && line.from_boat == afloat) {
          const bool shown = line.phase == Line::Phase::waiting ||
                             line.phase == Line::Phase::biting ||
                             line.phase == Line::Phase::playing;
          list.push_back ({ .verb = Verb::tend_line,
                            .button = Button::use,
                            .prompted = shown });
          return;
        }

        const std::optional<std::uint32_t> mushroom =
          mushroom_at_hand (session, around);
        const std::optional<std::size_t> nest =
          on_foot && !mushroom ? egg_at_hand (session, around) : std::nullopt;
        const Supper supper = on_foot && !mushroom
                                ? supper_at_hand (session, around)
                                : Supper::none;
        const bool by_hammock = on_foot && beside_hammock (session);

        if (mushroom)
          list.push_back ({ .verb = Verb::pick,
                            .button = Button::use,
                            .refused = !mushroom_edible (
                              around.mushrooms->site (*mushroom).kind),
                            .object = *mushroom });
        if (supper != Supper::none)
          list.push_back ({ .verb = Verb::cook,
                            .button = Button::use,
                            .refused = supper == Supper::frying });
        if (on_foot) {
          // The camp's prompts, one at a time: a beacon to light, the
          // hammock to lie in or to hang, a fire to light by it.
          const Affordance fire = the_fire (session, around);
          const bool beacon = fire.verb == Verb::tend_beacon && fire.prompted;
          if (beacon)
            list.push_back (fire);
          if (by_hammock && !mushroom)
            list.push_back ({ .verb = Verb::lie_down,
                              .button = Button::use,
                              .prompted = !beacon });
          if (by_hammock)
            list.push_back ({ .verb = Verb::take_down_hammock,
                              .button = Button::hammock,
                              .prompted = false });
          else if (logic.m_hammock_at_hand)
            list.push_back ({ .verb = Verb::hang_hammock,
                              .button = Button::hammock,
                              .prompted = !beacon });
          if (!beacon)
            list.push_back (fire);
        }
        if (nest)
          list.push_back ({ .verb = Verb::take_egg,
                            .button = Button::use,
                            .refused = logic.m_basket.eggs >= basket_holds_eggs,
                            .object = static_cast<std::uint32_t> (*nest) });

        // A line goes out where there is nothing nearer to do.
        const bool free_hands = afloat
                                  ? boat_drifting (session, around)
                                  : on_foot && !mushroom && !nest &&
                                      supper == Supper::none && !by_hammock;
        if (angling && !line_out (line) && free_hands &&
            water_ahead (session, around))
          list.push_back ({ .verb = Verb::cast, .button = Button::use });
      }

      // What the machines afford.
      void with_machines (std::vector<Affordance>& list,
                          const GameSession& session,
                          const map::SurfaceGeometry& terrain,
                          const Surroundings* around) {
        const GameLogicState& logic = session.logic ();
        const Doing doing = logic.m_doing;
        // The glider, from either key: let the bike go from under it, or
        // open it over the airborne bike.
        if (session.can_drop_bike ()) {
          list.push_back ({ .verb = Verb::drop_bike, .button = Button::use });
          list.push_back ({ .verb = Verb::drop_bike,
                            .button = Button::mount,
                            .prompted = false });
        }
        if (doing == Doing::driving (Machine::helicopter)) {
          // The mount key lets a load go first, else hooks one on, and
          // only then lets the pilot out, down near the ground.
          if (logic.m_sling.hooked)
            list.push_back ({ .verb = Verb::let_load_go,
                              .button = Button::mount,
                              // Prompted only once it hangs near the ground,
                              // though it falls from any height.
                              .prompted = session.helicopter ().altitude () <
                                          sling_hang + 4.0f * u::m,
                              .machine = logic.m_sling.load });
          else if (const std::optional<Machine> under =
                     session.hook_within_reach ())
            list.push_back ({ .verb = Verb::hook,
                              .button = Button::mount,
                              .machine = *under });
        }
        if (session.can_deploy_glider (terrain)) {
          list.push_back (
            { .verb = Verb::deploy_glider, .button = Button::use });
          list.push_back ({ .verb = Verb::deploy_glider,
                            .button = Button::mount,
                            .prompted = false });
        }
        if (around && around->bus && around->places) {
          if (doing == Doing::on_foot ()) {
            if (const std::optional<std::size_t> bus =
                  bus_at_hand (*around->bus,
                               position (session.walker ().position ()),
                               bus_time (logic),
                               plan_extent (around->places->period_x * u::m,
                                            around->places->period_z * u::m)))
              list.push_back ({ .verb = Verb::board_bus,
                                .button = Button::mount,
                                .object = static_cast<std::uint32_t> (*bus) });
          } else if (doing == Doing::on_the_bus ())
            list.push_back (
              { .verb = Verb::leave_bus, .button = Button::mount });
        }
        if (doing == Doing::on_foot ()) {
          if (const std::optional<Machine> seat =
                around ? seat_within_reach (session, around->companion)
                       : std::nullopt)
            list.push_back ({ .verb = Verb::ride_along,
                              .button = Button::mount,
                              .machine = *seat });
          if (const std::optional<Machine> stuck = session.haul_within_reach ())
            list.push_back ({ .verb = Verb::haul,
                              .button = Button::mount,
                              .machine = *stuck });
          if (const std::optional<Machine> reach =
                session.mount_within_reach ())
            list.push_back ({ .verb = Verb::take,
                              .button = Button::mount,
                              .machine = *reach });
        }
        if (const std::optional<Machine> driven = driven_machine (doing);
            driven && (*driven != Machine::helicopter ||
                       session.can_step_out_of_helicopter ()))
          list.push_back ({ .verb = Verb::step_out,
                            .button = Button::mount,
                            .prompted = false,
                            .machine = *driven });
        if (session.stuck ())
          list.push_back ({ .verb = Verb::stuck });
      }
    }
  }

  std::vector<Affordance> affordances (const GameSession& session,
                                       const map::SurfaceGeometry& terrain,
                                       const Surroundings* around) {
    std::vector<Affordance> list;
    using Verb = Affordance::Verb;
    const GameLogicState& logic = session.logic ();
    // Lying in the hammock, either key gets up, once settled in.
    if (logic.m_doing == Doing::on_foot () && logic.m_camp.resting) {
      const bool settled =
        logic.m_total_time - logic.m_camp.rest_time > 0.8 * si::second;
      list.push_back (
        { .verb = Verb::get_up, .button = Button::use, .refused = !settled });
      list.push_back ({ .verb = Verb::get_up,
                        .button = Button::mount,
                        .prompted = false,
                        .refused = !settled });
      return list;
    }
    if (const std::optional<Machine> held = session.hauling ())
      list.push_back ({ .verb = Verb::let_go_rope,
                        .button = Button::mount,
                        .machine = *held });
    if (around)
      affording::by_hand (list, session, *around);
    affording::with_machines (list, session, terrain, around);
    return list;
  }

  void perform (GameSession& session,
                const map::SurfaceGeometry& terrain,
                const Surroundings* around,
                const Affordance& a) {
    using Verb = Affordance::Verb;
    switch (a.verb) {
    case Verb::let_go_rope:
      let_go_rope (session);
      return;
    case Verb::tend_line:
      tend_line_by_hand (session);
      return;
    case Verb::pick:
      pick (session, *around, a.object);
      return;
    case Verb::take_egg:
      take_egg (session, a.object);
      return;
    case Verb::cook:
      cook (session, *around);
      return;
    case Verb::get_up:
      get_up (session);
      return;
    case Verb::lie_down:
      lie_down (session);
      return;
    case Verb::hang_hammock:
      hang_hammock (session);
      return;
    case Verb::take_down_hammock:
      take_down_hammock (session);
      return;
    case Verb::tend_beacon:
      tend_beacon (session, *around, a.object);
      return;
    case Verb::light_fire:
      if (const std::optional<Vec3> hearth = fire_place (session, *around))
        light_fire (session, *hearth);
      return;
    case Verb::douse_fire:
      douse_fire (session);
      return;
    case Verb::cast:
      cast (session, *around);
      return;
    case Verb::drop_bike:
      drop_bike (session);
      return;
    case Verb::deploy_glider:
      deploy_glider (session, terrain);
      return;
    case Verb::let_load_go:
      let_load_go (session);
      return;
    case Verb::hook:
      hook_load (session, a.machine);
      return;
    case Verb::haul:
      take_rope (session, a.machine);
      return;
    case Verb::ride_along:
      take_seat (session, *around->companion, a.machine);
      return;
    case Verb::take:
      get_in (session, a.machine);
      return;
    case Verb::step_out:
      step_out (session, a.machine);
      return;
    case Verb::board_bus: {
      // The front seat by the door, or beside it when the other player
      // sits there.
      const CompanionSnapshot* theirs = around->companion;
      const bool taken = theirs && theirs->mode == Doing::on_the_bus () &&
                         theirs->bus == a.object && theirs->bus_seat == 0;
      board_bus (session, *around->bus, a.object, taken ? 1 : 0);
      return;
    }
      return;
    case Verb::leave_bus:
      leave_bus (session, *around->bus);
      return;
    case Verb::stuck:
      return;
    }
  }

  void press_buttons (GameSession& session,
                      const map::SurfaceGeometry& terrain,
                      const Surroundings* around,
                      InputFrame& input) {
    using Verb = Affordance::Verb;
    const std::pair<Button, bool*> presses[] = {
      { Button::use, &input.use },
      { Button::mount, &input.toggle_mount },
      { Button::hammock, &input.hang_hammock },
      { Button::fire, &input.light_fire },
    };
    bool any = false;
    for (const auto& [button, pressed] : presses)
      any = any || *pressed;
    if (!any)
      return;
    const std::vector<Affordance> here = affordances (session, terrain, around);
    for (const auto& [button, pressed] : presses) {
      if (!*pressed)
        continue;
      const Affordance* a = nullptr;
      for (const Affordance& b : here)
        if (b.button == button) {
          a = &b;
          break;
        }
      // With nothing of its own to do, the mount key picks too, for a
      // touch screen's single button.
      if (!a && button == Button::mount)
        for (const Affordance& b : here)
          if (b.verb == Verb::pick || b.verb == Verb::take_egg) {
            a = &b;
            break;
          }
      if (!a)
        continue;
      *pressed = false;
      perform (session, terrain, around, *a);
      // Lying down, the walker lets go of everything else.
      if (a->verb == Verb::lie_down) {
        input = {};
        return;
      }
    }
  }
}
