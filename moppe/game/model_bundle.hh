#ifndef MOPPE_GAME_MODEL_BUNDLE_HH
#define MOPPE_GAME_MODEL_BUNDLE_HH

#include <moppe/gfx/vector.hh>
#include <moppe/quantities.hh>
#include <moppe/spatial/bundle.hh>
#include <moppe/spatial/bundle_storage.hh>

#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>

namespace moppe::game::model_mesh {
  // Finite domains for a rigid model's Arrow bundles: one file per domain
  // under data/models/<model>/{vertices,triangles,paints[,film]}.arrows.
  struct VertexDomain {
    using index_type = std::size_t;

    std::size_t count = 0;

    std::size_t size () const noexcept {
      return count;
    }
    std::size_t offset (index_type index) const noexcept {
      return index;
    }
    index_type index (std::size_t offset) const noexcept {
      return offset;
    }

    friend bool operator== (const VertexDomain&, const VertexDomain&) = default;
  };

  struct TriangleDomain {
    using index_type = std::size_t;

    std::size_t count = 0;

    std::size_t size () const noexcept {
      return count;
    }
    std::size_t offset (index_type index) const noexcept {
      return index;
    }
    index_type index (std::size_t offset) const noexcept {
      return offset;
    }

    friend bool operator== (const TriangleDomain&,
                            const TriangleDomain&) = default;
  };

  struct PaintDomain {
    using index_type = std::size_t;

    std::size_t count = 0;

    std::size_t size () const noexcept {
      return count;
    }
    std::size_t offset (index_type index) const noexcept {
      return index;
    }
    index_type index (std::size_t offset) const noexcept {
      return offset;
    }

    friend bool operator== (const PaintDomain&, const PaintDomain&) = default;
  };

  using VertexPosition =
    mp_units::quantity<vertex_position[mp_units::si::metre], Vec3>;
  using VertexNormal = mp_units::quantity<vertex_normal[mp_units::one], Vec3>;
  using FilmCoordinates =
    mp_units::quantity<film_coordinates[mp_units::si::metre], Vec2>;
  using SurfaceWear = mp_units::quantity<surface_wear[mp_units::one], float>;

  using CornerA = mp_units::quantity<corner_a[mp_units::one], std::uint16_t>;
  using CornerB = mp_units::quantity<corner_b[mp_units::one], std::uint16_t>;
  using CornerC = mp_units::quantity<corner_c[mp_units::one], std::uint16_t>;
  using PaintIndex =
    mp_units::quantity<paint_index[mp_units::one], std::uint8_t>;

  using PaintColour = mp_units::quantity<paint_colour[mp_units::one], Vec3>;
  using PaintUnlit =
    mp_units::quantity<paint_unlit[mp_units::one], std::uint8_t>;
  using PaintMetallic =
    mp_units::quantity<paint_metallic[mp_units::one], float>;
  using PaintRoughness =
    mp_units::quantity<paint_roughness[mp_units::one], float>;
  using PaintCoat = mp_units::quantity<paint_coat[mp_units::one], float>;
  using PaintCoatRoughness =
    mp_units::quantity<paint_coat_roughness[mp_units::one], float>;
  using PaintTransmission =
    mp_units::quantity<paint_transmission[mp_units::one], float>;
  using PaintIor = mp_units::quantity<paint_ior[mp_units::one], float>;
  using PaintSheen = mp_units::quantity<paint_sheen[mp_units::one], float>;
  using PaintEmission =
    mp_units::quantity<paint_emission[mp_units::one], float>;

  using VertexBundle =
    spatial::Bundle<VertexDomain, VertexPosition, VertexNormal>;
  using FilmBundle =
    spatial::Bundle<VertexDomain, FilmCoordinates, SurfaceWear>;
  using TriangleBundle =
    spatial::Bundle<TriangleDomain, CornerA, CornerB, CornerC, PaintIndex>;
  using PaintBundle = spatial::Bundle<PaintDomain,
                                      PaintColour,
                                      PaintUnlit,
                                      PaintMetallic,
                                      PaintRoughness,
                                      PaintCoat,
                                      PaintCoatRoughness,
                                      PaintTransmission,
                                      PaintIor,
                                      PaintSheen,
                                      PaintEmission>;
}

namespace moppe::spatial {
  template <>
  struct DomainStorage<game::model_mesh::VertexDomain> {
    static void write (std::ostream& out,
                       const game::model_mesh::VertexDomain& domain) {
      detail::write_scalar (out, static_cast<std::uint64_t> (domain.count));
    }

    static std::optional<game::model_mesh::VertexDomain>
    read (std::istream& in) {
      std::uint64_t count = 0;
      if (!detail::read_scalar (in, count))
        return std::nullopt;
      return game::model_mesh::VertexDomain { static_cast<std::size_t> (
        count) };
    }
  };

  template <>
  struct DomainStorage<game::model_mesh::TriangleDomain> {
    static void write (std::ostream& out,
                       const game::model_mesh::TriangleDomain& domain) {
      detail::write_scalar (out, static_cast<std::uint64_t> (domain.count));
    }

    static std::optional<game::model_mesh::TriangleDomain>
    read (std::istream& in) {
      std::uint64_t count = 0;
      if (!detail::read_scalar (in, count))
        return std::nullopt;
      return game::model_mesh::TriangleDomain { static_cast<std::size_t> (
        count) };
    }
  };

  template <>
  struct DomainStorage<game::model_mesh::PaintDomain> {
    static void write (std::ostream& out,
                       const game::model_mesh::PaintDomain& domain) {
      detail::write_scalar (out, static_cast<std::uint64_t> (domain.count));
    }

    static std::optional<game::model_mesh::PaintDomain>
    read (std::istream& in) {
      std::uint64_t count = 0;
      if (!detail::read_scalar (in, count))
        return std::nullopt;
      return game::model_mesh::PaintDomain { static_cast<std::size_t> (count) };
    }
  };
}

#endif
