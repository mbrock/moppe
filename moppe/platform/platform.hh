#ifndef MOPPE_PLATFORM_HH
#define MOPPE_PLATFORM_HH

#include <moppe/render/renderer.hh>

#include <string>

namespace moppe {
namespace platform {
  // Unified key codes: removes the old ASCII/GLUT_KEY_* numeric
  // collision ('d' == GLUT_KEY_LEFT == 100).  Only keys the game
  // actually uses.
  enum class Key {
    Unknown,
    Left, Right, Up, Down,
    W, A, S, D,
    Space, Tab, Escape, R,
    Five, Seven
  };

  struct Config {
    std::string title;
    int width = 1280;             // ignored when fullscreen
    int height = 800;
    bool fullscreen = false;
  };

  // Implemented by the game; driven by the per-OS run loop.
  //
  // Lifecycle: setup() must return quickly -- long world generation
  // belongs on a background thread (platform::async), with render()
  // drawing a loading screen until it finishes.  tick() receives
  // real dt clamped to 0.05 s.  key() edges are autorepeat-filtered;
  // releases are synthesized on focus loss / touch cancellation so
  // held inputs can't stick.
  class Game {
  public:
    virtual ~Game () {}
    virtual void setup (render::Renderer& r, int w_pts, int h_pts) = 0;
    virtual void resize (int w_pts, int h_pts) { (void) w_pts; (void) h_pts; }
    virtual void tick (float dt) = 0;
    virtual void render (render::Renderer& r) = 0;
    virtual void key (Key k, bool down) { (void) k; (void) down; }
  };

  // Runs the platform main loop; returns the process exit code.
  int run (Game& game, const Config& config);

  // Ask the run loop to quit (no-op on iOS, where apps don't exit).
  void request_quit ();

  // Resolve an asset-relative path ("textures/grass2.tga") to an
  // absolute path: the app bundle on Apple platforms, or the
  // executable/source directory for loose development builds.
  std::string asset_path (const std::string& relative);

  // Monotonic time in seconds; never wall-clock.
  double now ();

  // Screen areas covered by notches / home indicators, in points.
  // Zero on macOS.  The HUD and touch zones stay inside these.
  struct Insets {
    float left = 0, top = 0, right = 0, bottom = 0;
  };
  Insets safe_insets ();

  // Speak a phrase, asynchronously and best-effort.
  void say (const std::string& phrase);

  // Run work on a background (user-initiated QoS) queue, then call
  // done on the main/render thread.
  void async (void (*work) (void*), void (*done) (void*), void* ctx);

  // Rasterize a glyph run for the font atlas: platform-specific text
  // rendering behind a portable call.  Returns an 8-bit coverage
  // bitmap; caller owns interpretation.  Defined in render/text.cc
  // terms -- see text.hh for the atlas builder that consumes this.
  struct GlyphBitmap {
    int width = 0, height = 0;
    int bearing_x = 0, bearing_y = 0;  // from origin to bitmap top-left
    float advance = 0;
    std::basic_string<unsigned char> pixels;  // width*height coverage
  };
  bool rasterize_glyph (const char* font_family, float point_size,
			float scale, unsigned int codepoint,
			GlyphBitmap& out);
}
}

#endif
