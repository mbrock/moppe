// moppe-core-xbox: the core probe (moppe/game/core_probe.hh) on an Xbox in
// Developer Mode. It generates the game's default world on the console,
// simulates walking and riding over it, runs the application shell against
// a recording renderer, and writes what it measured to LocalState:
//
//   report.txt   the measurements, rewritten as each phase finishes
//   log.txt      everything the game writes to std::cerr
//
// LocalState/environment.txt, if present, holds NAME=VALUE lines for
// moppe::environment (MOPPE_SEED and MOPPE_RESOLUTION choose the world).
// The screen is a progress bar: blue while working, then green for a pass
// or red for a failure.

#include <moppe/environment.hh>
#include <moppe/game/core_probe.hh>
#include <moppe/platform/uwp/uwp.hh>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include <unknwn.h>
#include <windows.h>

#include <d3d11_1.h>
#include <dxgi1_2.h>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;

namespace {
  enum class Outcome { working, passed, failed };

  struct Status {
    std::atomic<float> progress = 0.0f;
    std::atomic<Outcome> outcome = Outcome::working;
  };

  void write_file (const std::string& path, const std::string& text) {
    if (FILE* file = std::fopen (path.c_str (), "wb")) {
      std::fwrite (text.data (), 1, text.size (), file);
      std::fclose (file);
    }
  }

  moppe::game::CoreProbeMemory app_memory () {
    using winrt::Windows::System::MemoryManager;
    const auto report = MemoryManager::GetAppMemoryReport ();
    return {
      .current = report.PrivateCommitUsage (),
      .peak = report.PeakPrivateCommitUsage (),
      .limit = MemoryManager::AppMemoryUsageLimit (),
    };
  }

  // A swap chain cleared to a progress bar: enough to see the probe working
  // from the sofa, and to tell a pass from a failure in a screenshot.
  class Screen {
  public:
    explicit Screen (const CoreWindow& window) {
      const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
      com_ptr<ID3D11Device> device;
      com_ptr<ID3D11DeviceContext> context;
      check_hresult (D3D11CreateDevice (nullptr,
                                        D3D_DRIVER_TYPE_HARDWARE,
                                        nullptr,
                                        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                        levels,
                                        1,
                                        D3D11_SDK_VERSION,
                                        device.put (),
                                        nullptr,
                                        context.put ()));
      m_device = device.as<ID3D11Device1> ();
      m_context = context.as<ID3D11DeviceContext1> ();
      const auto bounds = window.Bounds ();
      m_width = std::max (1u, static_cast<UINT> (bounds.Width));
      m_height = std::max (1u, static_cast<UINT> (bounds.Height));
      DXGI_SWAP_CHAIN_DESC1 desc {};
      desc.Width = m_width;
      desc.Height = m_height;
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      desc.BufferCount = 2;
      desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
      const auto dxgi_device = m_device.as<IDXGIDevice> ();
      com_ptr<IDXGIAdapter> adapter;
      check_hresult (dxgi_device->GetAdapter (adapter.put ()));
      com_ptr<IDXGIFactory2> factory;
      check_hresult (
        adapter->GetParent (__uuidof (IDXGIFactory2), factory.put_void ()));
      check_hresult (
        factory->CreateSwapChainForCoreWindow (m_device.get (),
                                               winrt::get_unknown (window),
                                               &desc,
                                               nullptr,
                                               m_swap_chain.put ()));
    }

    void present (float progress, Outcome outcome) {
      com_ptr<ID3D11Texture2D> back;
      check_hresult (m_swap_chain->GetBuffer (
        0, __uuidof (ID3D11Texture2D), back.put_void ()));
      com_ptr<ID3D11RenderTargetView> target;
      check_hresult (
        m_device->CreateRenderTargetView (back.get (), nullptr, target.put ()));
      const float background[4] = { 0.055f, 0.067f, 0.086f, 1.0f };
      m_context->ClearRenderTargetView (target.get (), background);
      const float track[4] = { 0.12f, 0.14f, 0.17f, 1.0f };
      const float working[4] = { 0.25f, 0.55f, 0.95f, 1.0f };
      const float passed[4] = { 0.30f, 0.80f, 0.40f, 1.0f };
      const float failed[4] = { 0.90f, 0.25f, 0.20f, 1.0f };
      const float* bar = outcome == Outcome::passed   ? passed
                         : outcome == Outcome::failed ? failed
                                                      : working;
      const LONG left = m_width / 8;
      const LONG right = m_width - m_width / 8;
      const LONG top = m_height / 2 - m_height / 60;
      const LONG bottom = m_height / 2 + m_height / 60;
      const D3D11_RECT whole { left, top, right, bottom };
      m_context->ClearView (target.get (), track, &whole, 1);
      const float t =
        outcome == Outcome::working ? std::clamp (progress, 0.0f, 1.0f) : 1.0f;
      const D3D11_RECT done {
        left, top, left + static_cast<LONG> (t * (right - left)), bottom
      };
      if (done.right > done.left)
        m_context->ClearView (target.get (), bar, &done, 1);
      check_hresult (m_swap_chain->Present (1, 0));
    }

  private:
    com_ptr<ID3D11Device1> m_device;
    com_ptr<ID3D11DeviceContext1> m_context;
    com_ptr<IDXGISwapChain1> m_swap_chain;
    UINT m_width = 1;
    UINT m_height = 1;
  };
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
  IFrameworkView CreateView () {
    return *this;
  }

  void Initialize (CoreApplicationView const& view) {
    view.Activated ([this] (auto&&, auto&&) {
      CoreWindow::GetForCurrentThread ().Activate ();
      m_activated = true;
    });
  }

  void SetWindow (CoreWindow const& window) {
    m_window = window;
    window.Closed ([this] (auto&&, auto&&) { m_closed = true; });
  }

  void Load (hstring const&) {}
  void Uninitialize () {}

  void Run () {
    while (!m_activated && !m_closed)
      m_window.Dispatcher ().ProcessEvents (
        CoreProcessEventsOption::ProcessOneAndAllPending);

    const std::string local = moppe::platform::uwp::local_state_path ();
    m_log.open (local + "log.txt", std::ios::trunc);
    if (m_log)
      std::cerr.rdbuf (m_log.rdbuf ());
    const int variables =
      moppe::platform::uwp::load_environment_file (local + "environment.txt");
    std::cerr << "moppe-core-xbox: " << variables
              << " variables from environment.txt" << std::endl;

    moppe::game::CoreProbeOptions options;
    if (const char* seed = moppe::environment ("MOPPE_SEED"))
      options.seed = std::atoi (seed);
    if (const char* resolution = moppe::environment ("MOPPE_RESOLUTION"))
      options.resolution = std::atoi (resolution);

    std::thread probe ([this, local, options] {
      moppe::game::CoreProbeHost host;
      host.platform_name = "Xbox (UWP)";
      host.memory = app_memory;
      host.write_report = [local] (const std::string& text) {
        write_file (local + "report.txt", text);
      };
      host.progress = [this] (const std::string&, float progress) {
        m_status.progress = progress;
      };
      bool ok = false;
      try {
        ok = moppe::game::run_core_probe (host, options);
      } catch (const std::exception& error) {
        std::cerr << "moppe-core-xbox: " << error.what () << std::endl;
      }
      m_status.outcome = ok ? Outcome::passed : Outcome::failed;
    });

    std::unique_ptr<Screen> screen;
    try {
      screen = std::make_unique<Screen> (m_window);
    } catch (const hresult_error& error) {
      std::cerr << "moppe-core-xbox: no screen: "
                << to_string (error.message ()) << std::endl;
    }
    while (!m_closed) {
      m_window.Dispatcher ().ProcessEvents (
        CoreProcessEventsOption::ProcessAllIfPresent);
      moppe::platform::uwp::run_main_thread_tasks ();
      if (screen)
        screen->present (m_status.progress, m_status.outcome);
      else
        std::this_thread::sleep_for (std::chrono::milliseconds (16));
    }
    // Closing mid-probe ends the process with it.
    if (m_status.outcome == Outcome::working)
      std::_Exit (0);
    probe.join ();
  }

private:
  CoreWindow m_window { nullptr };
  bool m_closed = false;
  bool m_activated = false;
  Status m_status;
  std::ofstream m_log;
};

int __stdcall wWinMain (HINSTANCE, HINSTANCE, PWSTR, int) {
  init_apartment ();
  CoreApplication::Run (make<App> ());
}
