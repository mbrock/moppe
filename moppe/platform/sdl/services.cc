// Platform services for the SDL host: assets in the app bundle, beside the
// executable, or in the source tree; caches in the platform's user cache
// folder; threads for background work, and a main-thread queue for their
// completions. The window, input, and platform::run are host.cc's.

#include <moppe/environment.hh>
#include <moppe/platform/platform.hh>
#include <moppe/platform/sdl/sdl.hh>

#include <SDL3/SDL.h>

#ifdef MOPPE_EMBEDDED_ASSETS
#include <zstd.h>
#endif

#include <algorithm>
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
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

// A browser page built without threads (docs/web.md) has only its own.
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define MOPPE_THREADLESS 1
#else
#define MOPPE_THREADLESS 0
#endif

namespace moppe::platform {
  namespace {
    std::mutex main_queue_mutex;
    std::deque<std::function<void ()>> main_queue;

#if MOPPE_THREADLESS
    // What platform::async was asked for, and the turns of the host's loop
    // still to pass before the page's thread takes it up.
    struct WaitingWork {
      int turns;
      std::function<void ()> work;
    };
    std::deque<WaitingWork> waiting_work;
#endif

    std::filesystem::path executable_path () {
#ifdef __APPLE__
      char buffer[4096];
      std::uint32_t size = sizeof buffer;
      if (_NSGetExecutablePath (buffer, &size) == 0)
        return std::filesystem::path (buffer);
      return {};
#else
      std::error_code error;
      return std::filesystem::read_symlink ("/proc/self/exe", error);
#endif
    }

#ifdef MOPPE_EMBEDDED_ASSETS
    // A release carries its assets and the default world's land inside
    // the executable (CMake's MOPPE_EMBED_ASSETS, tools/pack-assets), so
    // that the executable alone is the game.
    struct EmbeddedAsset {
      const char* name;
      const unsigned char* data;
      std::size_t size;
      std::size_t original_size;
      // The bytes are 16-bit words as the differences of neighbours, low
      // bytes then high bytes.
      bool delta16;
    };
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#include MOPPE_EMBEDDED_ASSETS
#pragma clang diagnostic pop

    std::string unpack (const EmbeddedAsset& asset) {
      std::string bytes (asset.original_size, '\0');
      const std::size_t size =
        ZSTD_decompress (bytes.data (), bytes.size (), asset.data, asset.size);
      if (ZSTD_isError (size) || size != bytes.size ())
        throw std::runtime_error (std::string ("can't unpack the built-in ") +
                                  asset.name);
      if (asset.delta16) {
        const std::size_t words = bytes.size () / 2;
        std::string coded = bytes;
        std::uint16_t word = 0;
        for (std::size_t i = 0; i < words; ++i) {
          word += static_cast<std::uint16_t> (
            static_cast<unsigned char> (coded[i]) |
            static_cast<unsigned char> (coded[words + i]) << 8);
          bytes[2 * i] = static_cast<char> (word & 0xff);
          bytes[2 * i + 1] = static_cast<char> (word >> 8);
        }
      }
      return bytes;
    }

    // Unpacks a built-in asset into a folder of the cache kept for these
    // assets, once, and says where; the loaders read files. Other builds'
    // folders go when this one is made.
    std::string embedded_asset_path (const std::string& relative) {
      namespace fs = std::filesystem;
      const auto found = std::ranges::find_if (
        embedded_assets, [&relative] (const EmbeddedAsset& asset) {
          return relative == asset.name;
        });
      if (found == std::end (embedded_assets))
        return {};
      static std::mutex mutex;
      const std::lock_guard lock (mutex);
      const fs::path folder =
        fs::path (cache_path ("assets")) / MOPPE_EMBEDDED_ASSETS_ID;
      const fs::path path = folder / relative;
      std::error_code error;
      if (fs::file_size (path, error) == found->original_size)
        return path.string ();
      if (!fs::exists (folder, error))
        for (const fs::directory_entry& old :
             fs::directory_iterator (folder.parent_path (), error))
          fs::remove_all (old.path (), error);
      fs::create_directories (path.parent_path (), error);
      // A name of this process's own, so two games starting at once each
      // write whole files.
      const fs::path partial = path.string () + ".partial-" +
                               std::to_string (SDL_GetCurrentThreadID ()) +
                               "-" + std::to_string (SDL_GetTicksNS ());
      const std::string bytes = unpack (*found);
      std::ofstream out (partial, std::ios::binary);
      out.write (bytes.data (), static_cast<std::streamsize> (bytes.size ()));
      out.close ();
      if (out)
        fs::rename (partial, path, error);
      if (!out || error) {
        fs::remove (partial, error);
        return {};
      }
      std::cerr << "moppe: unpacked the built-in " << relative << " to "
                << path.string () << std::endl;
      return path.string ();
    }
#endif
  }

  std::string asset_path (const std::string& relative) {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    if (const char* base = moppe::environment ("MOPPE_ASSETS"))
      roots.emplace_back (base);
    // An app bundle's resources, or the executable's folder.
    if (const char* base = SDL_GetBasePath ())
      roots.emplace_back (base);
    const fs::path bin = executable_path ().parent_path ();
    if (!bin.empty ()) {
      roots.push_back (bin.parent_path () / "share" / "moppe");
      roots.push_back (bin);
    }
#ifdef MOPPE_SOURCE_DIR
    roots.emplace_back (MOPPE_SOURCE_DIR);
#endif
    for (const fs::path& root : roots) {
      std::error_code error;
      if (fs::exists (root / relative, error))
        return (root / relative).string ();
    }
#ifdef MOPPE_EMBEDDED_ASSETS
    if (std::string path = embedded_asset_path (relative); !path.empty ())
      return path;
#endif
    return relative;
  }

  std::string executable_build_id () {
#ifdef __EMSCRIPTEN__
    // A page's caches go with the page, so its builds need no telling
    // apart.
    return "web";
#else
    static const std::string id = [] {
      std::ifstream input (executable_path (), std::ios::binary);
      if (!input)
        return std::string ("unknown");
      // FNV-1a over the linked executable, as on the other platforms: a
      // cache identity, not a security boundary.
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
#endif
  }

  std::string cache_path (const std::string& relative) {
    static const std::string base = [] {
      std::filesystem::path root;
      [[maybe_unused]] const char* home = moppe::environment ("HOME");
#ifdef __APPLE__
      // Where the Mac has always kept them, so finished worlds carry over.
      root = home ? std::filesystem::path (home) / "Library" / "Caches"
                        / "Moppe"
                  : std::filesystem::temp_directory_path () / "Moppe";
#elif defined(_WIN32)
      const char* local = moppe::environment ("LOCALAPPDATA");
      root = local ? std::filesystem::path (local) / "Moppe"
                   : std::filesystem::temp_directory_path () / "Moppe";
#else
      if (const char* xdg = moppe::environment ("XDG_CACHE_HOME"); xdg && *xdg)
        root = xdg;
      else if (home)
        root = std::filesystem::path (home) / ".cache";
      else
        root = std::filesystem::temp_directory_path ();
      root /= "moppe";
#endif
      std::error_code ignored;
      std::filesystem::create_directories (root, ignored);
      return root.string ();
    }();
    return relative.empty () ? base : base + "/" + relative;
  }

  double now () {
    using namespace std::chrono;
    return duration_cast<duration<double>> (
             steady_clock::now ().time_since_epoch ())
      .count ();
  }

  Insets safe_insets () {
#ifdef __EMSCRIPTEN__
    // A phone's notch and home bar, which the page measures (touch.js).
    const auto inset = [] (int side) {
      return float (EM_ASM_DOUBLE (
        { return (Module['moppeInsets'] || [ 0, 0, 0, 0 ])[$0]; }, side));
    };
    return { inset (0), inset (1), inset (2), inset (3) };
#else
    return {};
#endif
  }

  void say (const std::string& phrase) {
    std::cerr << "moppe: say: " << phrase << std::endl;
#ifdef __APPLE__
    // The Mac speaks it, as it always has, without waiting for the voice.
    const char* argv[] = { "say", phrase.c_str (), nullptr };
    pid_t child = 0;
    if (posix_spawnp (&child, "say", nullptr, nullptr,
                      const_cast<char* const*> (argv), environ) == 0)
      std::thread ([child] { waitpid (child, nullptr, 0); }).detach ();
#endif
  }

  void async (void (*work) (void*),
              void (*done) (void*),
              std::shared_ptr<void> context) {
#if MOPPE_THREADLESS
    // The page's own thread does the work, a few turns from now, so that
    // the frame saying what the page waits for is on screen meanwhile.
    waiting_work.push_back (
      { 4, [work, done, context = std::move (context)] {
         work (context.get ());
         done (context.get ());
       } });
#else
    std::thread ([work, done, context = std::move (context)] () mutable {
      work (context.get ());
      const std::lock_guard<std::mutex> lock (main_queue_mutex);
      main_queue.push_back (
        [done, context = std::move (context)] { done (context.get ()); });
    }).detach ();
#endif
  }


  namespace sdl {
    void frame_rendered () {}

    void run_main_thread_tasks () {
#if MOPPE_THREADLESS
      if (!waiting_work.empty () && --waiting_work.front ().turns <= 0) {
        const std::function<void ()> work =
          std::move (waiting_work.front ().work);
        waiting_work.pop_front ();
        work ();
      }
#endif
      std::deque<std::function<void ()>> tasks;
      {
        const std::lock_guard<std::mutex> lock (main_queue_mutex);
        tasks.swap (main_queue);
      }
      for (auto& task : tasks)
        task ();
    }
  }
}
