#ifndef MOPPE_GAME_WORLD_LOADING_HH
#define MOPPE_GAME_WORLD_LOADING_HH

#include <moppe/game/generated_world.hh>
#include <moppe/game/world_cache.hh>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace moppe::game {
  // One line of the loading log: what the loader started doing, and when.
  struct LoadingEvent {
    std::string title;
    quantity<si::second, double> elapsed;
  };

  // A main-thread reading of the loader for one frame of the loading
  // screen.  The text is whatever the worker last reported; progress is a
  // real measurement of the share of the current stage done when it has
  // one, and empty when it does not.  There is no schedule of expected
  // durations.
  struct LoadingStatus {
    std::string title;
    std::string detail;
    std::optional<proportion_t> progress;
    seconds_t elapsed = 0.0f * u::s;
    std::uint32_t seed = 0;
    std::vector<LoadingEvent> events;
  };

  // The finished-world cache directory this package carries, or empty when
  // it carries none (MOPPE_BUNDLED_WORLD_CACHE names it, relative to the
  // package's assets).
  std::string bundled_world_cache ();

  class WorldLoadingState;

  // Owns one single-flight world build on a background thread.  The worker
  // reports what it is doing; the main thread reads status and takes the
  // finished world.  The worker never borrows the application object that
  // requested the build.
  class WorldLoading {
  public:
    WorldLoading (const terrain::WorldRecipe& recipe,
                  WorldCacheConfig cache_config);

    WorldLoading (const WorldLoading&) = delete;
    WorldLoading& operator= (const WorldLoading&) = delete;

    void start (const WorldParams& world, terrain::WorldRecipe recipe);

    // Describe the current work.  Ordinarily called from the generation
    // thread; the application may also report its own finishing steps.
    void report (std::string title,
                 std::string detail,
                 std::optional<proportion_t> progress = std::nullopt);

    // Non-null exactly once per build, after the worker has finished.
    std::unique_ptr<GeneratedWorld> take_completed_world ();

    LoadingStatus status ();

  private:
    std::shared_ptr<WorldLoadingState> m_state;
  };
}

#endif
