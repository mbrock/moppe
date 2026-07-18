#include <moppe/game/glider_render.hh>

#include <moppe/game/model.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe::game {
  namespace {
    void wing_triangle (render::DrawList& dl,
                        const Vec3& a,
                        const Vec3& b,
                        const Vec3& c) {
      dl.normal (normalized (cross (b - a, c - a)));
      dl.vertex (a);
      dl.vertex (b);
      dl.vertex (c);
    }

    Mat4 glider_frame (const GliderPose& glider, const Vec3& visual_scale) {
      const Vec3 fwd = normalized (glider.heading);
      const Vec3 right = normalized (cross (Vec3 (0, 1, 0), fwd));
      const Vec3 up = cross (fwd, right);
      return Mat4::translation (glider.position) *
             Mat4::basis (right, up, fwd) *
             Mat4::rotation (-glider.bank_radians * u::rad, Vec3 (0, 0, 1)) *
             Mat4::scaling (visual_scale);
    }
  }

  void render_glider (render::DrawList& dl,
                      const GliderPose& glider,
                      float time,
                      const Vec3& visual_scale) {
    const Vec3 nose (0, 0.18f, 1.8f);
    const Vec3 left (-4.6f, 0, -0.75f);
    const Vec3 right (4.6f, 0, -0.75f);
    const Vec3 tail (0, -0.08f, -2.15f);
    const Vec3 center (0, 0.20f, -0.30f);
    const Vec3 hang (0, -1.15f, -0.22f);

    dl.push ();
    dl.mult (glider_frame (glider, visual_scale));

    // A lightly faceted Rogallo wing.  The darker underside is explicitly
    // wound the other way so it remains visible with back-face culling.
    dl.color (0.12f, 0.48f, 0.95f);
    dl.begin (render::Prim::Triangles);
    wing_triangle (dl, nose, right, center);
    wing_triangle (dl, right, tail, center);
    wing_triangle (dl, tail, left, center);
    wing_triangle (dl, left, nose, center);
    dl.end ();

    dl.color (0.06f, 0.16f, 0.34f);
    dl.begin (render::Prim::Triangles);
    wing_triangle (dl, center, right, nose);
    wing_triangle (dl, center, tail, right);
    wing_triangle (dl, center, left, tail);
    wing_triangle (dl, center, nose, left);
    dl.end ();

    // White leading edges make bank and wing attitude readable against both
    // forest and sky; the keel/control frame hangs beneath the sail.
    dl.color (0.86f, 0.89f, 0.92f);
    model::link (dl, left, nose, 0.055f);
    model::link (dl, nose, right, 0.055f);
    model::link (dl, nose, tail, 0.045f);
    model::link (dl, left, hang, 0.035f);
    model::link (dl, right, hang, 0.035f);
    model::link (dl, nose, hang, 0.04f);
    model::link (dl, tail, hang, 0.04f);

    if (glider.bike_attached) {
      dl.color (0.70f, 0.72f, 0.75f);
      model::link (dl, hang, Vec3 (-0.45f, -2.25f, -0.25f), 0.025f);
      model::link (dl, hang, Vec3 (0.45f, -2.25f, -0.25f), 0.025f);
    }

    const float sway = std::sin (time * 3.1f) * 0.035f;
    dl.push ();
    dl.translate (sway, -1.45f, -0.35f);
    dl.rotate (-70 * u::deg, 1, 0, 0);

    model::rider_material (dl, model::RiderMaterial::Jersey);
    model::box (dl, 0.42f, 0.25f, 0.72f);
    model::rider_material (dl, model::RiderMaterial::Armor);
    dl.push ();
    dl.translate (0, 0.12f, 0.10f);
    model::box (dl, 0.44f, 0.06f, 0.42f);
    dl.pop ();

    model::rider_material (dl, model::RiderMaterial::Pants);
    for (int side = -1; side <= 1; side += 2)
      model::link (dl,
                   Vec3 (side * 0.11f, 0, -0.28f),
                   Vec3 (side * 0.14f, -0.06f, -0.84f),
                   0.10f);
    model::rider_material (dl, model::RiderMaterial::Boots);
    for (int side = -1; side <= 1; side += 2) {
      dl.push ();
      dl.translate (side * 0.14f, -0.06f, -0.88f);
      model::box (dl, 0.13f, 0.12f, 0.28f);
      dl.pop ();
    }

    model::rider_material (dl, model::RiderMaterial::Gloves);
    model::link (
      dl, Vec3 (-0.15f, 0, 0.20f), Vec3 (-0.43f, 0.2f, 0.48f), 0.075f);
    model::link (dl, Vec3 (0.15f, 0, 0.20f), Vec3 (0.43f, 0.2f, 0.48f), 0.075f);

    dl.push ();
    dl.translate (0, 0.02f, 0.48f);
    model::rider_helmet (dl);
    dl.pop ();
    dl.pop ();

    dl.pop ();
  }
}
