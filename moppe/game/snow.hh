#ifndef MOPPE_GAME_SNOW_HH
#define MOPPE_GAME_SNOW_HH

#include <moppe/gfx/math.hh>

#include <array>
#include <map>
#include <vector>

namespace moppe::game {
  class GameSession;
  void disturb_snow (GameSession& session, proportion_t cover);

  // Signed relief relative to untouched powder. Keep only tiles somebody
  // has crossed, independent of the camera. Revisit a tile and the old
  // tracks are still there. At the budget, retire the least recently used.
  class SnowPack {
  public:
    static constexpr int tile = 64, window = 12, pixels = window * tile;
    static constexpr meters_t spacing = 0.08f * u::m;
    static constexpr meters_t depth = 0.16f * u::m;
    using Key = std::array<int, 2>;
    static Key tile_at (plan_position_t at);
    void press (plan_position_t from,
                plan_position_t to,
                meters_t radius,
                meters_t sink,
                meters_t belt_phase = -1.0f * u::m);
    // Origin is a tile corner; the upload is centred about the camera.
    bool read (Key origin, std::vector<std::uint8_t>& image) const;
    std::uint64_t revision () const {
      return m_revision;
    }

  private:
    struct Tile {
      std::array<std::uint8_t, tile * tile> height;
      std::uint64_t touched {};
      Tile () {
        height.fill (128);
      }
    };
    std::map<Key, Tile> m_tiles;
    std::uint64_t m_revision {};
  };
}
#endif
