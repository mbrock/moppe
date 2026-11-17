#include <moppe/game/bus_render.hh>

#include <moppe/gfx/mat4.hh>
#include <moppe/render/draw.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace moppe::game {
  namespace {
    namespace bus_render_parts {
      const Vec3 up (0.0f, 1.0f, 0.0f);

      Vec3 left_of (const Vec3& forward) {
        return Vec3 (forward[2], 0.0f, -forward[0]);
      }

      // A box of `size` (whole extents) about `centre`, in the frame
      // the list stands in.
      void box (render::DrawList& dl, const Vec3& centre, const Vec3& size) {
        dl.push ();
        dl.translate (centre);
        dl.scale (size);
        dl.cube (1.0f);
        dl.pop ();
      }

      // A wheel: a short cylinder across x at `centre`.
      void wheel (render::DrawList& dl,
                  const Vec3& centre,
                  float radius,
                  float width) {
        constexpr int sides = 14;
        const float tau = 2.0f * std::numbers::pi_v<float>;
        const auto rim = [&] (int k, float side) {
          const float a = tau * static_cast<float> (k) / sides;
          return centre + Vec3 (side * 0.5f * width,
                                radius * std::sin (a),
                                radius * std::cos (a));
        };
        dl.begin (render::Prim::Triangles);
        for (int k = 0; k < sides; ++k) {
          const float a = tau * (static_cast<float> (k) + 0.5f) / sides;
          dl.normal (Vec3 (0.0f, std::sin (a), std::cos (a)));
          dl.vertex (rim (k, -1.0f));
          dl.vertex (rim (k + 1, -1.0f));
          dl.vertex (rim (k + 1, 1.0f));
          dl.vertex (rim (k, -1.0f));
          dl.vertex (rim (k + 1, 1.0f));
          dl.vertex (rim (k, 1.0f));
          for (const float side : { -1.0f, 1.0f }) {
            dl.normal (Vec3 (side, 0.0f, 0.0f));
            const Vec3 hub = centre + Vec3 (side * 0.5f * width, 0.0f, 0.0f);
            dl.vertex (hub);
            dl.vertex (rim (side > 0.0f ? k : k + 1, side));
            dl.vertex (rim (side > 0.0f ? k + 1 : k, side));
          }
        }
        dl.end ();
      }

      // Till the bus's own model comes: a classic single-decker in its
      // frame (x to the left, y up, z forward, the origin on the road
      // under its middle), eleven metres long, red below the waist and
      // cream above, its windows a dark band between pillars.
      void record_bus (render::DrawList& dl) {
        constexpr float half_length = 5.5f, half_width = 1.25f;
        const DisplayColor red (0.66f, 0.09f, 0.07f);
        const DisplayColor cream (0.92f, 0.87f, 0.72f);
        const DisplayColor glass (0.08f, 0.10f, 0.12f);
        const DisplayColor dark (0.06f, 0.06f, 0.06f);
        // The skirt and the panels up to the waist.
        dl.color (red);
        box (
          dl, Vec3 (0, 0.75f, 0), Vec3 (2 * half_width, 0.8f, 2 * half_length));
        // A cream band at the waist.
        dl.color (cream);
        box (dl,
             Vec3 (0, 1.2f, 0),
             Vec3 (2 * half_width + 0.02f, 0.1f, 2 * half_length + 0.02f));
        // The windows, and the pillars between them.
        dl.color (glass);
        box (dl,
             Vec3 (0, 1.75f, -0.05f),
             Vec3 (2 * half_width - 0.06f, 1.0f, 2 * half_length - 0.1f));
        dl.color (cream);
        for (float z = -half_length + 0.1f; z <= half_length - 0.5f; z += 1.25f)
          box (dl, Vec3 (0, 1.75f, z), Vec3 (2 * half_width, 1.0f, 0.14f));
        // Above the windows, and the roof.
        box (
          dl, Vec3 (0, 2.5f, 0), Vec3 (2 * half_width, 0.5f, 2 * half_length));
        dl.color (0.82f, 0.80f, 0.76f);
        box (dl,
             Vec3 (0, 2.8f, 0),
             Vec3 (2 * half_width - 0.2f, 0.12f, 2 * half_length - 0.3f));
        // The windscreen, raked a little under the destination sign.
        dl.color (glass);
        box (dl,
             Vec3 (0, 1.7f, half_length - 0.02f),
             Vec3 (2 * half_width - 0.2f, 1.1f, 0.06f));
        dl.color (0.95f, 0.68f, 0.12f);
        box (
          dl, Vec3 (0, 2.45f, half_length + 0.01f), Vec3 (1.6f, 0.26f, 0.04f));
        // The front door, on the right (the -x side).
        dl.color (glass);
        box (dl,
             Vec3 (-half_width - 0.01f, 1.3f, half_length - 1.0f),
             Vec3 (0.04f, 2.0f, 1.1f));
        // Lamps and bumpers.
        dl.color (0.98f, 0.95f, 0.82f);
        for (const float x : { -0.85f, 0.85f })
          box (
            dl, Vec3 (x, 0.72f, half_length + 0.02f), Vec3 (0.3f, 0.2f, 0.05f));
        dl.color (0.75f, 0.08f, 0.06f);
        for (const float x : { -0.9f, 0.9f })
          box (dl,
               Vec3 (x, 0.75f, -half_length - 0.02f),
               Vec3 (0.25f, 0.25f, 0.05f));
        dl.color (dark);
        for (const float z : { half_length + 0.06f, -half_length - 0.06f })
          box (dl, Vec3 (0, 0.42f, z), Vec3 (2 * half_width, 0.2f, 0.14f));
        // The wheels, in their arches.
        for (const float z : { 3.4f, -2.6f })
          for (const float x : { -half_width + 0.2f, half_width - 0.2f })
            wheel (dl, Vec3 (x, 0.5f, z), 0.5f, 0.38f);
      }

      // A stop: in the frame of its sign's foot (x along the road against
      // the buses' way, y up, z toward the road), a post with the yellow
      // plate and green H of a Swedish hållplats; and behind it, in the
      // shelter's own frame, a falu-red timber shelter with its open
      // front to the road, a bench inside, and a roof sloping back.
      void record_sign (render::DrawList& dl) {
        dl.color (0.55f, 0.56f, 0.57f);
        box (dl, Vec3 (0, 1.2f, 0), Vec3 (0.07f, 2.6f, 0.07f));
        dl.color (0.97f, 0.80f, 0.12f);
        box (dl, Vec3 (0, 2.25f, 0), Vec3 (0.5f, 0.6f, 0.03f));
        dl.color (0.06f, 0.42f, 0.22f);
        for (const float side : { 1.0f, -1.0f }) {
          const float face = side * 0.017f;
          box (dl, Vec3 (-0.12f, 2.25f, face), Vec3 (0.06f, 0.36f, 0.004f));
          box (dl, Vec3 (0.12f, 2.25f, face), Vec3 (0.06f, 0.36f, 0.004f));
          box (dl, Vec3 (0, 2.25f, face), Vec3 (0.24f, 0.06f, 0.004f));
        }
      }

      constexpr float shelter_half_width = 1.6f, shelter_half_depth = 0.8f;

      void record_shelter (render::DrawList& dl) {
        const float hw = shelter_half_width, hd = shelter_half_depth;
        const DisplayColor falu (0.50f, 0.13f, 0.09f);
        const DisplayColor white (0.90f, 0.89f, 0.85f);
        // A footing of gravel and stone, sunk into the ground.
        dl.color (0.52f, 0.50f, 0.46f);
        box (dl, Vec3 (0, -0.2f, 0), Vec3 (2 * hw + 0.4f, 0.5f, 2 * hd + 0.4f));
        // The back wall and the two sides.
        dl.color (falu);
        box (dl, Vec3 (0, 1.0f, -hd + 0.05f), Vec3 (2 * hw, 2.0f, 0.1f));
        for (const float x : { -hw + 0.05f, hw - 0.05f })
          box (dl, Vec3 (x, 1.05f, -0.1f), Vec3 (0.1f, 2.1f, 2 * hd - 0.2f));
        // White corner posts and the eaves' board.
        dl.color (white);
        for (const float x : { -hw, hw })
          for (const float z : { -hd + 0.05f, hd - 0.1f })
            box (dl, Vec3 (x, 1.1f, z), Vec3 (0.12f, 2.2f, 0.12f));
        box (dl, Vec3 (0, 2.2f, hd - 0.1f), Vec3 (2 * hw + 0.12f, 0.16f, 0.1f));
        // The roof, sloping down to the back.
        dl.color (0.20f, 0.20f, 0.21f);
        dl.push ();
        dl.translate (Vec3 (0, 2.28f, 0));
        dl.mult (Mat4::rotation (-0.12f * u::rad, Vec3 (1, 0, 0)));
        dl.scale (Vec3 (2 * hw + 0.4f, 0.06f, 2 * hd + 0.5f));
        dl.cube (1.0f);
        dl.pop ();
        // The bench along the back wall.
        dl.color (0.62f, 0.45f, 0.28f);
        box (
          dl, Vec3 (0, 0.45f, -hd + 0.35f), Vec3 (2 * hw - 0.4f, 0.06f, 0.4f));
        dl.color (0.25f, 0.24f, 0.23f);
        for (const float x : { -hw + 0.5f, hw - 0.5f })
          box (dl, Vec3 (x, 0.22f, -hd + 0.35f), Vec3 (0.06f, 0.45f, 0.3f));
      }

      // The frame a stop's things stand in: x along the road against the
      // buses' way, y up, and z out toward the road.
      Mat4 facing_road (const Vec3& at, const Vec3& buses_way) {
        const Vec3 toward_road = left_of (buses_way);
        return Mat4::translation (at) *
               Mat4::basis (left_of (toward_road), up, toward_road);
      }

      Mat4 bus_frame (const BusPose& pose, const Vec3& at) {
        const Vec3 left = normalized (cross (pose.up, pose.forward));
        return Mat4::translation (at) *
               Mat4::basis (left, pose.up, pose.forward);
      }
    }
  }

  std::string bus_due_words (const BusDue& due) {
    const double seconds = due.wait.numerical_value_in (si::second);
    if (due.here)
      return seconds < 5.0
               ? "Bus here, leaving"
               : "Bus here, leaves in " +
                   std::to_string (static_cast<int> (seconds)) + " s";
    if (seconds < 60.0)
      return "Bus in " + std::to_string (static_cast<int> (seconds)) + " s";
    return "Bus in " +
           std::to_string (static_cast<int> (std::ceil (seconds / 60.0))) +
           " min";
  }

  std::vector<mov::Trunk> bus_stop_colliders (const BusLine& line) {
    using namespace bus_render_parts;
    std::vector<mov::Trunk> colliders;
    for (const BusStop& stop : line.stops ()) {
      colliders.push_back ({ .root = stop.sign + Vec3 (0, -0.2f, 0),
                             .axis = up,
                             .height = 2.8f,
                             .radius = 0.12f });
      if (!stop.shelter)
        continue;
      const Mat4 frame = facing_road (*stop.shelter, stop.forward);
      const auto post = [&] (float x, float z) {
        colliders.push_back (
          { .root = frame.transform_point (Vec3 (x, -0.3f, z)),
            .axis = up,
            .height = 2.6f,
            .radius = 0.3f });
      };
      for (float x = -shelter_half_width + 0.3f; x <= shelter_half_width;
           x += 0.6f)
        post (x, -shelter_half_depth + 0.3f);
      for (const float x :
           { -shelter_half_width + 0.3f, shelter_half_width - 0.3f })
        post (x, 0.2f);
    }
    return colliders;
  }

  void BusView::rebuild (render::Renderer& renderer,
                         const BusLine& line,
                         float period_x,
                         float period_z) {
    using namespace bus_render_parts;
    m_period_x = period_x;
    m_period_z = period_z;
    m_stops.clear ();
    const auto lit = [] (render::DrawList& dl) {
      render::DrawState solid;
      solid.cull = false;
      dl.state (solid);
      dl.lit (true);
      dl.fogged (true);
    };
    for (const BusStop& stop : line.stops ()) {
      render::DrawList dl;
      lit (dl);
      dl.push ();
      dl.mult (facing_road (stop.sign, stop.forward));
      record_sign (dl);
      dl.pop ();
      if (stop.shelter) {
        dl.push ();
        dl.mult (facing_road (*stop.shelter, stop.forward));
        record_shelter (dl);
        dl.pop ();
      }
      dl.state (render::DrawState ());
      m_stops.push_back (renderer.create_mesh (dl));
    }
    render::DrawList bus;
    lit (bus);
    record_bus (bus);
    bus.state (render::DrawState ());
    m_bus = line.empty () ? nullptr : renderer.create_mesh (bus);
  }

  Vec3 BusView::near_copy (const Vec3& at, const Vec3& camera) const {
    Vec3 out = at;
    out[0] += terrain::whole_periods (camera[0] - at[0], m_period_x);
    out[2] += terrain::whole_periods (camera[2] - at[2], m_period_z);
    return out;
  }

  void BusView::draw (render::Renderer& renderer,
                      const BusLine& line,
                      play_time_t now,
                      const Vec3& camera,
                      std::uint64_t motion_base) const {
    using namespace bus_render_parts;
    std::uint64_t id = motion_base;
    for (std::size_t i = 0; i < m_stops.size () && i < line.stops ().size ();
         ++i) {
      const Vec3& sign = line.stops ()[i].sign;
      const Vec3 at = near_copy (sign, camera);
      if (m_stops[i] && length2 (at - camera) < 1500.0f * 1500.0f)
        renderer.draw_mesh (*m_stops[i], Mat4::translation (at - sign));
    }
    if (!m_bus)
      return;
    for (const BusPose& pose : line.where (now)) {
      ++id;
      const Vec3 at = near_copy (pose.position, camera);
      if (length2 (at - camera) < 2500.0f * 2500.0f)
        renderer.draw_mesh (*m_bus, bus_frame (pose, at), id);
    }
  }

  void BusView::cast_shadows (render::Renderer& renderer,
                              const BusLine& line,
                              play_time_t now,
                              const Vec3& camera) const {
    using namespace bus_render_parts;
    for (std::size_t i = 0; i < m_stops.size () && i < line.stops ().size ();
         ++i) {
      const Vec3& sign = line.stops ()[i].sign;
      const Vec3 at = near_copy (sign, camera);
      if (m_stops[i] && length2 (at - camera) < 160.0f * 160.0f)
        renderer.draw_mesh (*m_stops[i], Mat4::translation (at - sign));
    }
    if (!m_bus)
      return;
    for (const BusPose& pose : line.where (now)) {
      const Vec3 at = near_copy (pose.position, camera);
      if (length2 (at - camera) < 160.0f * 160.0f)
        renderer.draw_mesh (*m_bus, bus_frame (pose, at));
    }
  }

  void BusView::draw_board (render::Renderer& renderer,
                            const render::Font* font,
                            const BusLine& line,
                            play_time_t now,
                            const Vec3& eye,
                            float height_pts) {
    constexpr float shown_m = 40.0f, fading_m = 25.0f;
    if (!font)
      return;
    const std::optional<std::size_t> stop =
      bus_stop_near (line, eye, shown_m * u::m, m_period_x, m_period_z);
    if (!stop)
      return;
    const BusStop& at = line.stops ()[*stop];
    const Vec3 sign = near_copy (at.sign, eye);
    const float metres = std::hypot (sign[0] - eye[0], sign[2] - eye[2]);
    const float shown =
      std::clamp ((shown_m - metres) / (shown_m - fading_m), 0.0f, 1.0f);
    m_board.clear ();
    render::TextStyle title;
    title.size = 20.0f;
    render::TextStyle small;
    small.size = 15.0f;
    small.tracking = 0.03f;
    const std::string due = bus_due_words (line.due (*stop, now));
    const DisplayColor ink (1.0f, 0.86f, 0.32f);
    for (const bool shade : { true, false }) {
      render::TextStyle t = title, d = small;
      t.red = d.red = shade ? 0.0f : ink.red;
      t.green = d.green = shade ? 0.0f : ink.green;
      t.blue = d.blue = shade ? 0.0f : ink.blue;
      d.red = shade ? 0.0f : 1.0f;
      d.green = shade ? 0.0f : 1.0f;
      d.blue = shade ? 0.0f : 1.0f;
      t.alpha = (shade ? 0.5f : 1.0f) * shown;
      d.alpha = (shade ? 0.5f : 0.95f) * shown;
      if (shade)
        t.softness = d.softness = 5.0f;
      font->draw (m_board, 0.0f, 0.0f, at.name, t, render::TextAlign::Center);
      font->draw (m_board, 0.0f, 19.0f, due, d, render::TextAlign::Center);
    }
    m_board.stand (sign + Vec3 (0, 3.2f, 0), height_pts);
    renderer.draw_world_labels (m_board);
  }
}
