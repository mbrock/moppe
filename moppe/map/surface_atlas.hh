#ifndef MOPPE_MAP_SURFACE_ATLAS_HH
#define MOPPE_MAP_SURFACE_ATLAS_HH

#include <moppe/map/surface_sections.hh>

#include <optional>
#include <stdexcept>
#include <utility>

namespace moppe::map {
  // The ground's finite intrinsic store. Geometry exists for the atlas's
  // whole lifetime; later analysis groups keep optional typed sections
  // instead of zero-filled columns plus an availability ledger.
  class SurfaceAtlas {
  public:
    class Hydrology {
    public:
      explicit Hydrology (SurfaceDomain domain)
          : m_domain (std::move (domain)) {}

      const SurfaceChannelFluxSections* channel_flux () const noexcept {
        return m_channel_flux ? &*m_channel_flux : nullptr;
      }

      const SurfaceMoistureSections* moisture () const noexcept {
        return m_moisture ? &*m_moisture : nullptr;
      }

      const SurfaceWaterlineSections* waterline () const noexcept {
        return m_waterline ? &*m_waterline : nullptr;
      }

      void set_channel_flux (SurfaceChannelFluxSections sections) {
        require_domain (sections.domain ());
        m_channel_flux = std::move (sections);
      }

      void set_moisture (SurfaceMoistureSections sections) {
        require_domain (sections.domain ());
        m_moisture = std::move (sections);
      }

      void set_waterline (SurfaceWaterlineSections sections) {
        require_domain (sections.domain ());
        m_waterline = std::move (sections);
      }

      void clear () noexcept {
        m_channel_flux.reset ();
        m_moisture.reset ();
        m_waterline.reset ();
      }

    private:
      void require_domain (const SurfaceDomain& domain) const {
        if (domain != m_domain)
          throw std::invalid_argument (
            "surface reading does not share the atlas domain");
      }

      SurfaceDomain m_domain;
      std::optional<SurfaceChannelFluxSections> m_channel_flux;
      std::optional<SurfaceMoistureSections> m_moisture;
      std::optional<SurfaceWaterlineSections> m_waterline;
    };

    class Geology {
    public:
      explicit Geology (SurfaceDomain domain) : m_domain (std::move (domain)) {}

      const SurfaceGeologySections* materials () const noexcept {
        return m_materials ? &*m_materials : nullptr;
      }

      void set_materials (SurfaceGeologySections sections) {
        if (sections.domain () != m_domain)
          throw std::invalid_argument (
            "surface geology does not share the atlas domain");
        m_materials = std::move (sections);
      }

      void clear () noexcept {
        m_materials.reset ();
      }

    private:
      SurfaceDomain m_domain;
      std::optional<SurfaceGeologySections> m_materials;
    };

    class Ecology {
    public:
      explicit Ecology (SurfaceDomain domain) : m_domain (std::move (domain)) {}

      const SurfaceHabitatSections* tree_habitat () const noexcept {
        return m_tree_habitat ? &*m_tree_habitat : nullptr;
      }

      const SurfaceForestSections* forest_cover () const noexcept {
        return m_forest_cover ? &*m_forest_cover : nullptr;
      }

      void set_tree_habitat (SurfaceHabitatSections sections) {
        if (sections.domain () != m_domain)
          throw std::invalid_argument (
            "surface habitat does not share the atlas domain");
        m_tree_habitat = std::move (sections);
      }

      void set_forest_cover (SurfaceForestSections sections) {
        if (sections.domain () != m_domain)
          throw std::invalid_argument (
            "surface forest does not share the atlas domain");
        m_forest_cover = std::move (sections);
      }

      void clear () noexcept {
        m_tree_habitat.reset ();
        m_forest_cover.reset ();
      }

    private:
      SurfaceDomain m_domain;
      std::optional<SurfaceHabitatSections> m_tree_habitat;
      std::optional<SurfaceForestSections> m_forest_cover;
    };

    class Use {
    public:
      explicit Use (SurfaceDomain domain) : m_domain (std::move (domain)) {}

      const SurfaceUseSections* readings () const noexcept {
        return m_readings ? &*m_readings : nullptr;
      }

      void set (SurfaceUseSections readings) {
        if (readings.domain () != m_domain)
          throw std::invalid_argument (
            "surface use does not share the atlas domain");
        m_readings = std::move (readings);
      }

      void clear () noexcept {
        m_readings.reset ();
      }

    private:
      SurfaceDomain m_domain;
      std::optional<SurfaceUseSections> m_readings;
    };

    explicit SurfaceAtlas (SurfaceDomain domain)
        : m_geometry (std::move (domain)), m_hydrology (m_geometry.domain ()),
          m_geology (m_geometry.domain ()), m_ecology (m_geometry.domain ()),
          m_use (m_geometry.domain ()) {}

    const SurfaceDomain& domain () const noexcept {
      return m_geometry.domain ();
    }

    const SurfaceGeometrySections& geometry () const noexcept {
      return m_geometry;
    }

    SurfaceGeometrySections& geometry () noexcept {
      return m_geometry;
    }

    const Hydrology& hydrology () const noexcept {
      return m_hydrology;
    }

    Hydrology& hydrology () noexcept {
      return m_hydrology;
    }

    const Geology& geology () const noexcept {
      return m_geology;
    }

    Geology& geology () noexcept {
      return m_geology;
    }

    const Ecology& ecology () const noexcept {
      return m_ecology;
    }

    Ecology& ecology () noexcept {
      return m_ecology;
    }

    const Use& use () const noexcept {
      return m_use;
    }

    Use& use () noexcept {
      return m_use;
    }

    void clear_derived () noexcept {
      m_hydrology.clear ();
      m_geology.clear ();
      m_ecology.clear ();
      m_use.clear ();
    }

  private:
    SurfaceGeometrySections m_geometry;
    Hydrology m_hydrology;
    Geology m_geology;
    Ecology m_ecology;
    Use m_use;
  };
}

#endif
