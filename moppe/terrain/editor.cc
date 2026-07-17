#include <moppe/terrain/editor.hh>

#include <stdexcept>
#include <variant>

namespace moppe::terrain {
  std::size_t TerrainTransformEditor::property_count () const {
    return std::visit (
      [] (const auto& transform) { return transform.property_count (); },
      *m_transform);
  }

  TransformProperty TerrainTransformEditor::property (std::size_t index) const {
    return std::visit (
      [index] (const auto& transform) { return transform.property (index); },
      *m_transform);
  }

  float TerrainTransformEditor::normalized_property (std::size_t index) const {
    return std::visit (
      [index] (const auto& transform) {
        return transform.normalized_property (index);
      },
      *m_transform);
  }

  bool TerrainTransformEditor::set_normalized_property (std::size_t index,
                                                        float value) {
    require_mutable ();
    return std::visit (
      [index, value] (auto& transform) {
        return transform.set_normalized_property (index, value);
      },
      *m_mutable_transform);
  }

  bool TerrainTransformEditor::adjust_natural_property (std::size_t index,
                                                        int direction) {
    require_mutable ();
    return std::visit (
      [index, direction] (auto& transform) {
        return transform.adjust_natural_property (index, direction);
      },
      *m_mutable_transform);
  }

  std::optional<float> TerrainTransformEditor::source_sea_level () const {
    return std::visit (
      [] (const auto& transform) -> std::optional<float> {
        if constexpr (requires { transform.source_sea_level (); })
          return transform.source_sea_level ();
        return std::nullopt;
      },
      *m_transform);
  }

  void TerrainTransformEditor::require_mutable () const {
    if (!m_mutable_transform)
      throw std::logic_error ("terrain transform editor is read-only");
  }

  std::size_t GeologicalSourceEditor::property_count () const {
    return m_source->property_count ();
  }

  TransformProperty GeologicalSourceEditor::property (std::size_t index) const {
    return m_source->property (index);
  }

  float GeologicalSourceEditor::normalized_property (std::size_t index) const {
    return m_source->normalized_property (index);
  }

  bool GeologicalSourceEditor::set_normalized_property (std::size_t index,
                                                        float value) {
    require_mutable ();
    return m_mutable_source->set_normalized_property (index, value);
  }

  bool GeologicalSourceEditor::adjust_natural_property (std::size_t index,
                                                        int direction) {
    require_mutable ();
    return m_mutable_source->adjust_natural_property (index, direction);
  }

  void GeologicalSourceEditor::require_mutable () const {
    if (!m_mutable_source)
      throw std::logic_error ("geological source editor is read-only");
  }

  GeologicalSourceEditor TerrainProgramEditor::source () {
    if (m_mutable_program)
      return GeologicalSourceEditor (m_mutable_program->source);
    return GeologicalSourceEditor (m_program->source);
  }

  GeologicalSourceEditor TerrainProgramEditor::source () const {
    return GeologicalSourceEditor (m_program->source);
  }

  TerrainTransformEditor TerrainProgramEditor::transform (std::size_t index) {
    if (m_mutable_program) {
      if (index >= m_mutable_program->transforms.size ())
        throw std::out_of_range ("terrain transform index is invalid");
      return TerrainTransformEditor (m_mutable_program->transforms[index]);
    }
    return static_cast<const TerrainProgramEditor&> (*this).transform (index);
  }

  TerrainTransformEditor
  TerrainProgramEditor::transform (std::size_t index) const {
    if (index >= m_program->transforms.size ())
      throw std::out_of_range ("terrain transform index is invalid");
    return TerrainTransformEditor (m_program->transforms[index]);
  }

  bool TerrainProgramEditor::set_source_normalized_property (std::size_t index,
                                                             float value) {
    require_mutable ();
    return source ().set_normalized_property (index, value);
  }

  bool TerrainProgramEditor::adjust_source_natural_property (std::size_t index,
                                                             int direction) {
    require_mutable ();
    return source ().adjust_natural_property (index, direction);
  }

  bool TerrainProgramEditor::set_transform_normalized_property (
    std::size_t stage, std::size_t property, float value) {
    require_mutable ();
    TerrainTransformEditor editor = transform (stage);
    if (!editor.set_normalized_property (property, value))
      return false;
    synchronize_source (editor);
    return true;
  }

  bool TerrainProgramEditor::adjust_transform_natural_property (
    std::size_t stage, std::size_t property, int direction) {
    require_mutable ();
    TerrainTransformEditor editor = transform (stage);
    if (!editor.adjust_natural_property (property, direction))
      return false;
    synchronize_source (editor);
    return true;
  }

  void TerrainProgramEditor::require_mutable () const {
    if (!m_mutable_program)
      throw std::logic_error ("terrain program editor is read-only");
  }

  void TerrainProgramEditor::synchronize_source (
    const TerrainTransformEditor& editor) {
    if (const std::optional<float> sea_level = editor.source_sea_level ())
      m_mutable_program->source.sea_level = *sea_level;
  }
}
