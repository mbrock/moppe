// The one translation unit that carries the metal-cpp implementation.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "atelier/atelier_renderer.hh"

#include "atelier/metal.hh"
#include "atelier/pipeline.hh"
#include "atelier/surface.hh"

#include <chrono>

namespace atelier {
  namespace {
    bool is_tree_scene (SceneKind scene) {
      return scene == SceneKind::tree_wind || scene == SceneKind::tree_diagram;
    }

    TreeEmbeddingKind tree_embedding (SceneKind scene) {
      return scene == SceneKind::tree_diagram ? TreeEmbeddingKind::diagram
                                              : TreeEmbeddingKind::wind_bent;
    }

    EmbeddingKind sheet_embedding (SceneKind scene) {
      switch (scene) {
      case SceneKind::rope_bridge:
        return EmbeddingKind::rope_bridge;
      case SceneKind::toroidal_sheet:
        return EmbeddingKind::toroidal_shell;
      case SceneKind::repeating_sheet:
        return EmbeddingKind::repeating_plane;
      case SceneKind::tree_wind:
      case SceneKind::tree_diagram:
        break;
      }
      throw std::invalid_argument ("A tree scene has no sheet embedding");
    }
  }

  class Renderer::Impl {
  public:
    explicit Impl (SceneKind scene)
        : m_surface (m_metal.device ()), m_tiles (m_metal),
          m_sheet (is_tree_scene (scene)
                     ? SheetBoundary::open
                     : boundary_for (sheet_embedding (scene))),
          m_scene (scene), m_started_at (Clock::now ()) {
      m_metal.use_residency_set (m_surface.residency_set ());
    }

    void* native_layer () const {
      return m_surface.native_layer ();
    }

    void resize (Viewport viewport) {
      m_surface.resize (m_metal, viewport);
    }

    void draw () {
      if (!m_surface.has_render_target ())
        return;
      CA::MetalDrawable* drawable = m_surface.next_drawable ();
      if (!drawable)
        return;

      const Duration elapsed = Clock::now () - m_started_at;
      const FrameSlot slot = m_metal.begin_frame ();
      Frame frame;
      if (is_tree_scene (m_scene)) {
        frame = compose_frame (
          m_tree, tree_embedding (m_scene), elapsed, m_surface.viewport ());
      } else {
        m_sheet.advance (elapsed);
        frame = compose_frame (
          m_sheet, sheet_embedding (m_scene), elapsed, m_surface.viewport ());
      }
      m_tiles.prepare (slot, frame);

      const auto pass = m_surface.make_render_pass (drawable->texture ());
      m_tiles.encode (m_metal.command_buffer (), pass.get ());
      m_metal.submit (drawable, slot);
    }

    Image capture (Duration elapsed) {
      const Viewport viewport = m_surface.viewport ();
      if (!m_surface.has_render_target ())
        throw std::runtime_error ("Cannot capture an unsized surface");

      const auto target = m_surface.make_capture_target (m_metal);
      const FrameSlot slot = m_metal.begin_frame ();
      if (is_tree_scene (m_scene)) {
        const Tree tree;
        m_tiles.prepare (
          slot,
          compose_frame (tree, tree_embedding (m_scene), elapsed, viewport));
      } else {
        HexSheet sheet (boundary_for (sheet_embedding (m_scene)));
        sheet.advance (elapsed);
        m_tiles.prepare (
          slot,
          compose_frame (sheet, sheet_embedding (m_scene), elapsed, viewport));
      }
      const auto pass = m_surface.make_render_pass (target.get ());
      m_tiles.encode (m_metal.command_buffer (), pass.get ());
      m_metal.submit_offscreen (slot);
      m_metal.wait_until_idle ();

      Image image {
        .width = viewport.width,
        .height = viewport.height,
        .bgra = std::make_unique<std::uint8_t[]> (4 * viewport.width *
                                                  viewport.height),
      };
      target->getBytes (image.bgra.get (),
                        4 * viewport.width,
                        MTL::Region (0, 0, viewport.width, viewport.height),
                        0);
      m_metal.remove_resident (target.get ());
      m_metal.commit_residency ();
      return image;
    }

  private:
    using Clock = std::chrono::steady_clock;

    MetalExecution m_metal;
    RenderSurface m_surface;
    TilePipeline m_tiles;
    HexSheet m_sheet;
    Tree m_tree;
    SceneKind m_scene;
    Clock::time_point m_started_at;
  };

  Renderer::Renderer (SceneKind scene)
      : m_impl (std::make_unique<Impl> (scene)) {}

  Renderer::~Renderer () = default;

  void* Renderer::native_layer () const {
    return m_impl->native_layer ();
  }

  void Renderer::resize (Viewport viewport) {
    m_impl->resize (viewport);
  }

  void Renderer::draw () {
    m_impl->draw ();
  }

  Image Renderer::capture (Duration elapsed) {
    return m_impl->capture (elapsed);
  }
}
