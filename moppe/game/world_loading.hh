#ifndef MOPPE_GAME_WORLD_LOADING_HH
#define MOPPE_GAME_WORLD_LOADING_HH

#include <moppe/game/generated_world.hh>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace moppe::game {
  // One line of the loading log: what the loader started doing, and when.
  struct LoadingEvent {
    std::string title;
    double elapsed;
  };

  // A main-thread reading of the loader for one frame of the loading
  // screen.  The text is whatever the worker last reported; progress is a
  // real measurement of the current stage when it has one, and negative
  // when it does not.  There is no schedule of expected durations.
  struct LoadingStatus {
    std::string title;
    std::string detail;
    float progress = -1.0f;
    float elapsed = 0.0f;
    bool terrain_visible = false;
    std::uint32_t seed = 0;
    std::vector<LoadingEvent> events;
  };

  class WorldLoadingState;

  // Owns one single-flight world build on a background thread.  The worker
  // reports what it is doing and publishes terrain snapshots; the main
  // thread reads status, refreshes the preview heightmap, and takes the
  // finished world.  The worker never borrows the application object that
  // requested the build.
  class WorldLoading {
  public:
    WorldLoading (const WorldParams& world, const terrain::WorldRecipe& recipe);

    WorldLoading (const WorldLoading&) = delete;
    WorldLoading& operator= (const WorldLoading&) = delete;

    void start (const WorldParams& world, terrain::WorldRecipe recipe);

    // Describe the current work.  Ordinarily called from the generation
    // thread; the application may also report its own finishing steps.
    void report (std::string title, std::string detail, float progress = -1.0f);

    // Non-null exactly once per build, after the worker has finished.
    std::unique_ptr<GeneratedWorld> take_completed_world ();

    LoadingStatus status ();

    // Copies the newest published terrain into the preview map.  True when
    // the preview changed and should be re-uploaded.
    bool refresh_preview ();

    const WorldParams& preview_world () const noexcept;
    const map::RandomHeightMap& preview_map () const noexcept;

    // Claims the one development loading capture.  The renderer's first
    // frames are still settling, so an ordinary claim waits until the
    // preview has been on screen for a moment; the final loading frame is
    // the last chance to fire regardless.
    bool claim_loading_capture (bool last_chance) noexcept;

  private:
    std::shared_ptr<WorldLoadingState> m_state;
  };
}

#endif
