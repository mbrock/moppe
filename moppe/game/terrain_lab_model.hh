#ifndef MOPPE_GAME_TERRAIN_LAB_MODEL_HH
#define MOPPE_GAME_TERRAIN_LAB_MODEL_HH

#include <moppe/map/terrain_evaluator.hh>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace moppe::game {
  // The CPU-side state of one Terrain Lab session.  It owns replayable
  // checkpoints and the original map snapshot; the renderer and its UI only
  // observe this state and decide how to present it.
  struct TerrainLabEvaluationProgress {
    enum class Phase { Idle, Materializing, Applying };

    Phase phase = Phase::Idle;
    std::size_t source_rows_completed = 0;
    std::size_t source_rows_total = 0;
    std::size_t completed_stages = 0;
    std::size_t total_stages = 0;
    std::size_t current_stage = 0;

    bool evaluating () const noexcept {
      return phase != Phase::Idle;
    }
  };

  class TerrainLabModel {
  public:
    TerrainLabModel () = default;
    TerrainLabModel (const TerrainLabModel&) = delete;
    TerrainLabModel& operator= (const TerrainLabModel&) = delete;
    TerrainLabModel (TerrainLabModel&&) = delete;
    TerrainLabModel& operator= (TerrainLabModel&&) = delete;
    ~TerrainLabModel ();

    // Borrow the map for a Lab session.  A caller may supply any field
    // evaluator, including none for a portable CPU-only session.
    void begin (map::RandomHeightMap& map,
                const terrain::TerrainProgram& program,
                const terrain::FieldEvaluator* source_evaluator = nullptr);
    void leave ();

    bool active () const noexcept {
      return m_map != nullptr;
    }
    bool map_pristine () const noexcept {
      return m_map_pristine;
    }

    map::RandomHeightMap& map ();
    const map::RandomHeightMap& map () const;

    terrain::TerrainProgram& program ();
    const terrain::TerrainProgram& program () const;
    const std::vector<map::TerrainCheckpoint>& checkpoints () const noexcept {
      return m_checkpoints;
    }
    const std::vector<terrain::TerrainTransformReport>&
    reports () const noexcept {
      return m_reports;
    }
    const std::optional<terrain::TrailNetwork>& trail_network () const;
    const TerrainLabEvaluationProgress& progress () const noexcept {
      return m_progress;
    }

    // Rebuild from the source, retaining one checkpoint for the input to
    // each transform.  Rerunning a suffix restores its saved input when it
    // is available, so a UI edit does not need to replay an unchanged prefix.
    void rebuild_program ();
    void rerun_program_from (std::size_t first_stage);

    // Restore the game map captured by begin().  Presentation code that
    // displays an external history snapshot can mark whether that snapshot is
    // the original map without duplicating model state.
    void restore_original_map ();
    void set_map_pristine (bool pristine) noexcept {
      m_map_pristine = pristine;
    }

  private:
    void require_active () const;
    void reset_progress (TerrainLabEvaluationProgress::Phase phase);
    void apply_from (std::size_t first_stage);

    map::RandomHeightMap* m_map = nullptr;
    std::unique_ptr<map::TerrainEvaluator> m_evaluator;
    terrain::TerrainProgram m_program = terrain::make_geological_program (0);
    std::optional<map::TerrainCheckpoint> m_original_map;
    std::vector<map::TerrainCheckpoint> m_checkpoints;
    std::vector<terrain::TerrainTransformReport> m_reports;
    TerrainLabEvaluationProgress m_progress;
    bool m_map_pristine = false;
  };
}

#endif
