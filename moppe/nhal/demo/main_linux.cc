// The NHAL demo on Linux: an SDL3 window presenting through Vulkan.
// `--capture PATH --frames N` renders N frames without a window (the
// device's own images stand in for the swapchain), writes the last one as
// an uncompressed TGA, and quits; `--window` keeps the window for it.
#include <moppe/nhal/demo/scene.hh>
#include <moppe/nhal/vulkan/vulkan_device.hh>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace moppe::nhal;

namespace {
  std::vector<std::uint32_t> read_spirv (const std::string& path) {
    std::ifstream file (path, std::ios::binary | std::ios::ate);
    if (!file)
      throw std::runtime_error ("cannot read " + path);
    const std::streamsize size = file.tellg ();
    std::vector<std::uint32_t> words (std::size_t (size) / 4);
    file.seekg (0);
    file.read (reinterpret_cast<char*> (words.data ()), size);
    return words;
  }

  // Uncompressed 32-bit TGA with a top-left origin, from BGRA or RGBA.
  void write_tga (const Capture& capture, const std::string& path) {
    FILE* file = std::fopen (path.c_str (), "wb");
    if (!file)
      return;
    unsigned char header[18] {};
    header[2] = 2;
    header[12] = capture.width & 255;
    header[13] = capture.width >> 8;
    header[14] = capture.height & 255;
    header[15] = capture.height >> 8;
    header[16] = 32;
    header[17] = 0x28;
    std::fwrite (header, 1, sizeof header, file);
    const bool rgba = capture.format == Format::rgba8_unorm;
    std::vector<unsigned char> row (std::size_t (capture.width) * 4);
    for (std::uint32_t y = 0; y < capture.height; ++y) {
      std::memcpy (row.data (),
                   capture.pixels.data () + std::size_t (y) * capture.row_bytes,
                   row.size ());
      if (rgba)
        for (std::size_t x = 0; x < row.size (); x += 4)
          std::swap (row[x], row[x + 2]);
      std::fwrite (row.data (), 1, row.size (), file);
    }
    std::fclose (file);
  }
}

int main (int argc, char** argv) {
  std::string shader_path = MOPPE_NHAL_DEMO_SHADERS;
  std::string capture_path;
  int frames = 30;
  bool native = false, window_wanted = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--capture" && i + 1 < argc)
      capture_path = argv[++i];
    else if (arg == "--frames" && i + 1 < argc)
      frames = std::max (1, std::atoi (argv[++i]));
    else if (arg == "--shaders" && i + 1 < argc)
      shader_path = argv[++i];
    else if (arg == "--native")
      native = true;
    else if (arg == "--window")
      window_wanted = true;
  }
  const bool windowed = capture_path.empty () || window_wanted;

  SDL_Window* window = nullptr;
  VulkanSurface surface;
  std::uint32_t width = 1280, height = 720;
  if (windowed) {
    if (!SDL_Init (SDL_INIT_VIDEO)) {
      std::cerr << "NHAL demo: " << SDL_GetError () << std::endl;
      return -1;
    }
    window = SDL_CreateWindow ("NHAL demo (Vulkan)", 1280, 720,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
                                 | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
      std::cerr << "NHAL demo: " << SDL_GetError () << std::endl;
      return -1;
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels (window, &w, &h);
    width = std::uint32_t (w);
    height = std::uint32_t (h);
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions (&count);
    surface.instance_extensions.assign (names, names + count);
    surface.create = [window] (VkInstance instance) {
      VkSurfaceKHR made = VK_NULL_HANDLE;
      if (!SDL_Vulkan_CreateSurface (window, instance, nullptr, &made))
        throw std::runtime_error (std::string ("SDL_Vulkan_CreateSurface: ")
                                  + SDL_GetError ());
      return made;
    };
  }

  int status = 0;
  try {
    auto device = create_vulkan_device (surface, width, height,
                                        Format::bgra8_unorm);
    // One SPIR-V module per program stage, as luv-shaderc writes them.
    const char* files[] = { "terrain.vertex", "terrain.fragment",
                            "trees.vertex", "trees.fragment",
                            "sky.vertex", "sky.fragment",
                            "tonemap.vertex", "tonemap.fragment",
                            "forest_wind.compute", "terrain_shadow.vertex",
                            "trees_shadow.vertex", "resolve.vertex",
                            "resolve.fragment" };
    std::array<std::vector<std::uint32_t>, 13> spirv;
    for (int i = 0; i < 13; ++i)
      spirv[i] = read_spirv (shader_path + "/" + files[i] + ".spv");
    auto code = [&] (int i) { return StageCode { {}, {}, spirv[i] }; };
    auto scene = std::make_unique<demo::Scene> (
      *device, demo::Shaders { code (0), code (1), code (2), code (3),
                               code (4), code (5), code (6), code (7),
                               code (8), code (9), code (10), code (11),
                               code (12) });
    scene->set_temporal (!native);
    std::cerr << "NHAL demo: " << device->info ().backend << " on "
              << device->info ().adapter << ", " << scene->tree_count ()
              << " trees" << std::endl;

    const auto start = std::chrono::steady_clock::now ();
    auto last = start;
    int rendered = 0;
    bool running = true;
    while (running) {
      if (window) {
        SDL_Event event;
        while (SDL_PollEvent (&event)) {
          if (event.type == SDL_EVENT_QUIT)
            running = false;
          else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            device->resize_surface (std::uint32_t (event.window.data1),
                                    std::uint32_t (event.window.data2));
          // U switches between temporal upscaling and native 4x MSAA.
          else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat
                   && event.key.scancode == SDL_SCANCODE_U) {
            scene->set_temporal (!scene->temporal ());
            std::cerr << "NHAL demo: "
                      << (scene->temporal () ? "temporal upscaling"
                                             : "native 4x MSAA")
                      << std::endl;
          } else if (event.type == SDL_EVENT_KEY_DOWN
                     && event.key.scancode == SDL_SCANCODE_ESCAPE)
            running = false;
        }
      }
      const auto now = std::chrono::steady_clock::now ();
      const double seconds =
        capture_path.empty ()
          ? std::chrono::duration<double> (now - start).count ()
          : 12.0 + rendered / 60.0;
      const double step =
        std::min (0.1, std::chrono::duration<double> (now - last).count ());
      last = now;

      // WASD moves, the arrows look, Q and E sink and rise, Shift hurries.
      demo::Flight flight;
      if (window) {
        const bool* keys = SDL_GetKeyboardState (nullptr);
        auto held = [&] (SDL_Scancode key) { return keys[key] ? 1.0f : 0.0f; };
        flight.forward = held (SDL_SCANCODE_W) - held (SDL_SCANCODE_S);
        flight.strafe = held (SDL_SCANCODE_D) - held (SDL_SCANCODE_A);
        flight.rise = held (SDL_SCANCODE_E) - held (SDL_SCANCODE_Q);
        flight.turn = held (SDL_SCANCODE_RIGHT) - held (SDL_SCANCODE_LEFT);
        flight.pitch = held (SDL_SCANCODE_UP) - held (SDL_SCANCODE_DOWN);
        flight.boost = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
      }
      scene->fly (flight, step);
      if (!scene->render (seconds)) {
        SDL_Delay (16);
        continue;
      }
      ++rendered;
      const bool final = !capture_path.empty () && rendered == frames;
      if (final)
        device->capture_frame ([&capture_path] (const Capture& capture) {
          write_tga (capture, capture_path);
          std::cerr << "NHAL demo: wrote " << capture_path << " ("
                    << capture.width << "x" << capture.height << ")"
                    << std::endl;
        });
      device->end_frame ();
      if (rendered % 120 == 0 || final) {
        std::cerr << "NHAL demo: GPU";
        for (const PassTiming& pass : device->pass_timings ())
          std::fprintf (stderr, " %s %.2f ms", pass.label.c_str (),
                        pass.milliseconds);
        std::cerr << std::endl;
      }
      if (final) {
        device->wait_idle ();
        running = false;
      }
    }
    device->wait_idle ();
    scene.reset ();
  } catch (const std::exception& error) {
    std::cerr << "NHAL demo: " << error.what () << std::endl;
    status = -1;
  }
  if (window) {
    SDL_DestroyWindow (window);
    SDL_Quit ();
  }
  return status;
}
