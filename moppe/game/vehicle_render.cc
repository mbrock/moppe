#include <moppe/game/vehicle_render.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe {
namespace game {
  static float
  boost_nozzle_angle (const mov::Vehicle& v) {
    // The exhaust points opposite the force: backward when boosting
    // forward, straight down at neutral drive, and forward in reverse.
    return 90.0f + 60.0f * v.boost_drive ();
  }

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

  // A box strut between two points (roughly along its z axis), for
  // limbs, forks, and swingarms.  thickness is the full width.
  static void
  draw_limb (render::DrawList& dl, const Vector3D& a,
	     const Vector3D& b, float thickness) {
    const Vector3D mid = (a + b) * 0.5f;
    const Vector3D d = b - a;
    const float len = d.length ();
    const float flat = std::sqrt (d.x * d.x + d.z * d.z);

    dl.push ();
    dl.translate (mid.x, mid.y, mid.z);
    dl.rotate_deg (std::atan2 (d.x, d.z) * 57.2958f, 0, 1, 0);
    dl.rotate_deg (-std::atan2 (d.y, flat) * 57.2958f, 1, 0, 0);
    solid_box (dl, thickness, thickness, len);
    dl.pop ();
  }

  // The dirt bike's spinning spoked wheel with a knobby tire; the
  // axle lies on x.  spin is the roll angle in radians.
  static void
  draw_bike_wheel (render::DrawList& dl, float y, float z,
		   float spin) {
    dl.push ();
    dl.translate (0, y, z);
    dl.rotate_deg (90, 0, 1, 0);
    dl.rotate_deg (spin * 57.2958f, 0, 0, 1);

    // Tire carcass and a ring of knobs.
    dl.color (0.055f, 0.055f, 0.065f);
    dl.torus (0.115f, 0.30f, 8, 18);
    for (int k = 0; k < 10; ++k) {
      dl.push ();
      dl.rotate_deg (k * 36.0f, 0, 0, 1);
      dl.translate (0.375f, 0, 0);
      solid_box (dl, 0.055f, 0.11f, 0.17f);
      dl.pop ();
    }

    // Silver rim, three crossed spoke pairs, hub.
    dl.color (0.70f, 0.72f, 0.76f);
    dl.torus (0.028f, 0.20f, 6, 16);
    for (int k = 0; k < 3; ++k) {
      dl.push ();
      dl.rotate_deg (k * 60.0f + 30.0f, 0, 0, 1);
      solid_box (dl, 0.022f, 0.40f, 0.022f);
      dl.pop ();
    }
    dl.color (0.42f, 0.44f, 0.48f);
    dl.sphere (0.075f, 10, 8);
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

    // And of course the continuously gimballed jump jets work in a car.
    if (v.boost_level () > 0.001f)
      {
        const float k = v.boost_level ();
        const float flicker = 0.88f
          + 0.12f * std::sin (t * 39.0f) * std::sin (t * 26.0f + 1.1f);
        const float len = k * flicker;

        render::DrawState glow;
        glow.blend = true;
        glow.additive = true;
        glow.depth_write = false;
        dl.state (glow);
        dl.lit (false);
        for (int s = -1; s <= 1; s += 2)
          {
            dl.push ();
            dl.translate (s * 0.6f, 0.25f, -0.4f);
            dl.rotate_deg (boost_nozzle_angle (v), 1, 0, 0);
            dl.color (1.0f, 0.42f, 0.10f, 0.28f);
            dl.cone (0.24f, 2.1f * len, 10, 3);
            dl.color (1.0f, 0.62f, 0.18f, 0.55f);
            dl.cone (0.14f, 2.7f * len, 10, 3);
            dl.color (0.90f, 0.95f, 1.0f, 0.85f);
            dl.cone (0.065f, 3.2f * len, 8, 3);
            dl.pop ();
          }
        dl.lit (true);
        dl.state (render::DrawState ());
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

    // Suspension: the frame bobs on susp() at the root while the
    // wheels press back toward the ground, so landings visibly
    // compress the travel.  (Model space is 2/3 world scale.)
    const float wheel_drop = -v.susp () * 0.45f;

    // Rider pose state: stand on the pegs in the air, tuck with
    // speed.  Smoothed here so the pose doesn't pop at touchdown;
    // only one bike ever renders, so file-static state is fine.
    static float s_stand = 0.0f;
    const float stand_target = v.airborne () ? 1.0f : 0.0f;
    s_stand += (stand_target - s_stand) * 0.10f;
    const float stand = s_stand;
    const float tuck = std::min (1.0f, v.velocity ().length ()
                                 / 45.0f) * (1.0f - stand);

    draw_bike_wheel (dl, -0.55f + wheel_drop, -0.75f,
                     v.wheel_spin ());

    // Rear swingarm down to the axle, with a bright shock absorber.
    dl.color (0.55f, 0.57f, 0.62f);
    for (int s = -1; s <= 1; s += 2)
      draw_limb (dl, Vector3D (s * 0.09f, -0.30f, -0.12f),
                 Vector3D (s * 0.09f, -0.55f + wheel_drop, -0.75f),
                 0.05f);
    dl.color (0.85f, 0.25f, 0.10f);
    draw_limb (dl, Vector3D (0, -0.12f, -0.28f),
               Vector3D (0, -0.42f + wheel_drop * 0.6f, -0.55f),
               0.055f);

    // Engine block under the tank.
    dl.color (0.16f, 0.17f, 0.19f);
    dl.push ();
    dl.translate (0, -0.34f, 0.05f);
    solid_box (dl, 0.26f, 0.30f, 0.42f);
    dl.pop ();

    // Gas tank in glorious metallic blue, with white side plates.
    dl.color (0.15f, 0.5f, 1.0f);
    dl.push ();
    dl.translate (0, -0.04f, 0.16f);
    solid_blob (dl, 0.21f, 0.20f, 0.44f);
    dl.pop ();
    dl.color (0.92f, 0.93f, 0.95f);
    for (int s = -1; s <= 1; s += 2)
      {
        dl.push ();
        dl.translate (s * 0.15f, -0.14f, -0.42f);
        dl.rotate_deg (s * 8.0f, 0, 1, 0);
        solid_box (dl, 0.02f, 0.18f, 0.26f);
        dl.pop ();
      }

    // Seat, low and flat like a real dirt bike's.
    dl.color (0.10f, 0.10f, 0.14f);
    dl.push ();
    dl.translate (0, 0.02f, -0.35f);
    solid_box (dl, 0.26f, 0.09f, 0.62f);
    dl.pop ();

    // Blue tail fender kicked up over the rear wheel.
    dl.color (0.15f, 0.5f, 1.0f);
    dl.push ();
    dl.translate (0, 0.10f, -0.70f);
    dl.rotate_deg (14, 1, 0, 0);
    solid_box (dl, 0.20f, 0.035f, 0.38f);
    dl.pop ();

    // Exhaust: header pipe sweeping back into a fat silver muffler.
    dl.color (0.60f, 0.62f, 0.65f);
    draw_limb (dl, Vector3D (0.13f, -0.36f, 0.22f),
               Vector3D (0.17f, -0.30f, -0.50f), 0.07f);
    dl.push ();
    dl.translate (0.17f, -0.27f, -0.62f);
    dl.rotate_deg (-6, 1, 0, 0);
    solid_box (dl, 0.11f, 0.13f, 0.42f);
    dl.pop ();

    // Steering assembly: triple clamp, fork legs, front fender,
    // handlebars, number plate, headlight, front wheel.
    dl.push ();
    dl.translate (0, 0.05f, 0.55f);
    dl.rotate_deg (-v.yaw () * 0.4f * (180.0f / 3.14159f), 0, 1, 0);

    // Fork legs run from the clamp down to the front axle.
    dl.color (0.72f, 0.74f, 0.78f);
    for (int s = -1; s <= 1; s += 2)
      draw_limb (dl, Vector3D (s * 0.10f, 0.10f, -0.02f),
                 Vector3D (s * 0.09f,
                           -0.60f + wheel_drop * 0.7f, 0.20f),
                 0.055f);

    // Front fender arching over the wheel.
    dl.color (0.15f, 0.5f, 1.0f);
    dl.push ();
    dl.translate (0, -0.24f, 0.24f);
    dl.rotate_deg (-20, 1, 0, 0);
    solid_box (dl, 0.20f, 0.035f, 0.52f);
    dl.pop ();

    // Number plate on the forks.
    dl.color (0.92f, 0.93f, 0.95f);
    dl.push ();
    dl.translate (0, 0.02f, 0.06f);
    dl.rotate_deg (-16, 1, 0, 0);
    solid_box (dl, 0.20f, 0.26f, 0.03f);
    dl.pop ();

    // Handlebars: crossbar, grips, risers.
    dl.color (0.1f, 0.1f, 0.12f);
    dl.push ();
    dl.translate (0, 0.14f, -0.02f);
    solid_box (dl, 0.72f, 0.05f, 0.05f);
    dl.pop ();
    for (int s = -1; s <= 1; s += 2)
      {
        dl.push ();
        dl.translate (s * 0.30f, 0.14f, -0.02f);
        solid_box (dl, 0.12f, 0.065f, 0.065f);
        dl.pop ();
      }
    dl.color (0.35f, 0.37f, 0.40f);
    for (int s = -1; s <= 1; s += 2)
      draw_limb (dl, Vector3D (s * 0.06f, 0.02f, 0.0f),
                 Vector3D (s * 0.06f, 0.13f, -0.02f), 0.04f);

    // Headlight, drawn unlit so it always looks switched on, with
    // a soft additive halo that the bloom pass picks up.
    dl.push ();
    dl.lit (false);
    dl.color (1.0f, 0.95f, 0.7f);
    dl.translate (0, -0.06f, 0.16f);
    dl.sphere (0.075f, 10, 10);
    {
      render::DrawState glow;
      glow.blend = true;
      glow.additive = true;
      glow.depth_write = false;
      dl.state (glow);
      dl.color (1.0f, 0.85f, 0.5f, 0.22f);
      dl.sphere (0.13f, 10, 10);
      dl.state (render::DrawState ());
    }
    dl.lit (true);
    dl.pop ();

    draw_bike_wheel (dl, -0.60f + wheel_drop * 0.7f, 0.20f,
                     v.wheel_spin ());
    dl.pop ();

    // The fearless rider.  Hips slide back in a tuck, the whole
    // body rises off the seat in the air, and the head leads the
    // steering.
    const float hip_y = 0.10f + 0.22f * stand;
    const float hip_z = -0.32f + 0.06f * stand - 0.08f * tuck;
    const float sh_y = 0.46f + 0.16f * stand - 0.07f * tuck;
    const float sh_z = -0.30f + 0.10f * stand + 0.10f * tuck;

    for (int s = -1; s <= 1; s += 2)
      {
        const Vector3D hip (s * 0.15f, hip_y, hip_z);
        const Vector3D knee (s * 0.21f, -0.02f + 0.08f * stand,
                             -0.02f + 0.04f * stand);
        const Vector3D ankle (s * 0.21f, -0.24f, 0.03f);

        // Riding gear: dark pants, black boots.
        dl.color (0.16f, 0.17f, 0.22f);
        draw_limb (dl, hip, knee, 0.13f);
        draw_limb (dl, knee, ankle, 0.10f);
        dl.color (0.06f, 0.06f, 0.08f);
        dl.push ();
        dl.translate (s * 0.21f, -0.26f, 0.07f);
        solid_box (dl, 0.11f, 0.10f, 0.30f);
        dl.pop ();
      }

    // Torso in the team blue, pitched with the pose.
    dl.color (0.15f, 0.28f, 0.55f);
    dl.push ();
    dl.translate (0, (hip_y + sh_y) * 0.5f + 0.04f,
                  (hip_z + sh_z) * 0.5f);
    dl.rotate_deg (-16.0f - 16.0f * tuck + 14.0f * stand, 1, 0, 0);
    solid_blob (dl, 0.19f, 0.30f, 0.15f);
    dl.pop ();

    // Arms reach from the shoulders to the grips.
    for (int s = -1; s <= 1; s += 2)
      draw_limb (dl, Vector3D (s * 0.18f, sh_y, sh_z),
                 Vector3D (s * 0.28f, 0.20f, 0.50f), 0.085f);

    // Helmet + dark visor; the head turns into the corner.
    dl.push ();
    dl.translate (0, sh_y + 0.17f, sh_z + 0.13f + 0.06f * tuck);
    dl.rotate_deg (-v.yaw () * 0.3f * (180.0f / 3.14159f), 0, 1, 0);
    dl.color (0.15f, 0.45f, 1.0f);
    dl.sphere (0.16f, 12, 12);
    dl.color (0.05f, 0.05f, 0.08f);
    dl.push ();
    dl.translate (0, 0.01f, 0.13f);
    solid_box (dl, 0.15f, 0.08f, 0.10f);
    dl.pop ();
    dl.pop ();

    // Gimballed jump-jet nozzles under the frame.
    dl.color (0.6f, 0.62f, 0.68f);
    for (int s = -1; s <= 1; s += 2)
      {
        dl.push ();
        dl.translate (s * 0.14f, -0.45f, -0.35f);
        dl.rotate_deg (boost_nozzle_angle (v), 1, 0, 0);
        dl.cone (0.09f, 0.22f, 8, 2);
        dl.pop ();
      }

    // Exhaust flame licking out of the muffler under load: an
    // additive two-layer lick with a pale core.
    if (std::abs (v.thrust ()) > 0.1)
      {
        const float th = std::abs (v.thrust ());
        const float lick = 0.85f + 0.15f
          * std::sin (time * 47.0f + std::sin (time * 31.0f));

        render::DrawState glow;
        glow.blend = true;
        glow.additive = true;
        glow.depth_write = false;
        dl.state (glow);
        dl.lit (false);
        dl.push ();
        dl.translate (0.17f, -0.30f, -0.80f);
        dl.rotate_deg (180, 0, 1, 0);
        dl.color (1.0f, 0.45f, 0.08f, 0.55f);
        dl.cone (0.07f, (0.16f + 0.30f * th) * lick, 8, 2);
        dl.color (1.0f, 0.85f, 0.45f, 0.75f);
        dl.cone (0.035f, (0.22f + 0.36f * th) * lick, 8, 2);
        dl.pop ();
        dl.lit (true);
        dl.state (render::DrawState ());
      }

    // Live jump-jet output: a layered additive plume -- a wide warm
    // sheath around an orange body around a white-hot core, all
    // shivering with engine flicker.  Additive layers sum where
    // they overlap, so the middle reads as incandescent.
    if (v.boost_level () > 0.001f)
      {
        const float k = v.boost_level ();
        const float flicker = 0.88f
          + 0.12f * std::sin (time * 41.0f)
                  * std::sin (time * 27.0f + 1.7f);
        const float len = k * flicker;

        render::DrawState glow;
        glow.blend = true;
        glow.additive = true;
        glow.depth_write = false;
        dl.state (glow);
        dl.lit (false);
        for (int s = -1; s <= 1; s += 2)
          {
            dl.push ();
            dl.translate (s * 0.14f, -0.55f, -0.35f);
            dl.rotate_deg (boost_nozzle_angle (v), 1, 0, 0);
            dl.color (1.0f, 0.42f, 0.10f, 0.28f);
            dl.cone (0.21f, 1.9f * len, 10, 3);
            dl.color (1.0f, 0.62f, 0.18f, 0.55f);
            dl.cone (0.12f, 2.5f * len, 10, 3);
            dl.color (0.90f, 0.95f, 1.0f, 0.85f);
            dl.cone (0.055f, 3.0f * len, 8, 3);
            dl.pop ();
          }
        dl.lit (true);
        dl.state (render::DrawState ());
      }

    dl.pop ();
  }
}
}
