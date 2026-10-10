// The game in an SDL3 window, on every desktop SDL carries and in the
// browser, where the window is the page's canvas: the NHAL renderer draws
// into the window through the platform's device (sdl.hh), and the keyboard,
// mouse, and first gamepad drive the game. The window is sized in points;
// the drawable is its pixels.

#include <moppe/environment.hh>
#include <moppe/nhal/renderer/nhal_renderer.hh>
#include <moppe/platform/input.hh>
#include <moppe/platform/platform.hh>
#include <moppe/platform/sdl/sdl.hh>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {
  using moppe::platform::ControlState;
  using moppe::platform::Game;
  using moppe::platform::Key;
  using moppe::platform::PointerButton;
  using moppe::platform::RemoteControl;

  std::atomic<bool> quitting = false;
  SDL_Window* active_window = nullptr;

  // The game's keys. The left hand's cluster goes by where the QWERTY
  // W, A, S, D, E, and F keys sit, so it stays under the hand on any
  // layout; the rest go by what the key types, so their letters follow
  // the layout.
  Key map_key (const SDL_KeyboardEvent& key) {
    switch (key.scancode) {
    case SDL_SCANCODE_W: return Key::W;
    case SDL_SCANCODE_A: return Key::A;
    case SDL_SCANCODE_S: return Key::S;
    case SDL_SCANCODE_D: return Key::D;
    case SDL_SCANCODE_E: return Key::E;
    case SDL_SCANCODE_F: return Key::Mount;
    case SDL_SCANCODE_LEFT: return Key::Left;
    case SDL_SCANCODE_RIGHT: return Key::Right;
    case SDL_SCANCODE_UP: return Key::Up;
    case SDL_SCANCODE_DOWN: return Key::Down;
    case SDL_SCANCODE_TAB: return Key::Tab;
    case SDL_SCANCODE_SPACE: return Key::Space;
    case SDL_SCANCODE_ESCAPE: return Key::Escape;
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: return Key::Shift;
    default: break;
    }
    switch (key.key) {
    case SDLK_P: return Key::Screenshot;
    case SDLK_G: return Key::G;
    case SDLK_H: return Key::H;
    case SDLK_M: return Key::M;
    case SDLK_N: return Key::N;
    case SDLK_R: return Key::R;
    case SDLK_T: return Key::T;
    case SDLK_Y: return Key::Y;
    case SDLK_1: return Key::One;
    case SDLK_2: return Key::Two;
    case SDLK_3: return Key::Three;
    case SDLK_4: return Key::Four;
    case SDLK_5: return Key::Five;
    case SDLK_6: return Key::Six;
    case SDLK_7: return Key::Seven;
    default: return Key::Unknown;
    }
  }

  // The first gamepad, read into the shared mapping (input.hh).
  class Pad {
  public:
    explicit Pad (Game& game) : m_mapper (game) {}

    ~Pad () {
      if (m_pad)
        SDL_CloseGamepad (m_pad);
    }

    void added (SDL_JoystickID id) {
      if (m_pad)
        return;
      m_pad = SDL_OpenGamepad (id);
      if (const char* name = m_pad ? SDL_GetGamepadName (m_pad) : nullptr)
        std::cerr << "moppe: gamepad connected: " << name << std::endl;
    }

    void removed (SDL_JoystickID id) {
      if (!m_pad || SDL_GetGamepadID (m_pad) != id)
        return;
      m_mapper.release ();
      SDL_CloseGamepad (m_pad);
      m_pad = nullptr;
    }

    bool connected () const { return m_pad != nullptr; }

    // The controls the sticks and triggers ask for, after pressing and
    // releasing the buttons' keys.
    moppe::platform::ControlState poll () {
      if (!m_pad)
        return {};
      auto held = [&] (SDL_GamepadButton b) {
        return SDL_GetGamepadButton (m_pad, b);
      };
      auto axis = [&] (SDL_GamepadAxis a) {
        return float (SDL_GetGamepadAxis (m_pad, a)) / 32767.0f;
      };
      moppe::platform::GamepadReading r;
      r.left_x = axis (SDL_GAMEPAD_AXIS_LEFTX);
      // SDL's sticks point down for positive y; driving forward is up.
      r.left_y = -axis (SDL_GAMEPAD_AXIS_LEFTY);
      r.right_x = axis (SDL_GAMEPAD_AXIS_RIGHTX);
      r.right_y = -axis (SDL_GAMEPAD_AXIS_RIGHTY);
      r.right_trigger = axis (SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
      r.a = held (SDL_GAMEPAD_BUTTON_SOUTH);
      r.b = held (SDL_GAMEPAD_BUTTON_EAST);
      r.x = held (SDL_GAMEPAD_BUTTON_WEST);
      r.y = held (SDL_GAMEPAD_BUTTON_NORTH);
      r.dpad_left = held (SDL_GAMEPAD_BUTTON_DPAD_LEFT);
      r.dpad_right = held (SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
      r.dpad_up = held (SDL_GAMEPAD_BUTTON_DPAD_UP);
      r.dpad_down = held (SDL_GAMEPAD_BUTTON_DPAD_DOWN);
      return m_mapper.map (r);
    }

  private:
    moppe::platform::GamepadMapper m_mapper;
    SDL_Gamepad* m_pad = nullptr;
  };

  // Keys and buttons held, so losing focus can let go of them all.
  struct Held {
    std::set<Key> keys;
    std::set<PointerButton> buttons;

    void release (Game& game, float x, float y) {
      for (Key k : keys)
        game.key (k, false);
      keys.clear ();
      for (PointerButton b : buttons)
        game.pointer_button (b, false, x, y);
      buttons.clear ();
    }
  };

  void pixel_size (SDL_Window* window, int& width, int& height) {
    SDL_GetWindowSizeInPixels (window, &width, &height);
  }
}

namespace moppe::platform {
  namespace {
    // The window, the renderer drawing into it, and the loop's state from
    // one turn to the next.
    class Host {
    public:
      Host (Game& game, const Config& config) : m_game (game), m_pad (game) {
        if (!SDL_Init (SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
          throw std::runtime_error (std::string ("SDL_Init: ")
                                    + SDL_GetError ());
        // Automated runs leave the active app in front.
        if (!config.activate)
          SDL_SetHint (SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
        SDL_WindowFlags flags = SDL_WindowFlags (sdl::window_flags ())
                                | SDL_WINDOW_RESIZABLE
                                | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (config.fullscreen)
          flags |= SDL_WINDOW_FULLSCREEN;
        m_window = SDL_CreateWindow (
          config.title.empty () ? "Moppe" : config.title.c_str (),
          config.width, config.height, flags);
        if (!m_window)
          throw std::runtime_error (std::string ("SDL_CreateWindow: ")
                                    + SDL_GetError ());
        active_window = m_window;

        int width = 0, height = 0, pixels_wide = 0, pixels_high = 0;
        SDL_GetWindowSize (m_window, &width, &height);
        pixel_size (m_window, pixels_wide, pixels_high);
        auto device = sdl::create_device (m_window,
                                          std::uint32_t (pixels_wide),
                                          std::uint32_t (pixels_high));
        // The device may choose its own drawable (the Xbox's is 4K behind a
        // 1080p window); the HUD's points follow the window, and resizes
        // keep the device's ratio to the window's pixels. A browser's
        // canvas has no size until the page lays it out, only a density.
        const float scale =
          width > 0 && device->surface_width () > 0
            ? float (device->surface_width ()) / float (width)
            : SDL_GetWindowPixelDensity (m_window);
        m_oversample = pixels_wide > 0 ? float (device->surface_width ())
                                           / float (pixels_wide)
                                       : 1.0f;
        std::cerr << "moppe: NHAL on " << device->info ().backend << ", "
                  << device->info ().adapter << ", "
                  << device->surface_width () << "x"
                  << device->surface_height () << " pixels at " << scale
                  << " per point" << std::endl;
        m_device = device.get ();
        // The game keeps the renderer's textures and meshes until main
        // returns, so the renderer outlives the host, even when an error
        // ends it; the process's end releases it.
        m_renderer = nhal::create_renderer (std::move (device),
                                            sdl::world_shaders (), scale)
                       .release ();
        m_game.setup (*m_renderer, m_renderer->width_pts (),
                      m_renderer->height_pts ());

        // MOPPE_CONTROL_FILE names a remote-control file (input.hh).
        if (const char* path = moppe::environment ("MOPPE_CONTROL_FILE"))
          m_remote = std::make_unique<RemoteControl> (m_game, path);
        // MOPPE_FPS_REPORT=1 logs the frame rate every ten seconds.
        const char* fps_report = moppe::environment ("MOPPE_FPS_REPORT");
        m_report_fps = fps_report && *fps_report && *fps_report != '0';
        // The simulation steps by when frames appear, where the device can
        // say (nhal::Device::next_frame_timing); MOPPE_FRAME_CLOCK=host
        // keeps the loop's own time.
        const char* frame_clock = moppe::environment ("MOPPE_FRAME_CLOCK");
        m_display_clock =
          !(frame_clock && std::string (frame_clock) == "host");
        m_start = m_last = m_report_start = std::chrono::steady_clock::now ();
      }

      ~Host () {
        m_device->wait_idle ();
        active_window = nullptr;
      }

      // One turn of the loop: the events since the last, a step of the
      // game, and a frame.
      void turn () {
        SDL_Event event;
        while (SDL_PollEvent (&event))
          handle (event);
        sdl::run_main_thread_tasks ();
        const ControlState held_controls = m_pad.poll ();
        const auto now = std::chrono::steady_clock::now ();
        if (m_remote)
          m_remote->poll (
            std::chrono::duration<double> (now - m_start).count ());
        if (m_remote && m_remote->controls ())
          m_game.controls (*m_remote->controls ());
        else if (m_pad.connected ())
          m_game.controls (held_controls);
        const double wall =
          std::chrono::duration<double> (now - m_last).count ();
        m_last = now;
        double dt = wall;
        if (m_display_clock) {
          const nhal::FrameTiming timing = m_device->next_frame_timing ();
          if (timing.predicted) {
            if (m_last_display > 0
                && timing.display_seconds > m_last_display) {
              dt = timing.display_seconds - m_last_display;
              ++m_report_displayed;
              if (!m_announced_display) {
                m_announced_display = true;
                std::cerr << "moppe: stepping by display time, "
                          << timing.refresh_seconds * 1000 << " ms refresh"
                          << std::endl;
              }
            }
            m_last_display = timing.display_seconds;
          } else {
            m_last_display = 0;
          }
        }
        m_shortest_step = std::min (m_shortest_step, dt);
        m_longest_step = std::max (m_longest_step, dt);
        m_game.tick (float (std::clamp (dt, 0.0, 0.05)));
        m_game.render (*m_renderer);
        sdl::frame_rendered ();
        if (m_report_fps)
          report (now, wall);
      }

    private:
      void handle (const SDL_Event& event) {
        switch (event.type) {
        case SDL_EVENT_QUIT: quitting = true; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          m_device->resize_surface (
            std::uint32_t (std::lround (event.window.data1 * m_oversample)),
            std::uint32_t (std::lround (event.window.data2 * m_oversample)));
          m_game.resize (m_renderer->width_pts (), m_renderer->height_pts ());
          break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
          m_held.release (m_game, m_pointer_x, m_pointer_y);
          break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
          const bool down = event.type == SDL_EVENT_KEY_DOWN;
          if (down && event.key.repeat)
            break;
          const Key k = map_key (event.key);
          if (k == Key::Unknown)
            break;
          if (down)
            m_held.keys.insert (k);
          else
            m_held.keys.erase (k);
          m_game.key (k, down);
          break;
        }
        case SDL_EVENT_MOUSE_MOTION:
          m_pointer_x = event.motion.x;
          m_pointer_y = event.motion.y;
          m_game.pointer_move (m_pointer_x, m_pointer_y, event.motion.xrel,
                               event.motion.yrel);
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
          const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
          const Uint8 pressed = event.button.button;
          const PointerButton button =
            pressed == SDL_BUTTON_RIGHT    ? PointerButton::Secondary
            : pressed == SDL_BUTTON_MIDDLE ? PointerButton::Middle
                                           : PointerButton::Primary;
          m_pointer_x = event.button.x;
          m_pointer_y = event.button.y;
          if (down)
            m_held.buttons.insert (button);
          else
            m_held.buttons.erase (button);
          m_game.pointer_button (button, down, m_pointer_x, m_pointer_y);
          break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
          m_game.pointer_scroll (m_pointer_x, m_pointer_y, event.wheel.y);
          break;
        case SDL_EVENT_GAMEPAD_ADDED: m_pad.added (event.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_REMOVED:
          m_pad.removed (event.gdevice.which);
          break;
        default: break;
        }
      }

      void report (std::chrono::steady_clock::time_point now, double wall) {
        m_slowest = std::max (m_slowest, wall);
        const double span =
          std::chrono::duration<double> (now - m_report_start).count ();
        if (++m_report_frames > 1 && span >= 10.0) {
          std::cerr << "moppe: " << m_report_frames / span
                    << " fps, slowest frame " << m_slowest * 1000
                    << " ms, steps " << m_shortest_step * 1000 << "-"
                    << m_longest_step * 1000 << " ms, "
                    << m_report_displayed << " of " << m_report_frames
                    << " by display time" << std::endl;
          m_report_start = now;
          m_report_frames = m_report_displayed = 0;
          m_slowest = m_longest_step = 0;
          m_shortest_step = 1;
        }
      }

      Game& m_game;
      SDL_Window* m_window = nullptr;
      nhal::Device* m_device = nullptr;
      render::Renderer* m_renderer = nullptr;
      float m_oversample = 1.0f;
      Pad m_pad;
      Held m_held;
      std::unique_ptr<RemoteControl> m_remote;
      float m_pointer_x = 0, m_pointer_y = 0;
      bool m_display_clock = true;
      double m_last_display = 0;
      bool m_announced_display = false;
      std::chrono::steady_clock::time_point m_start, m_last, m_report_start;
      bool m_report_fps = false;
      long m_report_frames = 0, m_report_displayed = 0;
      double m_slowest = 0, m_shortest_step = 1, m_longest_step = 0;
    };
  }

#ifdef __EMSCRIPTEN__
  namespace {
    // Says what stopped the game where the player sees it (pre.js).
    void fail (const std::exception& error) {
      std::cerr << "moppe: " << error.what () << std::endl;
      EM_ASM ({ Module['moppeFail'] (UTF8ToString ($0)); }, error.what ());
    }
  }

  // A browser's loop is the page's: it calls for a turn at each animation
  // frame, and run returns once that is arranged, leaving the host (and the
  // caller's game) to live as long as the page.
  int run (Game& game, const Config& config) {
    try {
      Host* host = new Host (game, config);
      emscripten_set_main_loop_arg (
        [] (void* state) {
          if (quitting)
            return;
          try {
            static_cast<Host*> (state)->turn ();
          } catch (const std::exception& e) {
            fail (e);
            quitting = true;
            emscripten_cancel_main_loop ();
          }
        },
        host, 0, false);
    } catch (const std::exception& e) {
      fail (e);
      return -1;
    }
    return 0;
  }
#else
  int run (Game& game, const Config& config) {
    Host host (game, config);
    while (!quitting)
      host.turn ();
    return 0;
  }
#endif

  void request_quit () {
    quitting = true;
  }

  void set_window_title (const std::string& title) {
    if (active_window)
      SDL_SetWindowTitle (active_window, title.c_str ());
  }

  void set_pointer_captured (bool captured) {
    if (active_window)
      SDL_SetWindowRelativeMouseMode (active_window, captured);
  }
}
