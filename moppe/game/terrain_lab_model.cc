#include <moppe/game/terrain_lab_model.hh>

#include <algorithm>
#include <stdexcept>

namespace moppe::game {
  TerrainLabModel::~TerrainLabModel () = default;

  void
  TerrainLabModel::begin (map::RandomHeightMap& map,
                          const terrain::TerrainProgram& program,
                          const terrain::FieldEvaluator* source_evaluator) {
    if (active ())
      throw std::logic_error ("terrain lab model is already active");

    m_map = &map;
    m_evaluator =
      std::make_unique<map::TerrainEvaluator> (map, source_evaluator);
    m_program = program;
    m_original_map = m_evaluator->checkpoint ();
    m_checkpoints.clear ();
    m_reports.clear ();
    reset_progress (TerrainLabEvaluationProgress::Phase::Idle);
    m_map_pristine = true;
  }

  void TerrainLabModel::leave () {
    if (!active ())
      return;

    restore_original_map ();
    m_checkpoints.clear ();
    m_checkpoints.shrink_to_fit ();
    m_reports.clear ();
    m_reports.shrink_to_fit ();
    m_original_map.reset ();
    m_evaluator.reset ();
    m_map = nullptr;
    reset_progress (TerrainLabEvaluationProgress::Phase::Idle);
  }

  map::RandomHeightMap& TerrainLabModel::map () {
    require_active ();
    return *m_map;
  }

  const map::RandomHeightMap& TerrainLabModel::map () const {
    require_active ();
    return *m_map;
  }

  terrain::TerrainProgram& TerrainLabModel::program () {
    require_active ();
    return m_program;
  }

  const terrain::TerrainProgram& TerrainLabModel::program () const {
    require_active ();
    return m_program;
  }

  const std::optional<terrain::TrailNetwork>&
  TerrainLabModel::trail_network () const {
    require_active ();
    return m_evaluator->trail_network ();
  }

  void TerrainLabModel::rebuild_program () {
    require_active ();
    m_map_pristine = false;
    reset_progress (TerrainLabEvaluationProgress::Phase::Materializing);
    m_evaluator->begin (m_program,
                        [this] (std::size_t completed, std::size_t total) {
                          m_progress.source_rows_completed = completed;
                          m_progress.source_rows_total = total;
                        });
    m_checkpoints.clear ();
    m_reports.clear ();
    apply_from (0);
    m_progress.phase = TerrainLabEvaluationProgress::Phase::Idle;
  }

  void TerrainLabModel::rerun_program_from (std::size_t first_stage) {
    require_active ();
    const std::size_t stage_count = m_program.transforms.size ();
    if (first_stage > stage_count || first_stage > m_checkpoints.size () ||
        (first_stage == 0 && m_checkpoints.empty ())) {
      rebuild_program ();
      return;
    }

    m_map_pristine = false;
    reset_progress (TerrainLabEvaluationProgress::Phase::Applying);
    if (first_stage < m_checkpoints.size ())
      m_evaluator->restore (m_checkpoints[first_stage]);
    m_progress.completed_stages = first_stage;

    // Appending a transform starts from the current final map.  Other edits
    // restore their saved input and invalidate only the changed suffix.
    const std::size_t retained = std::min (stage_count, first_stage + 1);
    if (m_checkpoints.size () > retained)
      m_checkpoints.resize (retained);
    if (m_reports.size () > first_stage)
      m_reports.resize (first_stage);
    apply_from (first_stage);
    m_progress.phase = TerrainLabEvaluationProgress::Phase::Idle;
  }

  void TerrainLabModel::restore_original_map () {
    require_active ();
    if (!m_original_map)
      return;
    m_evaluator->restore (*m_original_map);
    m_map_pristine = true;
    reset_progress (TerrainLabEvaluationProgress::Phase::Idle);
  }

  void TerrainLabModel::require_active () const {
    if (!active ())
      throw std::logic_error ("terrain lab model is not active");
  }

  void
  TerrainLabModel::reset_progress (TerrainLabEvaluationProgress::Phase phase) {
    m_progress = { .phase = phase,
                   .source_rows_completed = 0,
                   .source_rows_total = 0,
                   .completed_stages = 0,
                   .total_stages = m_program.transforms.size (),
                   .current_stage = 0 };
  }

  void TerrainLabModel::apply_from (std::size_t first_stage) {
    for (std::size_t i = first_stage; i < m_program.transforms.size (); ++i) {
      m_progress.phase = TerrainLabEvaluationProgress::Phase::Applying;
      m_progress.current_stage = i;
      if (i >= m_checkpoints.size ())
        m_checkpoints.push_back (m_evaluator->checkpoint ());
      const terrain::TerrainTransformReport report =
        m_evaluator->apply (m_program.transforms[i]);
      if (i >= m_reports.size ())
        m_reports.push_back (report);
      else
        m_reports[i] = report;
      m_progress.completed_stages = i + 1;
    }
  }
}
