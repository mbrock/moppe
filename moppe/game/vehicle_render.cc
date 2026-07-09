#include <moppe/game/vehicle_render.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe {
namespace game {
  // Copied from mov/vehicle.cc: normalizes the shrinking rocket
  // plume against the remaining burn.
  static const float rocket_burn_time = 1.0f;

  static void
  solid_box (render::DrawList& dl, float w, float h, float d) {
    dl.push ();
    dl.scale (w, h, d);
    dl.cube (1.0f);
    dl.pop ();
  }

  static void
  solid_blob (render::DrawList& dl, float rx, float ry, float rz) {
    dl.push ();
    dl.scale (rx, ry, rz);
    dl.sphere (1.0f, 16, 16);
    dl.pop ();
  }

  // A wheel rolling along +z: torus rotated so its axis lies on x.
  static void
  draw_wheel (render::DrawList& dl, float y, float z) {
    dl.push ();
    dl.translate (0, y, z);
    dl.rotate_deg (90, 0, 1, 0);
    dl.color (0.08f, 0.08f, 0.1f);
    dl.torus (0.13f, 0.32f, 10, 18);
    dl.color (0.7f, 0.72f, 0.78f);
    dl.sphere (0.13f, 10, 10);
    dl.pop ();
  }

  // A commandeered car (or truck), drawn in place of the bike.
  // Expects the orientation frame already on the matrix stack.
  static void
  render_car (render::DrawList& dl, const mov::Vehicle& v,
              float time) {
    const bool truck = (v.body_kind () == 3);
    const float t = time;

    // model floor sits where the wheels touch
    dl.translate (0, -1.0f, 0);

    const Vector3D body = v.body_color ();
    dl.color (body.x, body.y, body.z);
    dl.push ();
    dl.translate (0, truck ? 0.8f : 0.55f, 0);
    solid_box (dl, truck ? 2.1f : 1.7f, truck ? 1.5f : 0.85f,
               truck ? 5.6f : 3.6f);
    dl.pop ();

    dl.color (0.2f, 0.25f, 0.3f);
    dl.push ();
    if (truck)
      {
        dl.translate (0, 1.6f, 1.9f);
        solid_box (dl, 1.9f, 0.7f, 1.4f);
      }
    else
      {
        dl.translate (0, 1.15f, -0.2f);
        solid_box (dl, 1.5f, 0.6f, 1.9f);
      }
    dl.pop ();

    if (truck)
      {
        dl.color (0.75f, 0.76f, 0.78f);
        dl.push ();
        dl.translate (0, 1.75f, -0.8f);
        dl.rotate_deg (-6, 1, 0, 0);
        solid_box (dl, 0.5f, 0.12f, 4.4f);
        dl.pop ();
      }

    dl.color (0.08f, 0.08f, 0.1f);
    for (int lx = -1; lx <= 1; lx += 2)
      for (int lz = -1; lz <= 1; lz += 2)
        {
          dl.push ();
          dl.translate (lx * (truck ? 1.0f : 0.8f), 0.3f,
                        lz * (truck ? 1.7f : 1.2f));
          solid_box (dl, 0.25f, 0.6f, 0.65f);
          dl.pop ();
        }

    // flashing light bar on police and fire vehicles
    if (v.body_kind () >= 2)
      {
        dl.lit (false);
        const bool phase_a = std::fmod (t * 3.0f, 1.0f) < 0.5f;
        for (int s = -1; s <= 1; s += 2)
          {
            if ((s > 0) == phase_a)
              dl.color (0.2f, 0.4f, 1.0f);
            else
              dl.color (1.0f, 0.15f, 0.1f);
            dl.push ();
            dl.translate (s * 0.35f, truck ? 2.05f : 1.55f,
                          truck ? 2.0f : -0.2f);
            solid_box (dl, 0.4f, 0.22f, 0.35f);
            dl.pop ();
          }
        dl.lit (true);
      }

    // and of course the jump jets work in a car too
    if (v.rocket_time () > 0)
      {
        const float k = v.rocket_time () / rocket_burn_time;
        dl.lit (false);
        for (int s = -1; s <= 1; s += 2)
          {
            dl.push ();
            dl.translate (s * 0.6f, 0.25f, -0.4f);
            dl.rotate_deg (90, 1, 0, 0);
            dl.color (1.0f, 0.7f, 0.15f);
            dl.cone (0.16f, 2.0f * k, 8, 2);
            dl.color (0.55f, 0.75f, 1.0f);
            dl.cone (0.09f, 2.9f * k, 8, 2);
            dl.pop ();
          }
        dl.lit (true);
      }
  }

  void
  render_vehicle (render::DrawList& dl, const mov::Vehicle& v,
                  float time) {
    dl.push ();

    const Vector3D pos = v.position ();
    dl.translate (pos.x, pos.y + v.susp (), pos.z);

    // Orient the bike along its heading, upright on the smoothed
    // terrain normal (the raw one jitters at speed)
    Vector3D fwd   = v.orientation ().normalized ();
    Vector3D right = v.render_normal ().cross (fwd).normalized ();
    Vector3D up    = fwd.cross (right);

    dl.mult (Mat4::basis (right, up, fwd));

    // Lean into the corner
    dl.rotate_deg (v.lean () * 57.2958f, 0, 0, 1);

    // Chunkier bike, easier to see from the chase camera; lifted so
    // the scaled wheels still touch the ground
    dl.translate (0, 0.5f, 0);
    dl.scale (1.5f, 1.5f, 1.5f);

    if (v.body_kind () != 0)
      {
        render_car (dl, v, time);
        dl.pop ();
        return;
      }

    draw_wheel (dl, -0.55f, -0.75f);

    // Gas tank and frame in glorious metallic blue.
    dl.color (0.15f, 0.5f, 1.0f);
    dl.push ();
    dl.translate (0, -0.15f, 0.1f);
    solid_blob (dl, 0.24f, 0.3f, 0.7f);
    dl.pop ();

    // Seat.
    dl.color (0.08f, 0.08f, 0.1f);
    dl.push ();
    dl.translate (0, -0.02f, -0.5f);
    solid_box (dl, 0.3f, 0.12f, 0.55f);
    dl.pop ();

    // Exhaust pipes.
    dl.color (0.75f, 0.78f, 0.8f);
    for (int s = -1; s <= 1; s += 2)
      {
        dl.push ();
        dl.translate (s * 0.17f, -0.5f, -0.25f);
        solid_box (dl, 0.09f, 0.09f, 0.85f);
        dl.pop ();
      }

    // Steering assembly: forks, handlebars, headlight, front wheel.
    dl.push ();
    dl.translate (0, 0.05f, 0.55f);
    dl.rotate_deg (-v.yaw () * 0.4f * (180.0f / 3.14159f), 0, 1, 0);

    dl.color (0.75f, 0.78f, 0.8f);
    dl.push ();
    dl.translate (0, -0.3f, 0.1f);
    dl.rotate_deg (-18, 1, 0, 0);
    solid_box (dl, 0.08f, 0.75f, 0.08f);
    dl.pop ();

    // Handlebars.
    dl.color (0.1f, 0.1f, 0.12f);
    dl.push ();
    dl.translate (0, 0.12f, 0);
    solid_box (dl, 0.8f, 0.06f, 0.06f);
    dl.pop ();

    // Headlight, drawn unlit so it always looks switched on.
    dl.push ();
    dl.lit (false);
    dl.color (1.0f, 0.95f, 0.7f);
    dl.translate (0, -0.02f, 0.2f);
    dl.sphere (0.09f, 10, 10);
    dl.lit (true);
    dl.pop ();

    draw_wheel (dl, -0.6f, 0.2f);
    dl.pop ();

    // The fearless rider: leaning torso, proper legs gripping the
    // tank, stubby arms, blue helmet.
    dl.color (0.15f, 0.2f, 0.35f);
    dl.push ();
    dl.translate (0, 0.3f, -0.35f);
    dl.rotate_deg (-15, 1, 0, 0);
    solid_blob (dl, 0.2f, 0.34f, 0.16f);
    dl.pop ();

    // legs: near-horizontal thighs, shins down to the pegs, boots
    for (int s = -1; s <= 1; s += 2)
      {
        dl.color (0.14f, 0.15f, 0.2f);
        dl.push ();
        dl.translate (s * 0.18f, 0.02f, -0.15f);
        solid_box (dl, 0.11f, 0.13f, 0.48f);
        dl.pop ();
        dl.push ();
        dl.translate (s * 0.21f, -0.2f, 0.08f);
        solid_box (dl, 0.10f, 0.4f, 0.12f);
        dl.pop ();
        dl.color (0.07f, 0.07f, 0.09f);
        dl.push ();
        dl.translate (s * 0.21f, -0.42f, 0.12f);
        solid_box (dl, 0.11f, 0.11f, 0.28f);
        dl.pop ();
      }
    for (int s = -1; s <= 1; s += 2)
      {
        dl.push ();
        dl.translate (s * 0.21f, 0.33f, 0.12f);
        dl.rotate_deg (22, 1, 0, 0);
        solid_box (dl, 0.07f, 0.07f, 0.8f);
        dl.pop ();
      }
    dl.color (0.15f, 0.45f, 1.0f);
    dl.push ();
    dl.translate (0, 0.64f, -0.18f);
    dl.sphere (0.17f, 12, 12);
    dl.pop ();
    dl.color (0.05f, 0.05f, 0.08f);
    dl.push ();
    dl.translate (0, 0.62f, -0.02f);
    solid_box (dl, 0.16f, 0.08f, 0.1f);
    dl.pop ();

    // Jump-jet nozzles under the frame, pointing at the ground.
    dl.color (0.6f, 0.62f, 0.68f);
    for (int s = -1; s <= 1; s += 2)
      {
        dl.push ();
        dl.translate (s * 0.14f, -0.45f, -0.35f);
        dl.rotate_deg (90, 1, 0, 0);
        dl.cone (0.09f, 0.22f, 8, 2);
        dl.pop ();
      }

    // Exhaust flames while the throttle is open.
    if (std::abs (v.thrust ()) > 0.1)
      {
        dl.lit (false);
        dl.color (1.0f, 0.55f, 0.1f);
        for (int s = -1; s <= 1; s += 2)
          {
            dl.push ();
            dl.translate (s * 0.17f, -0.5f, -0.7f);
            dl.rotate_deg (180, 0, 1, 0);
            dl.cone (0.07f, 0.2f + 0.35f * std::abs (v.thrust ()),
                     8, 2);
            dl.pop ();
          }
        dl.lit (true);
      }

    // Rocket blast while the jump jets burn: an orange plume with
    // a blue-white core, shrinking as the burn runs out.
    if (v.rocket_time () > 0)
      {
        const float k = v.rocket_time () / rocket_burn_time;

        dl.lit (false);
        for (int s = -1; s <= 1; s += 2)
          {
            dl.push ();
            dl.translate (s * 0.14f, -0.55f, -0.35f);
            dl.rotate_deg (90, 1, 0, 0);
            dl.color (1.0f, 0.7f, 0.15f);
            dl.cone (0.14f, 1.8f * k, 8, 2);
            dl.color (0.55f, 0.75f, 1.0f);
            dl.cone (0.08f, 2.6f * k, 8, 2);
            dl.pop ();
          }
        dl.lit (true);
      }

    dl.pop ();
  }
}
}
