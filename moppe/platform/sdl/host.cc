// The game in an SDL3 window, on every desktop SDL carries: the NHAL
// renderer draws into the window through the platform's device (sdl.hh),
// and the keyboard, mouse, and first gamepad drive the game. The window is
// sized in points; the drawable is its pixels.

#include <moppe/environment.hh>
#include <moppe/nhal/renderer/nhal_renderer.hh>
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
  using moppe::platform::Game;
  using moppe::platform::Key;
  using moppe::platform::PointerButton;

  std::atomic<bool> quitting = false;
  SDL_Window* active_window = nullptr;

  // The game's keys by what the key types, so letters follow the layout.
  Key map_key (const SDL_KeyboardEvent& key) {
    switch (key.scancode) {
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
    case SDLK_W: return Key::W;
    case SDLK_A: return Key::A;
    case SDLK_S: return Key::S;
    case SDLK_D: return Key::D;
    case SDLK_E: return Key::E;
    case SDLK_F: return Key::Mount;
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

  // Where the QWERTY W, A, S, and D keys sit, whatever they type.
  Key physical_key (SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_W: return Key::PhysicalW;
    case SDL_SCANCODE_A: return Key::PhysicalA;
    case SDL_SCANCODE_S: return Key::PhysicalS;
    case SDL_SCANCODE_D: return Key::PhysicalD;
    default: return Key::Unknown;
    }
  }

  float axis (Sint16 value) {
    const float v = std::clamp (float (value) / 32767.0f, -1.0f, 1.0f);
    return std::abs (v) < 0.15f ? 0.0f : v;
  }

  // The gamepad as the other hosts read a controller: the left stick (or
  // the D-pad) drives and steers, the right trigger boosts; A deploys the
  // glider or restarts, B mounts, X cycles the camera, Y boosts or flares,
  // and the D-pad also presses the arrow keys for menus.
  class Pad {
  public:
    explicit Pad (Game& game) : m_game (game) {}

    ~Pad () {
      if (m_pad)
        SDL_CloseGamepad (m_pad);
    }

    void added (SDL_JoystickID id) {
      if (!m_pad)
        m_pad = SDL_OpenGamepad (id);
    }

    void removed (SDL_JoystickID id) {
      if (!m_pad || SDL_GetGamepadID (m_pad) != id)
        return;
      release ();
      SDL_CloseGamepad (m_pad);
      m_pad = nullptr;
    }

    void poll () {
      if (!m_pad)
        return;
      auto held = [&] (SDL_GamepadButton b) {
        return SDL_GetGamepadButton (m_pad, b);
      };
      const float dpad_x = (held (SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1.0f : 0)
                           - (held (SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? 1.0f : 0);
      const float dpad_y = (held (SDL_GAMEPAD_BUTTON_DPAD_UP) ? 1.0f : 0)
                           - (held (SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? 1.0f : 0);
      const float stick_x =
        axis (SDL_GetGamepadAxis (m_pad, SDL_GAMEPAD_AXIS_LEFTX));
      // SDL's sticks point down for positive y; driving forward is up.
      const float stick_y =
        -axis (SDL_GetGamepadAxis (m_pad, SDL_GAMEPAD_AXIS_LEFTY));
      const float trigger = std::max (
        0.0f, float (SDL_GetGamepadAxis (m_pad,
                                         SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
                / 32767.0f);
      moppe::platform::ControlState controls;
      controls.steer = stick_x != 0 ? stick_x : dpad_x;
      controls.drive = stick_y != 0 ? stick_y : dpad_y;
      controls.boost = std::max (
        trigger, held (SDL_GAMEPAD_BUTTON_NORTH) ? 1.0f : 0.0f);
      edge (0, held (SDL_GAMEPAD_BUTTON_SOUTH), Key::E);
      edge (1, held (SDL_GAMEPAD_BUTTON_SOUTH), Key::Restart);
      edge (2, held (SDL_GAMEPAD_BUTTON_EAST), Key::Mount);
      edge (3, held (SDL_GAMEPAD_BUTTON_WEST), Key::Tab);
      edge (4, held (SDL_GAMEPAD_BUTTON_DPAD_LEFT), Key::Left);
      edge (5, held (SDL_GAMEPAD_BUTTON_DPAD_RIGHT), Key::Right);
      edge (6, held (SDL_GAMEPAD_BUTTON_DPAD_UP), Key::Up);
      edge (7, held (SDL_GAMEPAD_BUTTON_DPAD_DOWN), Key::Down);
      edge (8, held (SDL_GAMEPAD_BUTTON_NORTH), Key::Space);
      m_game.controls (controls);
    }

  private:
    static constexpr Key keys[] = { Key::E,   Key::Restart, Key::Mount,
                                    Key::Tab, Key::Left,    Key::Right,
                                    Key::Up,  Key::Down,    Key::Space };

    void edge (int index, bool down, Key key) {
      if (m_buttons[index] == down)
        return;
      m_buttons[index] = down;
      m_game.key (key, down);
    }

    // A controller that disappears lets go of everything it held.
    void release () {
      for (int i = 0; i < 9; ++i)
        if (m_buttons[i]) {
          m_game.key (keys[i], false);
          m_buttons[i] = false;
        }
      m_game.controls ({});
    }

    Game& m_game;
    SDL_Gamepad* m_pad = nullptr;
    bool m_buttons[9] {};
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
    const float scale =
      width > 0 ? float (pixels_wide) / float (width) : 1.0f;
    auto device = sdl::create_device (window, std::uint32_t (pixels_wide),
                                      std::uint32_t (pixels_high));
    std::cerr << "moppe: NHAL on " << device->info ().backend << ", "
              << device->info ().adapter << ", " << pixels_wide << "x"
              << pixels_high << " pixels at " << scale << " per point"
              << std::endl;
    nhal::Device& surface_device = *device;
    std::unique_ptr<render::Renderer> renderer = nhal::create_renderer (
      std::move (device), sdl::world_shaders (), scale);
    game.setup (*renderer, renderer->width_pts (), renderer->height_pts ());

    Pad pad (game);
    Held held;
    float pointer_x = 0, pointer_y = 0;
    auto last = std::chrono::steady_clock::now ();
    while (!quitting) {
      SDL_Event event;
      while (SDL_PollEvent (&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT: quitting = true; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          surface_device.resize_surface (std::uint32_t (event.window.data1),
                                         std::uint32_t (event.window.data2));
          game.resize (renderer->width_pts (), renderer->height_pts ());
          break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
          held.release (game, pointer_x, pointer_y);
          break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
          const bool down = event.type == SDL_EVENT_KEY_DOWN;
          if (down && event.key.repeat)
            break;
          if (const Key physical = physical_key (event.key.scancode);
              physical != Key::Unknown)
            game.key (physical, down);
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
      pad.poll ();
      const auto now = std::chrono::steady_clock::now ();
      const double dt = std::chrono::duration<double> (now - last).count ();
      last = now;
      game.tick (float (std::clamp (dt, 0.0, 0.05)));
      game.render (*renderer);
    }
    // The game keeps the renderer's textures and meshes until main returns,
    // so the renderer outlives this function, as on the Mac; the GPU
    // finishes first, and the process's end releases the rest.
    surface_device.wait_idle ();
    (void)renderer.release ();
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
