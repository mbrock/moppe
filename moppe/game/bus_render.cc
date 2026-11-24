#include <moppe/game/bus_render.hh>

#include <moppe/game/bus_model.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/render/draw.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace moppe::game {
  namespace {
    namespace bus_render_parts {
      const direction_t up = unit_direction (Vec3 (0.0f, 1.0f, 0.0f));

      direction_t left_of (const direction_t& forward) {
        return unit_direction (Vec3 (
          direction_value (forward)[2], 0.0f, -direction_value (forward)[0]));
      }

      // A box of `size` (whole extents) about `centre`, in the frame
      // the list stands in.
      void box (render::DrawList& dl,
                const displacement_t& centre,
                const spatial_extent_t& size) {
        dl.push ();
        dl.translate (centre);
        dl.scale (extent_value (size));
        dl.cube (1.0f);
        dl.pop ();
      }

      // A stop: in the frame of its sign's foot (x along the road against
      // the buses' way, y up, z toward the road), a post with the yellow
      // plate and green H of a Swedish hållplats; and behind it, in the
      // shelter's own frame, a falu-red timber shelter with its open
      // front to the road, a bench inside, and a roof sloping back.
      void record_sign (render::DrawList& dl) {
        dl.color (0.55f, 0.56f, 0.57f);
        box (dl,
             displacement (Vec3 (0, 1.2f, 0)),
             spatial_extent_in_metres (Vec3 (0.07f, 2.6f, 0.07f)));
        dl.color (0.97f, 0.80f, 0.12f);
        box (dl,
             displacement (Vec3 (0, 2.25f, 0)),
             spatial_extent_in_metres (Vec3 (0.5f, 0.6f, 0.03f)));
        dl.color (0.06f, 0.42f, 0.22f);
        for (const float side : { 1.0f, -1.0f }) {
          // The H on each face of the plate, standing just proud of it.
          const meters_t face = side * 0.017f * u::m;
          box (dl,
               displacement (Vec3 (-0.12f, 2.25f, meters_value (face))),
               spatial_extent_in_metres (Vec3 (0.06f, 0.36f, 0.004f)));
          box (dl,
               displacement (Vec3 (0.12f, 2.25f, meters_value (face))),
               spatial_extent_in_metres (Vec3 (0.06f, 0.36f, 0.004f)));
          box (dl,
               displacement (Vec3 (0, 2.25f, meters_value (face))),
               spatial_extent_in_metres (Vec3 (0.24f, 0.06f, 0.004f)));
        }
      }

      constexpr meters_t shelter_half_width = 1.6f * u::m,
                         shelter_half_depth = 0.8f * u::m;

      void record_shelter (render::DrawList& dl) {
        // The shelter's literal geometry below is in metres.
        const float hw = meters_value (shelter_half_width),
                    hd = meters_value (shelter_half_depth);
        const DisplayColor falu (0.50f, 0.13f, 0.09f);
        const DisplayColor white (0.90f, 0.89f, 0.85f);
        // A footing of gravel and stone, sunk into the ground.
        dl.color (0.52f, 0.50f, 0.46f);
        box (
          dl,
          displacement (Vec3 (0, -0.2f, 0)),
          spatial_extent_in_metres (Vec3 (2 * hw + 0.4f, 0.5f, 2 * hd + 0.4f)));
        // The back wall and the two sides.
        dl.color (falu);
        box (dl,
             displacement (Vec3 (0, 1.0f, -hd + 0.05f)),
             spatial_extent_in_metres (Vec3 (2 * hw, 2.0f, 0.1f)));
        for (const float x : { -hw + 0.05f, hw - 0.05f })
          box (dl,
               displacement (Vec3 (x, 1.05f, -0.1f)),
               spatial_extent_in_metres (Vec3 (0.1f, 2.1f, 2 * hd - 0.2f)));
        // White corner posts and the eaves' board.
        dl.color (white);
        for (const float x : { -hw, hw })
          for (const float z : { -hd + 0.05f, hd - 0.1f })
            box (dl,
                 displacement (Vec3 (x, 1.1f, z)),
                 spatial_extent_in_metres (Vec3 (0.12f, 2.2f, 0.12f)));
        box (dl,
             displacement (Vec3 (0, 2.2f, hd - 0.1f)),
             spatial_extent_in_metres (Vec3 (2 * hw + 0.12f, 0.16f, 0.1f)));
        // The roof, sloping down to the back.
        dl.color (0.20f, 0.20f, 0.21f);
        dl.push ();
        dl.translate (displacement (Vec3 (0, 2.28f, 0)));
        dl.rotate (-0.12f * u::rad, unit_direction (Vec3 (1, 0, 0)));
        dl.scale (Vec3 (2 * hw + 0.4f, 0.06f, 2 * hd + 0.5f));
        dl.cube (1.0f);
        dl.pop ();
        // The bench along the back wall.
        dl.color (0.62f, 0.45f, 0.28f);
        box (dl,
             displacement (Vec3 (0, 0.45f, -hd + 0.35f)),
             spatial_extent_in_metres (Vec3 (2 * hw - 0.4f, 0.06f, 0.4f)));
        dl.color (0.25f, 0.24f, 0.23f);
        for (const float x : { -hw + 0.5f, hw - 0.5f })
          box (dl,
               displacement (Vec3 (x, 0.22f, -hd + 0.35f)),
               spatial_extent_in_metres (Vec3 (0.06f, 0.45f, 0.3f)));
      }

      // The frame a stop's things stand in: x along the road against the
      // buses' way, y up, and z out toward the road.
      Mat4 facing_road (const terrain::torus_position_t& at,
                        const direction_t& buses_way) {
        const direction_t toward_road = left_of (buses_way);
        return Mat4::translation (terrain::torus_position_value (at)) *
               Mat4::basis (direction_value (left_of (toward_road)),
                            direction_value (up),
                            direction_value (toward_road));
      }

      // The bus's model as render_car reads it: its driver at the wheel
      // and a rider in front seat `seat` (bus_seat_in_model), feet on
      // the floor.
      CarModel bus_car_for (std::size_t seat) {
        namespace m = bus_model;
        const displacement_t at = bus_seat_in_model (seat);
        return {
          .steering_ratio = 6.0f * scale_factor[one],
          .steering_hub = displacement (m::steering_hub),
          .steering_column = displacement (m::steering_column),
          .grip_left = displacement (m::grip_left),
          .grip_right = displacement (m::grip_right),
          .seat_driver = displacement (m::seat_driver),
          .seat_passenger = at,
          .pedal_left = displacement (m::pedal_left),
          .pedal_right = displacement (m::pedal_right),
          .footrest_left =
            displacement (Vec3 (displacement_value (at)[0] + 0.12f,
                                m::aisle[1],
                                displacement_value (at)[2] + 0.45f)),
          .footrest_right =
            displacement (Vec3 (displacement_value (at)[0] - 0.12f,
                                m::aisle[1],
                                displacement_value (at)[2] + 0.45f)),
          .wheels = { displacement (m::wheel_fl),
                      displacement (m::wheel_fr),
                      displacement (m::wheel_rl),
                      displacement (m::wheel_rr) },
          .lean = -0.10f * u::rad,
          .sole_tilt = 0.30f * grade[one],
          .knee_ahead = 0.5f * scale_factor[one],
          .knee_out = 0.15f * scale_factor[one],
        };
      }

      const CarModel& bus_car (std::size_t seat = 0) {
        static const CarModel models[2] = { bus_car_for (0), bus_car_for (1) };
        return models[std::min<std::size_t> (seat, 1)];
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
      colliders.push_back (
        { .root = position (terrain::torus_position_value (stop.sign) +
                            Vec3 (0, -0.2f, 0)),
          .axis = up,
          .height = 2.8f * u::m,
          .radius = 0.12f * u::m });
      if (!stop.shelter)
        continue;
      const Mat4 frame = facing_road (*stop.shelter, stop.forward);
      const auto post = [&] (meters_t x, meters_t z) {
        colliders.push_back ({ .root = position (frame.transform_point (Vec3 (
                                 meters_value (x), -0.3f, meters_value (z)))),
                               .axis = up,
                               .height = 2.6f * u::m,
                               .radius = 0.3f * u::m });
      };
      for (meters_t x = -shelter_half_width + 0.3f * u::m;
           x <= shelter_half_width;
           x += 0.6f * u::m)
        post (x, -shelter_half_depth + 0.3f * u::m);
      for (const meters_t x : { -shelter_half_width + 0.3f * u::m,
                                shelter_half_width - 0.3f * u::m })
        post (x, 0.2f * u::m);
    }
    return colliders;
  }

  void BusView::rebuild (render::Renderer& renderer,
                         const BusLine& line,
                         plan_extent_t period) {
    using namespace bus_render_parts;
    m_period = period;
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
    m_bus = {};
    m_doors = {};
    if (line.empty ())
      return;
    namespace m = bus_model;
    m_bus.body = { { model_mesh::bake (renderer, m::body) } };
    m_bus.steering_wheel = model_mesh::bake (renderer, m::steering_wheel);
    m_bus.wheel = model_mesh::bake (renderer, m::wheel);
    m_bus.rear_wheel = model_mesh::bake (renderer, m::wheel_rear);
    // The windows seen from the seats as well as from outside: the
    // model's panes face out, so each is drawn again facing in, for a
    // rider looking out through them.
    {
      std::vector<model_mesh::Vertex> inner (m::glass.vertices.begin (),
                                             m::glass.vertices.end ());
      for (model_mesh::Vertex& v : inner)
        for (float& n : v.normal)
          n = -n;
      std::vector<model_mesh::Triangle> turned (m::glass.triangles.begin (),
                                                m::glass.triangles.end ());
      for (model_mesh::Triangle& t : turned)
        std::swap (t.vertex[1], t.vertex[2]);
      render::DrawList glass;
      model_mesh::record (glass, m::glass);
      model_mesh::record (glass, { inner, turned, m::glass.paints });
      m_bus.glass = { { renderer.create_mesh (glass) } };
    }
    m_doors = { model_mesh::bake (renderer, m::door_fore),
                model_mesh::bake (renderer, m::door_aft) };
  }

  position_t BusView::near_copy (const position_t& at,
                                 const position_t& camera) const {
    position_t out = at;
    position_value (out)[0] += terrain::whole_periods (
      position_value (camera)[0] - position_value (at)[0],
      plan_value (m_period).x ());
    position_value (out)[2] += terrain::whole_periods (
      position_value (camera)[2] - position_value (at)[2],
      plan_value (m_period).z ());
    return out;
  }

  void BusView::draw (render::Renderer& renderer,
                      const BusLine& line,
                      play_time_t now,
                      const position_t& camera,
                      std::uint64_t motion_base,
                      std::span<const BusRider> riders) {
    using namespace bus_render_parts;
    std::uint64_t id = motion_base;
    for (std::size_t i = 0; i < m_stops.size () && i < line.stops ().size ();
         ++i) {
      const terrain::torus_position_t& sign = line.stops ()[i].sign;
      const position_t at =
        sign.point_for (terrain::world_origin, camera, m_period);
      if (m_stops[i] &&
          length2 (displacement_value (at - camera)) < 1500.0f * 1500.0f)
        renderer.draw_mesh (
          *m_stops[i],
          Mat4::translation (position_value (at) -
                             terrain::torus_position_value (sign)));
    }
    if (!m_bus.wheel)
      return;
    const std::vector<BusPose> buses = line.where (now);
    for (std::size_t k = 0; k < buses.size (); ++k) {
      const BusPose& pose = buses[k];
      id += 16;
      const position_t at = near_copy (pose.position, camera);
      if (length2 (displacement_value (at - camera)) > 2500.0f * 2500.0f ||
          !renderer.in_scene (at, 12.0f * u::m))
        continue;
      const CarPose car { .position = at,
                          .forward = pose.forward,
                          .up = pose.up,
                          .steer = pose.steer,
                          .wheel_spin = pose.wheel_spin };
      // A seated torso spans about one metre. Once it covers fewer than
      // three scene pixels behind the glass, omit the skinned occupants.
      const float focal = renderer.scene_focal_pixels ();
      const meters_t rider_reach = (focal / 3.0f) * u::m;
      const bool show_riders = focal <= 0.0f ||
        dot (at - camera, at - camera) <= rider_reach * rider_reach;
      // Its riders, by seat.
      std::array<std::optional<Outfit>, 2> seated;
      for (const BusRider& rider : riders)
        if (rider.bus == k && rider.seen)
          seated[std::min<std::size_t> (rider.seat, 1)] = rider.outfit;
      m_occupants.clear ();
      render_car (
        renderer,
        m_occupants,
        car,
        { .driver = show_riders,
          .passenger = show_riders && seated[0].has_value (),
          .driver_outfit = Outfit::green_jacket,
          .passenger_outfit = seated[0].value_or (Outfit::blue_jacket) },
        id,
        bus_car (),
        m_bus);
      if (show_riders && seated[1])
        seat_car_passenger (m_occupants, car, bus_car (1), *seated[1]);
      // Each bus owns its occupants' motion history as buses enter or
      // leave view. The walker's vertices keep their own correspondence.
      if (!m_occupants.empty ())
        renderer.draw_list (m_occupants, id);
      // Its doors, folding out about their hinges at a stop.
      const Mat4 frame = model_mesh::body_frame (at, pose.forward, pose.up);
      // The doors' swing when they stand fully open.
      const radians_t swing = radians_t (1.536f * pose.door_open * u::rad);
      renderer.draw_mesh (*m_doors[0],
                          frame *
                            Mat4::translation (bus_model::door_fore_hinge) *
                            Mat4::rotation (swing, Vec3 (0, 1, 0)),
                          id + 14);
      renderer.draw_mesh (*m_doors[1],
                          frame *
                            Mat4::translation (bus_model::door_aft_hinge) *
                            Mat4::rotation (-swing, Vec3 (0, 1, 0)),
                          id + 15);
    }
  }

  void BusView::cast_shadows (render::Renderer& renderer,
                              const BusLine& line,
                              play_time_t now,
                              const position_t& camera) const {
    using namespace bus_render_parts;
    for (std::size_t i = 0; i < m_stops.size () && i < line.stops ().size ();
         ++i) {
      const terrain::torus_position_t& sign = line.stops ()[i].sign;
      const position_t at =
        sign.point_for (terrain::world_origin, camera, m_period);
      if (m_stops[i] &&
          length2 (displacement_value (at - camera)) < 160.0f * 160.0f)
        renderer.draw_mesh (
          *m_stops[i],
          Mat4::translation (position_value (at) -
                             terrain::torus_position_value (sign)));
    }
    if (m_bus.body.empty ())
      return;
    for (const BusPose& pose : line.where (now)) {
      const position_t at = near_copy (pose.position, camera);
      if (length2 (displacement_value (at - camera)) < 160.0f * 160.0f)
        renderer.draw_mesh (*m_bus.body.front ().mesh,
                            model_mesh::body_frame (at, pose.forward, pose.up));
    }
  }

}
