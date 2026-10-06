// Platform services for the SDL host: assets in the app bundle, beside the
// executable, or in the source tree; caches in the platform's user cache
// folder; threads for background work, and a main-thread queue for their
// completions. The window, input, and platform::run are host.cc's.

#include <moppe/environment.hh>
#include <moppe/platform/platform.hh>
#include <moppe/platform/sdl/sdl.hh>

#include <SDL3/SDL.h>

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
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace moppe::platform {
  namespace {
    std::mutex main_queue_mutex;
    std::deque<std::function<void ()>> main_queue;

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
    return relative;
  }

  std::string executable_build_id () {
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
  }

  std::string cache_path (const std::string& relative) {
    static const std::string base = [] {
      std::filesystem::path root;
      const char* home = moppe::environment ("HOME");
#ifdef __APPLE__
      // Where the Mac has always kept them, so finished worlds carry over.
      root = home ? std::filesystem::path (home) / "Library" / "Caches"
                        / "Moppe"
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
    return {};
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
    std::thread ([work, done, context = std::move (context)] () mutable {
      work (context.get ());
      const std::lock_guard<std::mutex> lock (main_queue_mutex);
      main_queue.push_back (
        [done, context = std::move (context)] { done (context.get ()); });
    }).detach ();
  }


  namespace sdl {
    void frame_rendered () {}

    void run_main_thread_tasks () {
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
