#include <moppe/game/place_names.hh>

#include <algorithm>
#include <array>
#include <string_view>

namespace moppe::game {
  namespace {
    using Kind = terrain::RoadPlace::Kind;

    std::uint64_t name_mix (std::uint64_t x) {
      x += 0x9e3779b97f4a7c15ull;
      x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
      x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
      return x ^ (x >> 31);
    }

    // What the name_country round any place is like.
    constexpr std::array<std::string_view, 44> name_country {
      "Björk",  "Gran", "Tall", "Ek",   "Al",     "Asp",   "Rönn", "Sälg",
      "Hassel", "Sten", "Häll", "Grus", "Sand",   "Myr",   "Mos",  "Kärr",
      "Lill",   "Stor", "Norr", "Sör",  "Väster", "Öster", "Ulv",  "Björn",
      "Örn",    "Kråk", "Rav",  "Älg",  "Hjort",  "Svart", "Vit",  "Röd",
      "Grå",    "Kall", "Djup", "Lång", "Bred",   "Hög",   "Ljus", "Fager",
      "Ren",    "Lugn", "Sval", "Mörk",
    };
    // What swims in the water at a landing or an outlet.
    constexpr std::array<std::string_view, 6> name_fish {
      "Lax", "Gädd", "Sik", "Harr", "Abbor", "Ål",
    };
    // The old gods and the fires that were lit to them.
    constexpr std::array<std::string_view, 14> name_holy {
      "Tors", "Odens", "Frös", "Helga", "Vi",    "Ulls",  "Vård",
      "Bål",  "Rös",   "Kas",  "Sol",   "Offer", "Klock", "Hälle",
    };

    constexpr std::array<std::string_view, 5> name_trailhead_endings {
      "säter", "bodarna", "stugan", "torp", "gärdet",
    };
    constexpr std::array<std::string_view, 6> name_landing_endings {
      "vik", "viken", "hamn", "näs", "sund", "udden",
    };
    constexpr std::array<std::string_view, 5> name_outlet_endings {
      "kvarn", "fors", "forsen", "os", "ström",
    };
    constexpr std::array<std::string_view, 6> name_sanctuary_endings {
      "berget", "varden", "kullen", "toppen", "fjället", "höjden",
    };

    template <std::size_t N>
    std::string_view name_pick (const std::array<std::string_view, N>& words,
                                std::uint64_t roll) {
      return words[roll % N];
    }

    std::string name_roll_name (Kind kind, std::uint64_t roll) {
      const std::uint64_t first = name_mix (roll);
      const std::uint64_t second = name_mix (first);
      const std::uint64_t third = name_mix (second);
      std::string_view stem = name_pick (name_country, first);
      std::string_view ending;
      switch (kind) {
      case Kind::Trailhead:
        ending = name_pick (name_trailhead_endings, second);
        break;
      case Kind::Landing:
        if (third % 4 == 0)
          stem = name_pick (name_fish, first);
        ending = name_pick (name_landing_endings, second);
        break;
      case Kind::LakeOutlet:
        if (third % 4 == 0)
          stem = name_pick (name_fish, first);
        ending = name_pick (name_outlet_endings, second);
        break;
      case Kind::Sanctuary:
        if (third % 10 < 7)
          stem = name_pick (name_holy, first);
        ending = name_pick (name_sanctuary_endings, second);
        break;
      }
      return std::string (stem) + std::string (ending);
    }
  }

  std::vector<std::string> name_places (const terrain::RoadNetwork& network,
                                        std::uint32_t seed) {
    std::vector<std::string> names;
    names.reserve (network.places.size ());
    std::array<std::uint64_t, 4> counted {};
    for (const terrain::RoadPlace& place : network.places) {
      const auto kind = static_cast<std::uint64_t> (place.kind);
      const std::uint64_t index = counted[kind]++;
      // Rolled again, further along the same stream, until it is the only
      // place so called.
      std::uint64_t roll = name_mix (
        name_mix (static_cast<std::uint64_t> (seed) * 4 + kind) + index);
      std::string name = name_roll_name (place.kind, roll);
      while (std::ranges::find (names, name) != names.end ()) {
        roll = name_mix (roll);
        name = name_roll_name (place.kind, roll);
      }
      names.push_back (std::move (name));
    }
    return names;
  }
}
