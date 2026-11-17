#ifndef MOPPE_GAME_BUS_RENDER_HH
#define MOPPE_GAME_BUS_RENDER_HH

// The bus line as drawn (game/bus.hh): each stop's sign and shelter, the
// buses where the timetable has them, and over the stop nearest the eye
// its board -- its name and how long till the next bus.

#include <moppe/game/bus.hh>
#include <moppe/game/game_clock.hh>
#include <moppe/mov/trunk_field.hh>
#include <moppe/render/renderer.hh>
#include <moppe/render/text.hh>

#include <cstdint>
#include <string>
#include <vector>

namespace moppe::game {
  // What a stop's board says of `due`: "Bus in 4 min", "Bus here".
  [[nodiscard]] std::string bus_due_words (const BusDue& due);

  // What stands at the stops to be walked into: the shelters' walls and
  // the signs' posts.
  [[nodiscard]] std::vector<mov::Trunk>
  bus_stop_colliders (const BusLine& line);

  class BusView {
  public:
    // Bakes the stops' signs and shelters, and the bus.
    void rebuild (render::Renderer& renderer,
                  const BusLine& line,
                  float period_x,
                  float period_z);

    // The stops near the camera, and the buses where they are at `now`.
    void draw (render::Renderer& renderer,
               const BusLine& line,
               play_time_t now,
               const Vec3& camera,
               std::uint64_t motion_base) const;
    // The same, lent to the sun's shadow.
    void cast_shadows (render::Renderer& renderer,
                       const BusLine& line,
                       play_time_t now,
                       const Vec3& camera) const;

    // The board over the stop nearest `eye`, while it is near: standing
    // over the sign at a fixed size on a screen `height_pts` tall.
    void draw_board (render::Renderer& renderer,
                     const render::Font* font,
                     const BusLine& line,
                     play_time_t now,
                     const Vec3& eye,
                     float height_pts);

  private:
    Vec3 near_copy (const Vec3& at, const Vec3& camera) const;

    std::vector<render::MeshPtr> m_stops;
    render::MeshPtr m_bus;
    render::TextList m_board;
    float m_period_x = 0.0f;
    float m_period_z = 0.0f;
  };
}

#endif
