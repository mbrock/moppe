// The game in an SDL3 window, on every desktop SDL carries: the NHAL
// renderer draws into the window through the platform's device (sdl.hh),
// and the keyboard, mouse, and first gamepad drive the game. The window is
// sized in points; the drawable is its pixels.

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
  int run (Game& game, const Config& config) {
    if (!SDL_Init (SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
      throw std::runtime_error (std::string ("SDL_Init: ") + SDL_GetError ());
    // Automated runs leave the active app in front.
    if (!config.activate)
      SDL_SetHint (SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    SDL_WindowFlags flags = SDL_WindowFlags (sdl::window_flags ())
                            | SDL_WINDOW_RESIZABLE
                            | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.fullscreen)
      flags |= SDL_WINDOW_FULLSCREEN;
    SDL_Window* window =
      SDL_CreateWindow (config.title.empty () ? "Moppe" : config.title.c_str (),
                        config.width, config.height, flags);
    if (!window)
      throw std::runtime_error (std::string ("SDL_CreateWindow: ")
                                + SDL_GetError ());
    active_window = window;

    int width = 0, height = 0, pixels_wide = 0, pixels_high = 0;
    SDL_GetWindowSize (window, &width, &height);
    pixel_size (window, pixels_wide, pixels_high);
    auto device = sdl::create_device (window, std::uint32_t (pixels_wide),
                                      std::uint32_t (pixels_high));
    // The device may choose its own drawable (the Xbox's is 4K behind a
    // 1080p window); the HUD's points follow the window, and resizes keep
    // the device's ratio to the window's pixels.
    const float scale =
      width > 0 ? float (device->surface_width ()) / float (width) : 1.0f;
    const float oversample =
      pixels_wide > 0
        ? float (device->surface_width ()) / float (pixels_wide)
        : 1.0f;
    std::cerr << "moppe: NHAL on " << device->info ().backend << ", "
              << device->info ().adapter << ", "
              << device->surface_width () << "x"
              << device->surface_height () << " pixels at " << scale
              << " per point" << std::endl;
    nhal::Device& surface_device = *device;
    // The game keeps the renderer's textures and meshes until main returns,
    // so the renderer outlives this function, even when an error leaves it;
    // the process's end releases it.
    render::Renderer& renderer =
      *nhal::create_renderer (std::move (device), sdl::world_shaders (), scale)
         .release ();
    game.setup (renderer, renderer.width_pts (), renderer.height_pts ());

    Pad pad (game);
    Held held;
    // MOPPE_CONTROL_FILE names a remote-control file (input.hh).
    std::unique_ptr<RemoteControl> remote;
    if (const char* path = moppe::environment ("MOPPE_CONTROL_FILE"))
      remote = std::make_unique<RemoteControl> (game, path);
    // MOPPE_FPS_REPORT=1 logs the frame rate every ten seconds.
    const char* fps_report = moppe::environment ("MOPPE_FPS_REPORT");
    const bool report_fps = fps_report && *fps_report && *fps_report != '0';
    float pointer_x = 0, pointer_y = 0;
    // The simulation steps by when frames appear, where the device can say
    // (nhal::Device::next_frame_timing); MOPPE_FRAME_CLOCK=host keeps the
    // loop's own time.
    const char* frame_clock = moppe::environment ("MOPPE_FRAME_CLOCK");
    const bool display_clock =
      !(frame_clock && std::string (frame_clock) == "host");
    double last_display = 0;
    bool announced_display = false;
    const auto start = std::chrono::steady_clock::now ();
    auto last = start;
    auto report_start = last;
    long report_frames = 0, report_displayed = 0;
    double slowest = 0, shortest_step = 1, longest_step = 0;
    while (!quitting) {
      SDL_Event event;
      while (SDL_PollEvent (&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT: quitting = true; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          surface_device.resize_surface (
            std::uint32_t (std::lround (event.window.data1 * oversample)),
            std::uint32_t (std::lround (event.window.data2 * oversample)));
          game.resize (renderer.width_pts (), renderer.height_pts ());
          break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
          held.release (game, pointer_x, pointer_y);
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
            held.keys.insert (k);
          else
            held.keys.erase (k);
          game.key (k, down);
          break;
        }
        case SDL_EVENT_MOUSE_MOTION:
          pointer_x = event.motion.x;
          pointer_y = event.motion.y;
          game.pointer_move (pointer_x, pointer_y, event.motion.xrel,
                             event.motion.yrel);
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
          const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
          const PointerButton button =
            event.button.button == SDL_BUTTON_RIGHT    ? PointerButton::Secondary
            : event.button.button == SDL_BUTTON_MIDDLE ? PointerButton::Middle
                                                       : PointerButton::Primary;
          pointer_x = event.button.x;
          pointer_y = event.button.y;
          if (down)
            held.buttons.insert (button);
          else
            held.buttons.erase (button);
          game.pointer_button (button, down, pointer_x, pointer_y);
          break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
          game.pointer_scroll (pointer_x, pointer_y, event.wheel.y);
          break;
        case SDL_EVENT_GAMEPAD_ADDED: pad.added (event.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_REMOVED:
          pad.removed (event.gdevice.which);
          break;
        default: break;
        }
      }
      sdl::run_main_thread_tasks ();
      const ControlState held_controls = pad.poll ();
      const auto now = std::chrono::steady_clock::now ();
      if (remote)
        remote->poll (std::chrono::duration<double> (now - start).count ());
      if (remote && remote->controls ())
        game.controls (*remote->controls ());
      else if (pad.connected ())
        game.controls (held_controls);
      const double wall = std::chrono::duration<double> (now - last).count ();
      last = now;
      double dt = wall;
      if (display_clock) {
        const nhal::FrameTiming timing = surface_device.next_frame_timing ();
        if (timing.predicted) {
          if (last_display > 0 && timing.display_seconds > last_display) {
            dt = timing.display_seconds - last_display;
            ++report_displayed;
            if (!announced_display) {
              announced_display = true;
              std::cerr << "moppe: stepping by display time, "
                        << timing.refresh_seconds * 1000 << " ms refresh"
                        << std::endl;
            }
          }
          last_display = timing.display_seconds;
        } else {
          last_display = 0;
        }
      }
      shortest_step = std::min (shortest_step, dt);
      longest_step = std::max (longest_step, dt);
      game.tick (float (std::clamp (dt, 0.0, 0.05)));
      game.render (renderer);
      sdl::frame_rendered ();
      if (report_fps) {
        slowest = std::max (slowest, wall);
        const double span =
          std::chrono::duration<double> (now - report_start).count ();
        if (++report_frames > 1 && span >= 10.0) {
          std::cerr << "moppe: " << report_frames / span
                    << " fps, slowest frame " << slowest * 1000
                    << " ms, steps " << shortest_step * 1000 << "-"
                    << longest_step * 1000 << " ms, "
                    << report_displayed << " of " << report_frames
                    << " by display time" << std::endl;
          report_start = now;
          report_frames = report_displayed = 0;
          slowest = longest_step = 0;
          shortest_step = 1;
        }
      }
    }
    surface_device.wait_idle ();
    active_window = nullptr;
    return 0;
  }

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
