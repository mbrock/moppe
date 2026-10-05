// The NHAL demo on Xbox (UWP): a CoreApplication view with a Direct3D 12
// device presenting a 3840x2160 swapchain, which the console shows natively.
// The report and, after a few seconds, one captured frame go to LocalState.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Core.h>

#include <moppe/nhal/d3d12/d3d12_device.hh>
#include <moppe/nhal/demo/scene.hh>

#include <shader_forest_wind_compute.h>
#include <shader_sky_fragment.h>
#include <shader_terrain_shadow_vertex.h>
#include <shader_trees_shadow_vertex.h>
#include <shader_sky_vertex.h>
#include <shader_terrain_fragment.h>
#include <shader_terrain_vertex.h>
#include <shader_tonemap_fragment.h>
#include <shader_tonemap_vertex.h>
#include <shader_trees_fragment.h>
#include <shader_trees_vertex.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;
using namespace moppe::nhal;

namespace {
  std::string local_state () {
    return to_string (
      winrt::Windows::Storage::ApplicationData::Current ().LocalFolder ()
        .Path ());
  }

  void report (const std::string& text) {
    static std::string log;
    log += text + "\n";
    if (FILE* file = std::fopen ((local_state () + "\\nhal.txt").c_str (),
                                 "w")) {
      std::fputs (log.c_str (), file);
      std::fclose (file);
    }
  }

  // Uncompressed 32-bit BGRA TGA: small enough to write by hand.
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
    header[17] = 0x28; // top-left origin, 8 alpha bits
    std::fwrite (header, 1, sizeof header, file);
    for (std::uint32_t y = 0; y < capture.height; ++y)
      std::fwrite (capture.pixels.data () + std::size_t (y) * capture.row_bytes,
                   1, capture.width * 4, file);
    std::fclose (file);
  }

  StageCode code (const unsigned char* dxil, std::size_t size) {
    return { {}, { dxil, size } };
  }
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
  IFrameworkView CreateView () { return *this; }

  void Initialize (CoreApplicationView const& view) {
    view.Activated ([] (auto&&, auto&&) {
      CoreWindow::GetForCurrentThread ().Activate ();
    });
  }

  void SetWindow (CoreWindow const& window) {
    m_window = window;
    window.Closed ([this] (auto&&, auto&&) { m_closed = true; });
  }

  void Load (hstring const&) {}
  void Uninitialize () {}

  void Run () {
    try {
      auto device = create_d3d12_device (winrt::get_unknown (m_window), 3840,
                                         2160, Format::bgra8_unorm);
      const demo::Shaders shaders {
        code (shader_terrain_vertex, sizeof shader_terrain_vertex),
        code (shader_terrain_fragment, sizeof shader_terrain_fragment),
        code (shader_trees_vertex, sizeof shader_trees_vertex),
        code (shader_trees_fragment, sizeof shader_trees_fragment),
        code (shader_sky_vertex, sizeof shader_sky_vertex),
        code (shader_sky_fragment, sizeof shader_sky_fragment),
        code (shader_tonemap_vertex, sizeof shader_tonemap_vertex),
        code (shader_tonemap_fragment, sizeof shader_tonemap_fragment),
        code (shader_forest_wind_compute, sizeof shader_forest_wind_compute),
        code (shader_terrain_shadow_vertex, sizeof shader_terrain_shadow_vertex),
        code (shader_trees_shadow_vertex, sizeof shader_trees_shadow_vertex),
      };
      demo::Scene scene (*device, shaders);
      report ("NHAL demo: " + device->info ().backend + " on "
              + device->info ().adapter + ", "
              + std::to_string (scene.tree_count ()) + " trees, "
              + std::to_string (device->surface_width ()) + "x"
              + std::to_string (device->surface_height ()));

      const auto start = std::chrono::steady_clock::now ();
      auto last = start;
      int frames = 0;
      double slowest = 0;
      bool captured = false;
      while (!m_closed) {
        m_window.Dispatcher ().ProcessEvents (
          CoreProcessEventsOption::ProcessAllIfPresent);
        const auto now = std::chrono::steady_clock::now ();
        const double seconds =
          std::chrono::duration<double> (now - start).count ();
        slowest = std::max (
          slowest, std::chrono::duration<double> (now - last).count ());
        last = now;
        if (!scene.render (seconds))
          continue;
        if (!captured && seconds > 5) {
          captured = true;
          const std::string path = local_state () + "\\nhal.tga";
          device->capture_frame ([path] (const Capture& capture) {
            write_tga (capture, path);
            report ("captured " + std::to_string (capture.width) + "x"
                    + std::to_string (capture.height));
          });
        }
        device->end_frame ();
        ++frames;
        if (frames <= 3 || frames == 30)
          report ("frame " + std::to_string (frames) + " submitted");
        if (frames % 600 == 0) {
          char line[128];
          std::snprintf (line, sizeof line,
                         "%d frames, %.2f fps overall, slowest %.1f ms",
                         frames, frames / seconds, slowest * 1000);
          report (line);
          slowest = 0;
        }
      }
      device->wait_idle ();
    } catch (const std::exception& error) {
      report (std::string ("NHAL demo failed: ") + error.what ());
    } catch (hresult_error const& error) {
      report ("NHAL demo failed: " + to_string (error.message ()));
    }
  }

private:
  CoreWindow m_window { nullptr };
  bool m_closed = false;
};

int __stdcall wWinMain (HINSTANCE, HINSTANCE, PWSTR, int) {
  init_apartment ();
  CoreApplication::Run (make<App> ());
}
