#include <moppe/map/terrain_evaluator.hh>

#include <algorithm>
#include <stdexcept>
#include <variant>

namespace moppe::map {
  TerrainEvaluator::TerrainEvaluator
    (RandomHeightMap& target,
     const terrain::FieldEvaluator* source_evaluator)
    : m_target (target), m_source_evaluator (source_evaluator)
  { }

  void
  TerrainEvaluator::begin (const terrain::TerrainProgram& program)
  {
    terrain::validate_program (program);
    m_randomness.seed (program.randomness.seed);
    m_randomness.discard (program.randomness.offset);
    const terrain::GeologicalFields fields =
      terrain::make_geological_fields (program.source.recipe);
    const terrain::ScalarField field =
      terrain::geological_layer (fields, program.source.layer);
    if (m_source_evaluator)
      m_target.materialize (field, *m_source_evaluator);
    else
      m_target.materialize (field);
  }

  void
  TerrainEvaluator::apply (const terrain::TerrainTransform& transform)
  {
    if (std::holds_alternative<terrain::NormalizeHeights> (transform))
      m_target.normalize ();
    else if (const auto* power =
	     std::get_if<terrain::PowerHeights> (&transform))
      m_target.exponentiate (power->exponent);
    else if (const auto* hydraulic =
	     std::get_if<terrain::HydraulicErosion> (&transform))
      m_target.erode_hydraulically
	(m_randomness, hydraulic->droplets, hydraulic->batch_size);
    else if (const auto* thermal =
	     std::get_if<terrain::ThermalErosion> (&transform))
      m_target.erode_thermally (thermal->iterations, thermal->talus);
    m_target.synchronize_periodic_edges ();
  }

  void
  TerrainEvaluator::evaluate
    (const terrain::TerrainProgram& program, const Progress& progress)
  {
    begin (program);
    for (std::size_t i = 0; i < program.transforms.size (); ++i) {
      if (progress)
	progress (i, program.transforms[i]);
      apply (program.transforms[i]);
    }
  }

  TerrainCheckpoint
  TerrainEvaluator::checkpoint () const
  {
    const std::size_t count = static_cast<std::size_t>
      (m_target.width ()) * m_target.height ();
    return {
      .heights = std::vector<float>
	(m_target.raw_heights (), m_target.raw_heights () + count),
      .randomness = m_randomness
    };
  }

  void
  TerrainEvaluator::restore (const TerrainCheckpoint& checkpoint)
  {
    const std::size_t expected = static_cast<std::size_t>
      (m_target.width ()) * m_target.height ();
    if (checkpoint.heights.size () != expected)
      throw std::invalid_argument
	("terrain checkpoint dimensions do not match target");
    std::copy (checkpoint.heights.begin (), checkpoint.heights.end (),
	       m_target.raw_heights ());
    m_randomness = checkpoint.randomness;
  }
}
