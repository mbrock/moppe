// Renderer testbed: exercises the uber pipeline (lit/unlit, blend,
// solids, fog), the HUD pass, and the frame chain -- no game logic.
// Temporary scaffolding for the Metal port; replaced by the real
// game once the port lands.

#include <moppe/platform/platform.hh>
#include <moppe/render/renderer.hh>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace moppe;

namespace {
  class Testbed : public platform::Game {
  public:
    Testbed () : m_time (0), m_frames (0), m_max_frames (0) {
      if (const char* f = ::getenv ("MOPPE_FRAMES"))
        m_max_frames = ::atoi (f);
    }

    void setup (render::Renderer&, int, int) override {}

    void tick (float dt) override {
      m_time += dt;
    }

    void key (platform::Key k, bool down) override {
      if (k == platform::Key::Escape && down)
        platform::request_quit ();
    }

    void render (render::Renderer& r) override {
      render::FrameParams fp;
      const float orbit = m_time * 0.4f;
      const Vec3 eye (std::sin (orbit) * 18, 8, std::cos (orbit) * 18);
      const Vec3 at (0, 2, 0);
      fp.view = Mat4::look_at (eye, at, Vec3 (0, 1, 0));
      fp.proj = Mat4::perspective_reversed (
        60 * u::deg, (float)r.width_pts () / r.height_pts (), 0.5f, 9000.0f);
      fp.camera_pos = eye;
      fp.cam_right = Vec3 (fp.view.m[0], fp.view.m[4], fp.view.m[8]);
      fp.cam_up = Vec3 (fp.view.m[1], fp.view.m[5], fp.view.m[9]);
      fp.cam_forward = normalized (at - eye);
      fp.clear_color = DisplayColor (0.55f, 0.65f, 0.85f);
      fp.fog_scale = 0.004f;
      fp.sun_dir = normalized (Vec3 (0.5f, 0.8f, 0.3f));
      fp.sun_diffuse = DisplayColor (1.0f, 0.9f, 0.75f);
      fp.sun_specular = DisplayColor (0.5f, 0.5f, 0.5f);
      fp.ambient = DisplayColor (0.35f, 0.35f, 0.4f);
      fp.time = m_time;

      if (!r.begin_frame (fp))
        return;

      m_list.clear ();

      // Ground: a big lit quad.
      m_list.color (0.3f, 0.55f, 0.25f);
      m_list.begin (render::Prim::Quads);
      m_list.normal (Vec3 (0, 1, 0));
      m_list.vertex (-60, 0, -60);
      m_list.vertex (-60, 0, 60);
      m_list.vertex (60, 0, 60);
      m_list.vertex (60, 0, -60);
      m_list.end ();

      // Spinning solids, one of each kind.
      m_list.push ();
      m_list.translate (0, 2.5f, 0);
      m_list.rotate ((m_time * 60) * u::deg, 0, 1, 0);
      m_list.color (0.8f, 0.25f, 0.2f);
      m_list.cube (2.5f);
      m_list.pop ();

      m_list.push ();
      m_list.translate (6, 2, 0);
      m_list.color (0.9f, 0.75f, 0.1f);
      m_list.torus (0.16f, 0.75f, 10, 18);
      m_list.pop ();

      m_list.push ();
      m_list.translate (-6, 2, 0);
      m_list.color (0.2f, 0.4f, 0.9f);
      m_list.sphere (1.5f, 16, 12);
      m_list.pop ();

      m_list.push ();
      m_list.translate (0, 1, 7);
      m_list.rotate (-90 * u::deg, 1, 0, 0);
      m_list.color (0.15f, 0.6f, 0.3f);
      m_list.cone (1.2f, 3.0f, 8, 2);
      m_list.pop ();

      // An unlit emissive marker.
      m_list.push ();
      m_list.translate (0, 6.5f, 0);
      m_list.lit (false);
      m_list.color (1.0f, 0.9f, 0.3f);
      m_list.sphere (0.4f, 8, 8);
      m_list.lit (true);
      m_list.pop ();

      // Translucent quad, depth-write off (a dust/ocean stand-in).
      render::DrawState blend;
      blend.blend = true;
      blend.depth_write = false;
      blend.cull = false;
      m_list.state (blend);
      m_list.lit (false);
      m_list.color (1.0f, 1.0f, 1.0f, 0.35f);
      m_list.begin (render::Prim::Quads);
      m_list.normal (Vec3 (0, 1, 0));
      m_list.vertex (-30, 1.0f, -30);
      m_list.vertex (-30, 1.0f, 30);
      m_list.vertex (30, 1.0f, 30);
      m_list.vertex (30, 1.0f, -30);
      m_list.end ();

      r.draw_list (m_list);

      // HUD: corner panel, a "health bar", a triangle-fan disc.
      m_hud.clear ();
      m_hud.color (0.1f, 0.1f, 0.14f, 0.8f);
      render::DrawState hud_state;
      hud_state.blend = true;
      hud_state.depth_test = false;
      hud_state.depth_write = false;
      hud_state.cull = false;
      m_hud.state (hud_state);
      m_hud.begin (render::Prim::Quads);
      m_hud.vertex (20, 20);
      m_hud.vertex (220, 20);
      m_hud.vertex (220, 70);
      m_hud.vertex (20, 70);
      m_hud.end ();

      m_hud.color (0.2f, 0.9f, 0.3f, 0.9f);
      m_hud.begin (render::Prim::Quads);
      m_hud.vertex (30, 30);
      m_hud.vertex (30 + 180 * (0.5f + 0.5f * std::sin (m_time)), 30);
      m_hud.vertex (30 + 180 * (0.5f + 0.5f * std::sin (m_time)), 44);
      m_hud.vertex (30, 44);
      m_hud.end ();

      m_hud.color (0.95f, 0.8f, 0.2f, 0.9f);
      m_hud.begin (render::Prim::TriangleFan);
      const float cx = 60, cy = 110, rad = 18;
      m_hud.vertex (cx, cy);
      for (int i = 0; i <= 24; ++i) {
        const float a = PI2 * i / 24;
        m_hud.vertex (cx + rad * std::cos (a), cy - rad * std::sin (a));
      }
      m_hud.end ();

      r.draw_hud (m_hud);
      r.end_frame ();

      if (m_max_frames > 0 && ++m_frames >= m_max_frames)
        platform::request_quit ();
    }

  private:
    float m_time;
    int m_frames, m_max_frames;
    render::DrawList m_list;
    render::DrawList m_hud;
  };
}

int main (int argc, char** argv) {
  (void)argc;
  (void)argv;
  Testbed game;
  platform::Config config;
  config.title = "Moppe Renderer Testbed";
  return platform::run (game, config);
}
