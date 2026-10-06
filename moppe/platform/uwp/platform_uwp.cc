// Platform services for UWP (Xbox Series consoles in Developer Mode): the
// package folder for assets, LocalCache for caches, threads for background
// work, and a main-thread queue for their completions.
//
// platform::run is not defined here yet: it belongs with a renderer, and
// NHAL's Direct3D 12 backend will provide the one this platform runs.

#include <moppe/environment.hh>
#include <moppe/platform/platform.hh>
#include <moppe/platform/uwp/uwp.hh>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include <windows.h>

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

namespace moppe::platform {
  namespace {
    std::atomic<bool> quitting = false;

    std::mutex main_queue_mutex;
    std::deque<std::function<void ()>> main_queue;

    std::string narrow (const winrt::hstring& text) {
      return winrt::to_string (text);
    }

    // The package's install folder: read-only, holding the executable and
    // everything share/<pname>/ installed beside it.
    const std::string& package_root () {
      static const std::string root =
        narrow (winrt::Windows::ApplicationModel::Package::Current ()
                  .InstalledLocation ()
                  .Path ()) +
        "\\";
      return root;
    }

    std::string windows_path (std::string path) {
      for (char& c : path)
        if (c == '/')
          c = '\\';
      return path;
    }

    std::string executable_path () {
      wchar_t buffer[MAX_PATH];
      const DWORD length = ::GetModuleFileNameW (nullptr, buffer, MAX_PATH);
      if (length == 0 || length == MAX_PATH)
        return {};
      return winrt::to_string (std::wstring_view (buffer, length));
    }
  }

  void request_quit () {
    quitting = true;
  }

  void set_window_title (const std::string&) {}

  void set_pointer_captured (bool) {}

  std::string asset_path (const std::string& relative) {
    if (const char* base = moppe::environment ("MOPPE_ASSETS")) {
      const std::string p = windows_path (std::string (base) + "/" + relative);
      if (std::filesystem::exists (p))
        return p;
    }
    return package_root () + windows_path (relative);
  }

  std::string executable_build_id () {
    static const std::string id = [] {
      std::ifstream input (executable_path (), std::ios::binary);
      if (!input)
        return std::string ("unknown");
      // FNV-1a over the linked executable, as on Apple platforms: a cache
      // identity, not a security boundary.
      std::uint64_t hash = 14695981039346656037ull;
      std::vector<char> bytes (64 * 1024);
      while (input) {
        input.read (bytes.data (),
                    static_cast<std::streamsize> (bytes.size ()));
        for (std::streamsize i = 0; i < input.gcount (); ++i) {
          hash ^= static_cast<unsigned char> (bytes[i]);
          hash *= 1099511628211ull;
        }
      }
      std::ostringstream text;
      text << std::hex << std::setfill ('0') << std::setw (16) << hash;
      return text.str ();
    }();
    return id;
  }

  std::string cache_path (const std::string& relative) {
    // Cache names spell out their whole recipe, which takes them past
    // MAX_PATH under the package's deep LocalCache; the \\?\ prefix lifts the
    // limit. Only the last folder is created: the app container refuses to
    // "create" ancestors it may not write, even ones that exist.
    static const std::string base = [] {
      const std::string folder =
        "\\\\?\\" +
        narrow (winrt::Windows::Storage::ApplicationData::Current ()
                  .LocalCacheFolder ()
                  .Path ()) +
        "\\Moppe";
      std::error_code ignored;
      std::filesystem::create_directory (folder, ignored);
      return folder;
    }();
    return relative.empty () ? base : base + "\\" + windows_path (relative);
  }

  double now () {
    using namespace std::chrono;
    return duration_cast<duration<double>> (
             steady_clock::now ().time_since_epoch ())
      .count ();
  }

  Insets safe_insets () {
    return {};
  }

  void say (const std::string& phrase) {
    std::cerr << "moppe: say: " << phrase << std::endl;
  }

  void async (void (*work) (void*),
              void (*done) (void*),
              std::shared_ptr<void> context) {
    std::thread ([work, done, context = std::move (context)] () mutable {
      work (context.get ());
      const std::lock_guard<std::mutex> lock (main_queue_mutex);
      main_queue.push_back (
        [done, context = std::move (context)] { done (context.get ()); });
    }).detach ();
  }

  bool rasterize_glyph (const char*, float, float, unsigned int, GlyphBitmap&) {
    return false;
  }

  namespace uwp {
    void run_main_thread_tasks () {
      std::deque<std::function<void ()>> tasks;
      {
        const std::lock_guard<std::mutex> lock (main_queue_mutex);
        tasks.swap (main_queue);
      }
      for (auto& task : tasks)
        task ();
    }

    bool quit_requested () {
      return quitting;
    }

    std::string local_state_path () {
      return narrow (winrt::Windows::Storage::ApplicationData::Current ()
                       .LocalFolder ()
                       .Path ()) +
             "\\";
    }

    int load_environment_file (const std::string& path) {
      std::ifstream input (path);
      int count = 0;
      std::string line;
      while (std::getline (input, line)) {
        if (!line.empty () && line.back () == '\r')
          line.pop_back ();
        const std::size_t equals = line.find ('=');
        if (line.empty () || line[0] == '#' || equals == std::string::npos)
          continue;
        const std::string name = line.substr (0, equals);
        const std::string value = line.substr (equals + 1);
        moppe::set_environment (name.c_str (), value.c_str ());
        ++count;
      }
      return count;
    }
  }
}
