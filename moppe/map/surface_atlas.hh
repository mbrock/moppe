#ifndef MOPPE_MAP_SURFACE_ATLAS_HH
#define MOPPE_MAP_SURFACE_ATLAS_HH

#include <moppe/map/surface_sections.hh>

#include <optional>
#include <utility>

namespace moppe::map {
  // The ground's finite intrinsic store. Geometry exists after refresh;
  // later analysis groups keep their own optional typed sections instead of
  // leaving zero-filled columns plus a parallel availability ledger.
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

      SurfaceChannelFluxSections& materialize_channel_flux () {
        if (!m_channel_flux)
          m_channel_flux.emplace (m_domain);
        return *m_channel_flux;
      }

      SurfaceMoistureSections& materialize_moisture () {
        if (!m_moisture)
          m_moisture.emplace (m_domain);
        return *m_moisture;
      }

      SurfaceWaterlineSections& materialize_waterline () {
        if (!m_waterline)
          m_waterline.emplace (m_domain);
        return *m_waterline;
      }

      void clear () noexcept {
        m_channel_flux.reset ();
        m_moisture.reset ();
        m_waterline.reset ();
      }

    private:
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

      SurfaceGeologySections& materialize_materials () {
        if (!m_materials)
          m_materials.emplace (m_domain);
        return *m_materials;
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

      SurfaceHabitatSections& materialize_tree_habitat () {
        if (!m_tree_habitat)
          m_tree_habitat.emplace (m_domain);
        return *m_tree_habitat;
      }

      SurfaceForestSections& materialize_forest_cover () {
        if (!m_forest_cover)
          m_forest_cover.emplace (m_domain);
        return *m_forest_cover;
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

      const SurfaceTrailSections* trails () const noexcept {
        return m_trails ? &*m_trails : nullptr;
      }

      const SurfaceHomeBaseSections* home_base () const noexcept {
        return m_home_base ? &*m_home_base : nullptr;
      }

      SurfaceTrailSections& materialize_trails () {
        if (!m_trails)
          m_trails.emplace (m_domain);
        return *m_trails;
      }

      SurfaceHomeBaseSections& materialize_home_base () {
        if (!m_home_base)
          m_home_base.emplace (m_domain);
        return *m_home_base;
      }

      void clear () noexcept {
        m_trails.reset ();
        m_home_base.reset ();
      }

    private:
      SurfaceDomain m_domain;
      std::optional<SurfaceTrailSections> m_trails;
      std::optional<SurfaceHomeBaseSections> m_home_base;
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
