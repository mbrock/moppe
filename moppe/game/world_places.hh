// What stands at the ends of the roads in the active world (places.hh) and
// what lives there: the plan of the farms, the landing, and the
// sanctuaries; the barns' nest boxes; the built things as drawn and
// collided with; the stock and the poultry let out round them; and the
// lamps they light by night; and the bus round the loop through the
// landing, with its stops (bus.hh).
#ifndef MOPPE_GAME_WORLD_PLACES_HH
#define MOPPE_GAME_WORLD_PLACES_HH

#include <moppe/game/animals.hh>
#include <moppe/game/bus.hh>
#include <moppe/game/bus_render.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/hens.hh>
#include <moppe/game/places.hh>
#include <moppe/game/world_ground.hh>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace moppe::game {
  struct WorldPlaces {
    // Where the places' things stand, planned before the trees and rocks,
    // which keep out of their clearings; with the barns' nest boxes and
    // the eggs first laid in them, the sanctuaries' beacons, the rooms
    // the camera keeps inside, and the bus's stops, named among `names`
    // (the network's places', place_names.hh).
    void plan (const WorldGround& ground,
               GameSession& session,
               std::uint32_t world_seed,
               std::span<const std::string> names);

    // Once the trees stand: the camps among them, and, with a renderer,
    // the built things with their colliders.
    void build (render::Renderer* renderer, const WorldGround& ground);

    // The barns of the farms still lived in, in the plan's order.
    std::vector<const Building*> kept_barns () const;

    std::uint32_t egg_seed () const {
      return m_seed ^ 0xe665u;
    }

    // Each barn's stock: two horses and two cows tied in their stalls
    // with their heads to the manger, and the pigs loose in their pen.
    std::vector<Stall> barn_stalls () const;

    // What the animals read of the world: the ground and its water, the
    // grazing and the plants, and the trunks and walls about.
    AnimalGround animal_ground (const WorldGround& ground) const;

    // The stock goes out round the places still lived in, and the wild
    // ones are where their country is; the poultry to their barns.
    void let_out_animals (const WorldGround& ground,
                          GameSession& session,
                          const Vec3& home_base,
                          const Vec3& spawn) const;

    // The places' stones, timber, and buildings; each beacon's laid wood;
    // the old fire rings, cold until a camp's fire is lit in one; and the
    // bus stops and the buses.
    void draw (render::Renderer& r,
               render::DrawList& dl,
               const FrameView& frame,
               const GameLogicState& logic);

    // A burning beacon near the eye lights what stands round it.
    void light_beacons (FrameView& frame, const std::vector<float>& burn) const;

    // A barn lived in keeps its lantern lit in the dark hours; by day the
    // light from its open doors fills it. Near the eye, it is the lamp,
    // unless a fire is nearer.
    void light_barn (FrameView& frame) const;

    PlacesPlan layout;
    // The barns' nest boxes (the eggs in them are the logic's m_nests).
    std::vector<Nest> nests;
    PlacesLandscape landscape;
    // The bus round the loop, and its stops and buses as drawn.
    BusLine bus;
    BusView bus_view;

  private:
    std::uint32_t m_seed = 0;
  };
}

#endif
