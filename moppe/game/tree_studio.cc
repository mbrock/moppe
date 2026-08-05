// Tree studio: render (plane + tree) and look at the tree.
//
// A specimen bench for the procedural forest: a flat neutral ground plane, a
// lineup of conifer individuals across the age range, a fixed studio camera,
// and nothing else -- no generated world, no trail, no reverse-engineered
// viewpoints. MOPPE_STUDIO_OUT captures one deterministic PNG and exits;
// without it the camera orbits slowly for interactive inspection.
//
// MOPPE_STUDIO_DISTANCE overrides the camera distance in metres, so the same
// lineup documents the hero, middle, and proxy detail tiers.

#include <moppe/platform/platform.hh>
#include <moppe/render/renderer.hh>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace moppe;

namespace {
  class TreeStudio : public platform::Game {
  public:
    TreeStudio () {
      if (const char* out = ::getenv ("MOPPE_STUDIO_OUT"))
        m_output = out;
      if (const char* distance = ::getenv ("MOPPE_STUDIO_DISTANCE"))
        m_distance = std::max (10.0f, (float)::atof (distance));
    }

    void setup (render::Renderer& r, int, int) override {
      // A lineup across the age range, all conifer: sapling to ancient.
      const float heights[] = { 4.0f, 9.0f, 15.0f, 20.0f, 25.0f, 30.0f };
      const render::ForestAge ages[] = {
        render::ForestAge::Sapling, render::ForestAge::Young,
        render::ForestAge::Young,   render::ForestAge::Mature,
        render::ForestAge::Mature,  render::ForestAge::Ancient,
      };
      std::vector<render::ForestInstance> trees;
      float x = -42.0f;
      for (int i = 0; i < 6; ++i) {
        const float height = heights[i];
        x += 6.0f + 0.55f * height;
        trees.push_back ({
          .root = position (Vec3 (x, 0.0f, 0.0f)),
          .ground_normal =
            Vec3 (0, 1, 0) * terrain::terrain_normal[mp_units::one],
          .height = height * u::m,
          .crown_radius = 0.19f * height * u::m,
          .canopy_cover = 0.5f * mp_units::one,
          .moisture = 0.5f * mp_units::one,
          .seed = 0x51a7c0deu + 977u * (unsigned)i,
          .species = render::ForestSpecies::Conifer,
          .age = ages[i],
        });
      }
      r.set_forest ({ .period = {} }, trees);
    }

    void tick (float dt) override {
      m_time += dt;
    }

    void key (platform::Key k, bool down) override {
      if (k == platform::Key::Escape && down)
        platform::request_quit ();
    }

    void render (render::Renderer& r) override {
      render::FrameParams fp;
      const bool capture = !m_output.empty ();
      const float orbit = capture ? 0.0f : m_time * 0.15f;
      const Vec3 at (4, 9, 0);
      const Vec3 eye = at + Vec3 (std::sin (orbit) * m_distance,
                                  3.0f,
                                  std::cos (orbit) * m_distance);
      fp.view = Mat4::look_at (eye, at, Vec3 (0, 1, 0));
      fp.proj = Mat4::perspective_reversed (
        50 * u::deg, (float)r.width_pts () / r.height_pts (), 0.5f, 9000.0f);
      fp.camera_pos = eye;
      fp.cam_right =
        Vec3 (fp.view.element (0), fp.view.element (4), fp.view.element (8));
      fp.cam_up =
        Vec3 (fp.view.element (1), fp.view.element (5), fp.view.element (9));
      fp.cam_forward = normalized (at - eye);
      fp.clear_color = DisplayColor (0.62f, 0.71f, 0.82f);
      fp.fog_scale = 0.0001f;
      // Morning sun from front-left: form-modelling light with a visible
      // shaded side on every specimen.
      fp.sun_dir = normalized (Vec3 (0.55f, 0.42f, 0.72f));
      fp.sun_diffuse = DisplayColor (1.0f, 0.93f, 0.80f);
      fp.sun_specular = DisplayColor (0.6f, 0.6f, 0.55f);
      fp.ambient = DisplayColor (0.34f, 0.36f, 0.40f);
      fp.time = capture ? 0.0f : m_time;
      // The studio wants raw, predictable light: no adaptation, no bloom.
      fp.auto_exposure = false;
      fp.bloom = false;
      fp.lens_flare = false;

      if (!r.begin_frame (fp))
        return;

      render::SkyParams sky;
      sky.time = fp.time;
      sky.sun_height = 0.62f;
      sky.cloudiness = 0.2f;
      sky.sun_dir = fp.sun_dir;
      sky.fog_color = fp.clear_color;
      r.draw_sky (sky);

      // The plane: one big neutral quad.
      m_list.clear ();
      m_list.color (0.36f, 0.40f, 0.33f);
      m_list.begin (render::Prim::Quads);
      m_list.normal (Vec3 (0, 1, 0));
      m_list.vertex (-400, 0, -400);
      m_list.vertex (-400, 0, 400);
      m_list.vertex (400, 0, 400);
      m_list.vertex (400, 0, -400);
      m_list.end ();
      r.draw_list (m_list);

      r.draw_forest ();
      // The HUD pass also enforces the scene-resolve boundary the present
      // and exposure chain expect; an empty list is enough.
      m_hud.clear ();
      r.draw_hud (m_hud);
      r.end_frame ();

      ++m_frames;
      if (capture) {
        if (m_frames == 40)
          r.request_screenshot (m_output);
        if (m_frames >= 44)
          platform::request_quit ();
      }
    }

  private:
    float m_time = 0.0f;
    int m_frames = 0;
    float m_distance = 55.0f;
    std::string m_output;
    render::DrawList m_list;
    render::DrawList m_hud;
  };
}

int main (int, char**) {
  TreeStudio studio;
  platform::Config config;
  config.title = "Moppe Tree Studio";
  config.fullscreen = false;
  // Captures need blit-readable drawables, exactly like game screenshots.
  config.capture_frames = ::getenv ("MOPPE_STUDIO_OUT") != nullptr;
  return platform::run (studio, config);
}
