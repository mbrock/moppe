#ifndef MOPPE_TERRAIN_EDITOR_HH
#define MOPPE_TERRAIN_EDITOR_HH

#include <moppe/terrain/program.hh>

#include <cstddef>
#include <optional>

namespace moppe::terrain {
  // A small adapter over the TerrainTransform variant.  It deliberately has
  // no transform-specific switchboard: each alternative supplies the editing
  // operations, so adding a transform means adding its own local semantics.
  class TerrainTransformEditor {
  public:
    explicit TerrainTransformEditor (const TerrainTransform& transform)
        : m_transform (&transform) {}
    explicit TerrainTransformEditor (TerrainTransform& transform)
        : m_transform (&transform), m_mutable_transform (&transform) {}

    std::size_t property_count () const;
    TransformProperty property (std::size_t index) const;
    float normalized_property (std::size_t index) const;
    bool set_normalized_property (std::size_t index, float value);
    bool adjust_natural_property (std::size_t index, int direction);

    // Some transform edits carry a source-wide invariant.  The program
    // editor consumes this capability without knowing transform alternatives.
    std::optional<float> source_sea_level () const;

  private:
    void require_mutable () const;

    const TerrainTransform* m_transform;
    TerrainTransform* m_mutable_transform = nullptr;
  };

  class GeologicalSourceEditor {
  public:
    explicit GeologicalSourceEditor (const GeologicalSource& source)
        : m_source (&source) {}
    explicit GeologicalSourceEditor (GeologicalSource& source)
        : m_source (&source), m_mutable_source (&source) {}

    std::size_t property_count () const;
    TransformProperty property (std::size_t index) const;
    float normalized_property (std::size_t index) const;
    bool set_normalized_property (std::size_t index, float value);
    bool adjust_natural_property (std::size_t index, int direction);

  private:
    void require_mutable () const;

    const GeologicalSource* m_source;
    GeologicalSource* m_mutable_source = nullptr;
  };

  // Owns the source/transform relationship during an edit, including the
  // orogeny sea-level invariant.  The Lab uses this instead of duplicating
  // normalized-property type checks in its UI code.
  class TerrainProgramEditor {
  public:
    explicit TerrainProgramEditor (const TerrainProgram& program)
        : m_program (&program) {}
    explicit TerrainProgramEditor (TerrainProgram& program)
        : m_program (&program), m_mutable_program (&program) {}

    GeologicalSourceEditor source ();
    GeologicalSourceEditor source () const;
    TerrainTransformEditor transform (std::size_t index);
    TerrainTransformEditor transform (std::size_t index) const;

    bool set_source_normalized_property (std::size_t index, float value);
    bool adjust_source_natural_property (std::size_t index, int direction);
    bool set_transform_normalized_property (std::size_t stage,
                                            std::size_t property,
                                            float value);
    bool adjust_transform_natural_property (std::size_t stage,
                                            std::size_t property,
                                            int direction);

  private:
    void require_mutable () const;
    void synchronize_source (const TerrainTransformEditor& editor);

    const TerrainProgram* m_program;
    TerrainProgram* m_mutable_program = nullptr;
  };
}

#endif
