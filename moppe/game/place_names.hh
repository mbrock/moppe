#ifndef MOPPE_GAME_PLACE_NAMES_HH
#define MOPPE_GAME_PLACE_NAMES_HH

#include <moppe/terrain/roads.hh>

#include <cstdint>
#include <string>
#include <vector>

namespace moppe::game {
  // A Swedish name for each of the road network's places (roads.hh), in
  // the order of network.places: a compound of a stem from the country
  // round it -- birch, stone, bog, bear -- and an ending that says what
  // the place is: the trailhead a -säter or -bodarna, the sea landing a
  // -vik or -hamn, a lake's outlet a -kvarn or -fors, and a sanctuary a
  // -berget or -varden with an older, holier stem (Tors-, Helga-, Vi-).
  // Each is rolled from the world's seed and the place's kind and its
  // index among places of that kind, so a world keeps its names whatever
  // else changes, and no two places share one.
  [[nodiscard]] std::vector<std::string>
  name_places (const terrain::RoadNetwork& network, std::uint32_t seed);
}

#endif
