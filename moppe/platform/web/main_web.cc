#include <moppe/platform/platform.hh>
#include <moppe/render/webgpu/webgpu_renderer.hh>

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/threading_legacy.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {
  constexpr const char* canvas_selector = "#canvas";

  EM_JS (double, browser_device_pixel_ratio, (), {
    return Math.max (1, window.devicePixelRatio || 1);
  });

  EM_JS (void, browser_set_title, (const char* title), {
    document.title = UTF8ToString (title);
  });

  EM_JS (void, browser_mark_started, (), {
    document.getElementById ("status") ?.remove ();
  });

  EM_JS (void, browser_say, (const char* phrase), {
    if ('speechSynthesis' in window) {
      window.speechSynthesis.speak (
        new SpeechSynthesisUtterance (UTF8ToString (phrase)));
    }
  });

  EM_JS (uintptr_t,
         browser_rasterize_glyph,
         (const char* family,
          float pixel_size,
          float scale,
          unsigned int codepoint,
          int* dimensions,
          float* metrics_out),
         {
           const canvas = document.createElement ("canvas");
           const context =
             canvas.getContext ("2d", { willReadFrequently : true });
           const font = pixel_size + "px " + UTF8ToString (family);
           context.font = font;
           context.textBaseline = "alphabetic";
           const glyph = String.fromCodePoint (codepoint);
           const metrics = context.measureText (glyph);
           const pad = 2;
           const left = Math.ceil (metrics.actualBoundingBoxLeft || 0);
           const right = Math.ceil (metrics.actualBoundingBoxRight || 0);
           const ascent = Math.ceil (metrics.actualBoundingBoxAscent || 0);
           const descent = Math.ceil (metrics.actualBoundingBoxDescent || 0);
           const width = left + right > 0 ? left + right + pad * 2 : 0;
           const height = ascent + descent > 0 ? ascent + descent + pad * 2 : 0;
           HEAP32[dimensions >> 2] = width;
           HEAP32[(dimensions + 4) >> 2] = height;
           HEAPF32[metrics_out >> 2] = -left - pad;
           HEAPF32[(metrics_out + 4) >> 2] = ascent + pad;
           HEAPF32[(metrics_out + 8) >> 2] = metrics.width / scale;
           if (width == 0 || height == 0)
             return 0;

           canvas.width = width;
           canvas.height = height;
           context.font = font;
           context.textBaseline = "alphabetic";
           context.fillStyle = "white";
           context.fillText (glyph, left + pad, ascent + pad);
           const rgba = context.getImageData (0, 0, width, height).data;
           const result = _malloc (width * height);
           for (let i = 0; i < width * height; ++i)
             HEAPU8[result + i] = rgba[i * 4 + 3];
           return result;
         });

  struct WebContext {
    moppe::platform::Game* game = nullptr;
    std::unique_ptr<moppe::render::WebGpuRenderer> renderer;
    double last_frame_ms = 0.0;
    int width_points = 1;
    int height_points = 1;
    float scale = 1.0f;
    bool quit = false;
  };

  std::unique_ptr<WebContext> active_context;

  moppe::platform::Key key_for (const char* code) {
    using moppe::platform::Key;
    if (std::strcmp (code, "ArrowLeft") == 0)
      return Key::Left;
    if (std::strcmp (code, "ArrowRight") == 0)
      return Key::Right;
    if (std::strcmp (code, "ArrowUp") == 0)
      return Key::Up;
    if (std::strcmp (code, "ArrowDown") == 0)
      return Key::Down;
    if (std::strcmp (code, "KeyW") == 0)
      return Key::W;
    if (std::strcmp (code, "KeyA") == 0)
      return Key::A;
    if (std::strcmp (code, "KeyS") == 0)
      return Key::S;
    if (std::strcmp (code, "KeyD") == 0)
      return Key::D;
    if (std::strcmp (code, "Space") == 0)
      return Key::Space;
    if (std::strcmp (code, "Tab") == 0)
      return Key::Tab;
    if (std::strcmp (code, "Escape") == 0)
      return Key::Escape;
    if (std::strcmp (code, "KeyE") == 0)
      return Key::E;
    if (std::strcmp (code, "KeyG") == 0)
      return Key::G;
    if (std::strcmp (code, "KeyM") == 0)
      return Key::M;
    if (std::strcmp (code, "KeyN") == 0)
      return Key::N;
    if (std::strcmp (code, "KeyR") == 0)
      return Key::R;
    if (std::strcmp (code, "KeyT") == 0)
      return Key::T;
    if (std::strcmp (code, "KeyY") == 0)
      return Key::Y;
    if (std::strcmp (code, "Digit1") == 0)
      return Key::One;
    if (std::strcmp (code, "Digit2") == 0)
      return Key::Two;
    if (std::strcmp (code, "Digit3") == 0)
      return Key::Three;
    if (std::strcmp (code, "Digit4") == 0)
      return Key::Four;
    if (std::strcmp (code, "Digit5") == 0)
      return Key::Five;
    if (std::strcmp (code, "Digit6") == 0)
      return Key::Six;
    if (std::strcmp (code, "Digit7") == 0)
      return Key::Seven;
    return Key::Unknown;
  }

  bool keyboard_callback (int event_type,
                          const EmscriptenKeyboardEvent* event,
                          void* data) {
    auto& context = *static_cast<WebContext*> (data);
    const moppe::platform::Key key = key_for (event->code);
    if (key == moppe::platform::Key::Unknown)
      return false;
    const bool down = event_type == EMSCRIPTEN_EVENT_KEYDOWN;
    if (!(down && event->repeat))
      context.game->key (key, down);
    return true;
  }

  bool blur_callback (int, const EmscriptenFocusEvent*, void* data) {
    auto& context = *static_cast<WebContext*> (data);
    using moppe::platform::Key;
    constexpr std::array held_keys = { Key::Left, Key::Right, Key::Up,
                                       Key::Down, Key::W,     Key::A,
                                       Key::S,    Key::D,     Key::Space,
                                       Key::E };
    for (const Key key : held_keys)
      context.game->key (key, false);
    return false;
  }

  moppe::platform::PointerButton pointer_button_for (unsigned short button) {
    using moppe::platform::PointerButton;
    if (button == 2)
      return PointerButton::Secondary;
    if (button == 1)
      return PointerButton::Middle;
    return PointerButton::Primary;
  }

  bool mouse_callback (int event_type,
                       const EmscriptenMouseEvent* event,
                       void* data) {
    auto& context = *static_cast<WebContext*> (data);
    if (event_type == EMSCRIPTEN_EVENT_MOUSEMOVE) {
      context.game->pointer_move (
        event->targetX, event->targetY, event->movementX, event->movementY);
      return true;
    }
    const bool down = event_type == EMSCRIPTEN_EVENT_MOUSEDOWN;
    context.game->pointer_button (
      pointer_button_for (event->button), down, event->targetX, event->targetY);
    if (down && event->button == 0)
      emscripten_request_pointerlock (canvas_selector, true);
    return true;
  }

  bool wheel_callback (int, const EmscriptenWheelEvent* event, void* data) {
    auto& context = *static_cast<WebContext*> (data);
    context.game->pointer_scroll (event->mouse.targetX,
                                  event->mouse.targetY,
                                  static_cast<float> (-event->deltaY));
    return true;
  }

  void resize_canvas (WebContext& context) {
    double css_width = 0.0;
    double css_height = 0.0;
    emscripten_get_element_css_size (canvas_selector, &css_width, &css_height);
    const int width = std::max (1, static_cast<int> (std::lround (css_width)));
    const int height =
      std::max (1, static_cast<int> (std::lround (css_height)));
    const float scale = static_cast<float> (browser_device_pixel_ratio ());
    if (width == context.width_points && height == context.height_points &&
        scale == context.scale)
      return;

    context.width_points = width;
    context.height_points = height;
    context.scale = scale;
    emscripten_set_canvas_element_size (
      canvas_selector,
      std::max (1, static_cast<int> (std::lround (width * scale))),
      std::max (1, static_cast<int> (std::lround (height * scale))));
    context.renderer->resize (width, height, scale);
    context.game->resize (width, height);
  }

  bool animation_frame (double time_ms, void* data) {
    auto& context = *static_cast<WebContext*> (data);
    if (context.quit)
      return false;
    try {
      resize_canvas (context);
      const float elapsed =
        context.last_frame_ms == 0.0
          ? 0.0f
          : static_cast<float> ((time_ms - context.last_frame_ms) / 1000.0);
      context.last_frame_ms = time_ms;
      context.game->tick (std::clamp (elapsed, 0.0f, 0.05f));
      context.game->render (*context.renderer);
      return !context.quit;
    } catch (const std::exception& error) {
      std::cerr << "moppe: browser frame failed: " << error.what ()
                << std::endl;
      return false;
    }
  }

  void start_game (WebContext& context) {
    resize_canvas (context);
    context.game->setup (
      *context.renderer, context.width_points, context.height_points);
    browser_mark_started ();
    emscripten_set_keydown_callback (
      EMSCRIPTEN_EVENT_TARGET_WINDOW, &context, true, keyboard_callback);
    emscripten_set_keyup_callback (
      EMSCRIPTEN_EVENT_TARGET_WINDOW, &context, true, keyboard_callback);
    emscripten_set_blur_callback (
      EMSCRIPTEN_EVENT_TARGET_WINDOW, &context, true, blur_callback);
    emscripten_set_mousedown_callback (
      canvas_selector, &context, true, mouse_callback);
    emscripten_set_mouseup_callback (
      EMSCRIPTEN_EVENT_TARGET_WINDOW, &context, true, mouse_callback);
    emscripten_set_mousemove_callback (
      EMSCRIPTEN_EVENT_TARGET_WINDOW, &context, true, mouse_callback);
    emscripten_set_wheel_callback (
      canvas_selector, &context, true, wheel_callback);
    emscripten_request_animation_frame_loop (animation_frame, &context);
  }

  struct AsyncJob {
    void (*work) (void*);
    void (*done) (void*);
    std::shared_ptr<void> context;
    void* raw_context;
  };

  void complete_async_job (int raw_job) {
    std::unique_ptr<AsyncJob> job (
      reinterpret_cast<AsyncJob*> (static_cast<uintptr_t> (raw_job)));
    job->done (job->raw_context);
  }
}

namespace moppe::platform {
  int run (Game& game, const Config& config) {
    std::filesystem::create_directories ("/cache");
    browser_set_title (config.title.c_str ());

    active_context = std::make_unique<WebContext> ();
    active_context->game = &game;
    active_context->renderer = std::make_unique<render::WebGpuRenderer> (
      canvas_selector, config.width, config.height, 1.0f);
    WebContext* context = active_context.get ();
    context->renderer->initialize (
      [context] (bool success, const std::string& error) {
        if (!success) {
          std::cerr << "moppe: WebGPU initialization failed: " << error
                    << std::endl;
          return;
        }
        start_game (*context);
      });
    // WebGPU adapter/device acquisition is asynchronous. Keep main's stack
    // alive as the browser takes ownership of the run loop; both terminal
    // programs intentionally construct their Game object on that stack.
    emscripten_exit_with_live_runtime ();
  }

  void request_quit () {
    if (active_context)
      active_context->quit = true;
  }

  void set_window_title (const std::string& title) {
    browser_set_title (title.c_str ());
  }

  std::string asset_path (const std::string& relative) {
    return "/" + relative;
  }

  std::string executable_build_id () {
    return "moppe-web";
  }

  std::string cache_path (const std::string& relative) {
    return relative.empty () ? "/cache" : "/cache/" + relative;
  }

  double now () {
    return emscripten_get_now () / 1000.0;
  }

  std::unique_ptr<terrain::FieldEvaluator> create_field_evaluator () {
    return {};
  }

  std::unique_ptr<terrain::StreamPowerEvolutionBackend>
  create_stream_power_evolution_backend () {
    return {};
  }

  Insets safe_insets () {
    return {};
  }

  void say (const std::string& phrase) {
    browser_say (phrase.c_str ());
  }

  void async (void (*work) (void*),
              void (*done) (void*),
              std::shared_ptr<void> context) {
    auto* job = new AsyncJob { work, done, std::move (context), nullptr };
    job->raw_context = job->context.get ();
    std::thread ([job] {
      job->work (job->raw_context);
      emscripten_async_run_in_main_runtime_thread (
        EM_FUNC_SIG_VI,
        complete_async_job,
        static_cast<int> (reinterpret_cast<uintptr_t> (job)));
    }).detach ();
  }

  bool rasterize_glyph (const char* font_family,
                        float point_size,
                        float scale,
                        unsigned int codepoint,
                        GlyphBitmap& out) {
    int dimensions[2] {};
    float metrics[3] {};
    auto* pixels = reinterpret_cast<unsigned char*> (browser_rasterize_glyph (
      font_family, point_size * scale, scale, codepoint, dimensions, metrics));
    out.width = dimensions[0];
    out.height = dimensions[1];
    out.bearing_x = metrics[0];
    out.bearing_y = metrics[1];
    out.advance = metrics[2];
    if (out.width > 0 && out.height > 0) {
      if (!pixels)
        return false;
      out.pixels.assign (
        pixels, pixels + static_cast<std::size_t> (out.width) * out.height);
    } else {
      out.pixels.clear ();
    }
    std::free (pixels);
    return true;
  }
}
