#ifndef MOPPE_GAME_WORLD_LOADING_HH
#define MOPPE_GAME_WORLD_LOADING_HH

#include <moppe/game/generated_world.hh>

#include <cstdint>
#include <memory>
#include <vector>

namespace moppe::game {
  enum class LoadingStage {
    Starting,
    LookingForCache,
    ReadingCache,
    BuildingContinents,
    EvolvingTerrain,
    RefiningTerrain,
    SavingTerrain,
    RebuildingSurface,
    FindingStandingWater,
    CataloguingLakes,
    TracingDrainage,
    ConnectingWaterways,
    ExtractingRivers,
    AssemblingWorld,
    PreparingWater,
    PreparingSurface,
    PlacingStars,
    GrowingForest,
    PlantingTrailside,
    PlanningJourney,
    UploadingTerrain,
    CastingShadows,
    Ready,
  };

  struct LoadingStageText {
    const char* title;
    const char* detail;
    float progress;
  };

  struct LoadingEvent {
    LoadingStage stage;
    double elapsed;
  };

  struct WorldLoadingFrame {
    LoadingStage stage;
    float elapsed;
    float progress;
    float local_progress;
    bool preview_changed;
    bool preview_sequence_complete;
    bool terrain_visible;
    std::uint32_t seed;
    int work_done;
    int work_total;
    int geological_years_done;
    int geological_years_total;
    int source_done;
    int source_total;
    std::vector<LoadingEvent> events;
  };

  LoadingStageText loading_stage_text (LoadingStage stage);

  class WorldLoadingState;

  // Owns one single-flight world build and the thread-safe readings used by
  // its loading presentation. The worker retains only shared loader state;
  // it never borrows the application object that requested the build.
  class WorldLoading {
  public:
    WorldLoading (const WorldParams& world, const terrain::WorldRecipe& recipe);

    WorldLoading (const WorldLoading&) = delete;
    WorldLoading& operator= (const WorldLoading&) = delete;

    void start (const WorldParams& world, terrain::WorldRecipe recipe);
    void set_stage (LoadingStage stage);

    bool generation_complete () const noexcept;
    std::unique_ptr<GeneratedWorld> take_completed_world ();

    WorldLoadingFrame advance (double now, float transition_seconds);

    const WorldParams& preview_world () const noexcept;
    const map::RandomHeightMap& preview_map () const noexcept;

    bool claim_loading_capture () noexcept;

  private:
    std::shared_ptr<WorldLoadingState> m_state;
  };
}

#endif
