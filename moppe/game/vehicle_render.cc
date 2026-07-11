#include <moppe/game/vehicle_render.hh>
#include <moppe/game/model.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/render/renderer.hh>

#include <cmath>

namespace moppe {
namespace game {
  static float
  boost_nozzle_angle (const mov::Vehicle& v) {
    // The exhaust points opposite the force: backward when boosting
    // forward, straight down at neutral drive, and forward in reverse.
    return 90.0f + 60.0f * v.boost_drive ();
  }

  // -- baked bike assemblies ------------------------------------------
  //
  // The bike's rigid clusters are recorded once into retained meshes
  // and replayed with a model matrix, so no per-vertex CPU work
  // remains on the hot path.  Only geometry that actually changes
  // shape per frame -- the suspension links and the additive flames
  // -- still records immediate vertices.
  namespace {
    struct BikeMeshes {
      render::MeshPtr wheel;     // spoked wheel around z; spin is Rz
      render::MeshPtr chassis;   // rigid frame cluster in bike space
      render::MeshPtr steering;  // clamp cluster in steering space
      render::MeshPtr nozzle;    // one jump-jet cone along +z
    };

    // The dirt bike's spoked wheel with a knobby tire, around the z
    // axis; the model matrix lays the axle on x and applies the spin.
    void
    record_wheel (render::DrawList& dl) {
      // Tire carcass and a ring of knobs.
      dl.color (0.055f, 0.055f, 0.065f);
      dl.torus (0.115f, 0.30f, 8, 18);
      for (int k = 0; k < 10; ++k) {
	dl.push ();
	dl.rotate_deg (k * 36.0f, 0, 0, 1);
	dl.translate (0.375f, 0, 0);
	model::box (dl, 0.055f, 0.11f, 0.17f);
	dl.pop ();
      }

      // Silver rim, three crossed spoke pairs, hub.
      dl.color (0.70f, 0.72f, 0.76f);
      dl.torus (0.028f, 0.20f, 6, 16);
      for (int k = 0; k < 3; ++k) {
	dl.push ();
	dl.rotate_deg (k * 60.0f + 30.0f, 0, 0, 1);
	model::box (dl, 0.022f, 0.40f, 0.022f);
	dl.pop ();
      }
      dl.color (0.42f, 0.44f, 0.48f);
      dl.sphere (0.075f, 10, 8);
    }

    void
    record_chassis (render::DrawList& dl) {
      // Engine block under the tank.
      dl.color (0.16f, 0.17f, 0.19f);
      dl.push ();
      dl.translate (0, -0.34f, 0.05f);
      model::box (dl, 0.26f, 0.30f, 0.42f);
      dl.pop ();

      // Gas tank in glorious metallic blue, with white side plates.
      dl.color (0.15f, 0.5f, 1.0f);
      dl.push ();
      dl.translate (0, -0.04f, 0.16f);
      model::ellipsoid (dl, 0.21f, 0.20f, 0.44f);
      dl.pop ();
      dl.color (0.92f, 0.93f, 0.95f);
      for (int s = -1; s <= 1; s += 2)
	{
	  dl.push ();
	  dl.translate (s * 0.15f, -0.14f, -0.42f);
	  dl.rotate_deg (s * 8.0f, 0, 1, 0);
	  model::box (dl, 0.02f, 0.18f, 0.26f);
	  dl.pop ();
	}

      // Seat, low and flat like a real dirt bike's.
      dl.color (0.10f, 0.10f, 0.14f);
      dl.push ();
      dl.translate (0, 0.02f, -0.35f);
      model::box (dl, 0.26f, 0.09f, 0.62f);
      dl.pop ();

      // Blue tail fender kicked up over the rear wheel.
      dl.color (0.15f, 0.5f, 1.0f);
      dl.push ();
      dl.translate (0, 0.10f, -0.70f);
      dl.rotate_deg (14, 1, 0, 0);
      model::box (dl, 0.20f, 0.035f, 0.38f);
      dl.pop ();

      // Exhaust: header pipe sweeping back into a fat silver muffler.
      dl.color (0.60f, 0.62f, 0.65f);
      model::link (dl, Vector3D (0.13f, -0.36f, 0.22f),
		 Vector3D (0.17f, -0.30f, -0.50f), 0.07f);
      dl.push ();
      dl.translate (0.17f, -0.27f, -0.62f);
      dl.rotate_deg (-6, 1, 0, 0);
      model::box (dl, 0.11f, 0.13f, 0.42f);
      dl.pop ();

      // Footpegs.
      dl.color (0.45f, 0.47f, 0.50f);
      for (int s = -1; s <= 1; s += 2)
	{
	  dl.push ();
	  dl.translate (s * 0.16f, -0.285f, 0.02f);
	  model::box (dl, 0.10f, 0.03f, 0.06f);
	  dl.pop ();
	}
    }

    // Everything rigidly attached to the triple clamp: fender,
    // number plate, handlebars, headlight.  The fork legs stretch
    // with the suspension, so they stay immediate.
    void
    record_steering (render::DrawList& dl) {
      // Front fender arching over the wheel.
      dl.color (0.15f, 0.5f, 1.0f);
      dl.push ();
      dl.translate (0, -0.24f, 0.24f);
      dl.rotate_deg (-20, 1, 0, 0);
      model::box (dl, 0.20f, 0.035f, 0.52f);
      dl.pop ();

      // Number plate on the forks.
      dl.color (0.92f, 0.93f, 0.95f);
      dl.push ();
      dl.translate (0, 0.02f, 0.06f);
      dl.rotate_deg (-16, 1, 0, 0);
      model::box (dl, 0.20f, 0.26f, 0.03f);
      dl.pop ();

      // Handlebars: crossbar, grips, risers.
      dl.color (0.1f, 0.1f, 0.12f);
      dl.push ();
      dl.translate (0, 0.14f, -0.02f);
      model::box (dl, 0.72f, 0.05f, 0.05f);
      dl.pop ();
      for (int s = -1; s <= 1; s += 2)
	{
	  dl.push ();
	  dl.translate (s * 0.30f, 0.14f, -0.02f);
	  model::box (dl, 0.12f, 0.065f, 0.065f);
	  dl.pop ();
	}
      dl.color (0.35f, 0.37f, 0.40f);
      for (int s = -1; s <= 1; s += 2)
	model::link (dl, Vector3D (s * 0.06f, 0.02f, 0.0f),
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
    }

    void
    record_nozzle (render::DrawList& dl) {
      dl.color (0.6f, 0.62f, 0.68f);
      dl.cone (0.09f, 0.22f, 8, 2);
    }

    const BikeMeshes&
    bike_meshes (render::Renderer& r) {
      static const BikeMeshes meshes = [&r] {
	BikeMeshes b;
	render::DrawList dl;
	record_wheel (dl);
	b.wheel = r.create_mesh (dl);
	dl.clear ();
	record_chassis (dl);
	b.chassis = r.create_mesh (dl);
	dl.clear ();
	record_steering (dl);
	b.steering = r.create_mesh (dl);
	dl.clear ();
	record_nozzle (dl);
	b.nozzle = r.create_mesh (dl);
	return b;
      } ();
      return meshes;
    }
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
    model::box (dl, truck ? 2.1f : 1.7f, truck ? 1.5f : 0.85f,
               truck ? 5.6f : 3.6f);
    dl.pop ();

    dl.color (0.2f, 0.25f, 0.3f);
    dl.push ();
    if (truck)
      {
        dl.translate (0, 1.6f, 1.9f);
        model::box (dl, 1.9f, 0.7f, 1.4f);
      }
    else
      {
        dl.translate (0, 1.15f, -0.2f);
        model::box (dl, 1.5f, 0.6f, 1.9f);
      }
    dl.pop ();

    if (truck)
      {
        dl.color (0.75f, 0.76f, 0.78f);
        dl.push ();
        dl.translate (0, 1.75f, -0.8f);
        dl.rotate_deg (-6, 1, 0, 0);
        model::box (dl, 0.5f, 0.12f, 4.4f);
        dl.pop ();
      }

    dl.color (0.08f, 0.08f, 0.1f);
    for (int lx = -1; lx <= 1; lx += 2)
      for (int lz = -1; lz <= 1; lz += 2)
        {
          dl.push ();
          dl.translate (lx * (truck ? 1.0f : 0.8f), 0.3f,
                        lz * (truck ? 1.7f : 1.2f));
          model::box (dl, 0.25f, 0.6f, 0.65f);
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
            model::box (dl, 0.4f, 0.22f, 0.35f);
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
  render_vehicle (render::Renderer& r, render::DrawList& dl,
                  const mov::Vehicle& v, float time) {
    dl.push ();

    const Vector3D pos = v.position ();
    dl.translate (pos.x, pos.y + v.susp (), pos.z);

    // Follow the smoothed surface frame on the ground and the velocity arc
    // in flight.
    Vector3D fwd   = v.render_orientation ().normalized ();
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

    const BikeMeshes& bm = bike_meshes (r);
    const Mat4 frame = dl.matrix ();

    const Vector3D x_axis (1, 0, 0), y_axis (0, 1, 0), z_axis (0, 0, 1);
    const Mat4 axle = Mat4::rotation (degrees_to_radians (90.0f), y_axis)
      * Mat4::rotation (v.wheel_spin (), z_axis);

    // Suspension: the frame bobs on susp() at the root while the
    // wheels press back toward the ground, so landings visibly
    // compress the travel.  (Model space is 2/3 world scale.)
    const float wheel_drop = -v.susp () * 0.45f;

    // Rear wheel on the swingarm.
    r.draw_mesh (*bm.wheel, frame
		 * Mat4::translation
		   (Vector3D (0, -0.55f + wheel_drop, -0.75f))
		 * axle);

    // Rear swingarm down to the axle, with a bright shock absorber;
    // both stretch with the suspension, so they stay immediate.
    dl.color (0.55f, 0.57f, 0.62f);
    for (int s = -1; s <= 1; s += 2)
      model::link (dl, Vector3D (s * 0.09f, -0.30f, -0.12f),
                 Vector3D (s * 0.09f, -0.55f + wheel_drop, -0.75f),
                 0.05f);
    dl.color (0.85f, 0.25f, 0.10f);
    model::link (dl, Vector3D (0, -0.12f, -0.28f),
               Vector3D (0, -0.42f + wheel_drop * 0.6f, -0.55f),
               0.055f);

    // The rigid frame cluster: engine, tank, seat, fenders, exhaust.
    r.draw_mesh (*bm.chassis, frame);

    // Steering assembly: triple clamp cluster, fork legs, front wheel.
    const radians_t steer = -v.yaw () * 0.4f;
    const Mat4 steering = frame
      * Mat4::translation (Vector3D (0, 0.05f, 0.55f))
      * Mat4::rotation (steer, y_axis);
    r.draw_mesh (*bm.steering, steering);

    // Fork legs run from the clamp down to the front axle.
    dl.push ();
    dl.translate (0, 0.05f, 0.55f);
    dl.rotate (steer, y_axis);
    dl.color (0.72f, 0.74f, 0.78f);
    for (int s = -1; s <= 1; s += 2)
      model::link (dl, Vector3D (s * 0.10f, 0.10f, -0.02f),
                 Vector3D (s * 0.09f,
                           -0.60f + wheel_drop * 0.7f, 0.20f),
                 0.055f);
    dl.pop ();

    r.draw_mesh (*bm.wheel, steering
		 * Mat4::translation
		   (Vector3D (0, -0.60f + wheel_drop * 0.7f, 0.20f))
		 * axle);

    // Gimballed jump-jet nozzles under the frame.
    for (int s = -1; s <= 1; s += 2)
      r.draw_mesh (*bm.nozzle, frame
		   * Mat4::translation
		     (Vector3D (s * 0.14f, -0.45f, -0.35f))
		   * Mat4::rotation
		     (degrees_to_radians (boost_nozzle_angle (v)),
		      x_axis));

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
