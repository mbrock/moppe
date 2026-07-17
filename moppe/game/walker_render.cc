#include <moppe/game/walker_render.hh>

#include <moppe/game/model.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  void render_walker (render::DrawList& draw,
                      const WalkerPose& walker,
                      float time,
                      const Vec3& visual_scale) {
    draw.set_texture (nullptr);

    // The rider, dismounted: the same guy in the same gear, with knees and
    // elbows that actually articulate through the gait.
    const bool moving = std::abs (walker.walk) > 0.01f;
    const float phase = walker.animation_distance * 3.6f;
    const float bob = moving ? 0.025f * std::fabs (std::sin (phase)) : 0.0f;
    const float breathe = moving ? 0.0f : 0.006f * std::sin (time * 2.0f);

    draw.push ();
    draw.translate (
      walker.position[0], walker.position[1] + bob, walker.position[2]);
    draw.rotate (
      std::atan2 (walker.heading[0], walker.heading[2]) * u::rad, 0, 1, 0);
    draw.scale (visual_scale);

    // Legs: the thigh swings from the hip, the shin lags behind with a knee
    // bend that folds on the back-swing, and the boot rides along.
    for (int side = -1; side <= 1; side += 2) {
      const float leg_phase = phase + (side > 0 ? 0.0f : PI);
      const float thigh = moving ? -30.0f * std::sin (leg_phase) : 0.0f;
      const float knee =
        moving ? 12.0f + 30.0f * std::max (0.0f, std::sin (leg_phase - 1.1f))
               : 4.0f;

      draw.push ();
      draw.translate (side * 0.10f, 0.82f, 0);
      draw.rotate (thigh * u::deg, 1, 0, 0);

      model::rider_material (draw, model::RiderMaterial::Pants);
      draw.push ();
      draw.translate (0, -0.20f, 0);
      model::box (draw, 0.13f, 0.42f, 0.14f);
      draw.pop ();

      draw.translate (0, -0.40f, 0);
      draw.rotate (knee * u::deg, 1, 0, 0);
      draw.push ();
      draw.translate (0, -0.17f, 0);
      model::box (draw, 0.11f, 0.36f, 0.12f);
      draw.pop ();

      model::rider_material (draw, model::RiderMaterial::Boots);
      draw.push ();
      draw.translate (0, -0.34f, 0.05f);
      model::box (draw, 0.11f, 0.10f, 0.26f);
      draw.pop ();

      draw.pop ();
    }

    // Pelvis, jersey torso, white roost guard.
    model::rider_material (draw, model::RiderMaterial::Pants);
    draw.push ();
    draw.translate (0, 0.88f, 0);
    model::ellipsoid (draw, 0.16f, 0.11f, 0.12f, 8, 6);
    draw.pop ();

    model::rider_material (draw, model::RiderMaterial::Jersey);
    draw.push ();
    draw.translate (0, 1.24f + breathe, 0);
    model::ellipsoid (draw, 0.18f, 0.24f, 0.13f, 8, 6);
    draw.pop ();
    model::rider_material (draw, model::RiderMaterial::Armor);
    draw.push ();
    draw.translate (0, 1.26f + breathe, 0.09f);
    model::box (draw, 0.15f, 0.20f, 0.05f);
    draw.pop ();

    // Arms counter-swing the legs; the elbow keeps a natural bend and the
    // glove caps the wrist.
    for (int side = -1; side <= 1; side += 2) {
      const float arm_phase = phase + (side > 0 ? PI : 0.0f);
      const float swing = moving ? -24.0f * std::sin (arm_phase) : 0.0f;

      draw.push ();
      draw.translate (side * 0.24f, 1.42f, 0);
      draw.rotate (swing * u::deg, 1, 0, 0);

      model::rider_material (draw, model::RiderMaterial::Jersey);
      draw.push ();
      draw.translate (0, -0.15f, 0);
      model::box (draw, 0.09f, 0.32f, 0.09f);
      draw.pop ();

      draw.translate (0, -0.30f, 0);
      draw.rotate ((moving
                      ? -18.0f - 10.0f * std::max (0.0f, std::sin (arm_phase))
                      : -12.0f) *
                     u::deg,
                   1,
                   0,
                   0);
      model::rider_material (draw, model::RiderMaterial::Armor);
      draw.push ();
      draw.translate (0, -0.13f, 0);
      model::box (draw, 0.075f, 0.28f, 0.075f);
      draw.pop ();

      model::rider_material (draw, model::RiderMaterial::Gloves);
      draw.push ();
      draw.translate (0, -0.28f, 0);
      draw.sphere (0.05f, 8, 6);
      draw.pop ();

      draw.pop ();
    }

    // The same lid he wears on the bike: shell, chin bar, dark visor, white
    // peak.
    draw.push ();
    draw.translate (0, 1.62f + breathe, 0);
    model::rider_helmet (draw);
    draw.pop ();

    draw.pop ();
  }
}
