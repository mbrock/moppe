#ifndef MOPPE_TERRAIN_RASTER_HH
#define MOPPE_TERRAIN_RASTER_HH

#include <moppe/quantities.hh>

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace moppe::terrain {
  struct RasterDomain {
    std::size_t width;
    std::size_t height;
    float min_x = 0.0f;
    float max_x = 1.0f;
    float min_y = 0.0f;
    float max_y = 1.0f;

    friend bool operator== (const RasterDomain&, const RasterDomain&) = default;
  };

  class ScalarRaster {
  public:
    ScalarRaster (RasterDomain domain, std::vector<float> values);

    const RasterDomain& domain () const noexcept {
      return m_domain;
    }
    std::span<const float> values () const noexcept {
      return m_values;
    }

    float at (std::size_t x, std::size_t y) const;
    float min_value () const;
    float max_value () const;

  private:
    RasterDomain m_domain;
    std::vector<float> m_values;
  };

  template <auto R>
    requires mp_units::Reference<std::remove_cvref_t<decltype (R)>>
  class Raster {
  public:
    static constexpr auto reference = R;

    explicit Raster (ScalarRaster raster) : m_raster (std::move (raster)) {}

    const ScalarRaster& untyped () const noexcept {
      return m_raster;
    }
    const RasterDomain& domain () const noexcept {
      return m_raster.domain ();
    }
    std::span<const float> values () const noexcept {
      return m_raster.values ();
    }
    mp_units::quantity<R, float> sample (std::size_t x, std::size_t y) const {
      return m_raster.at (x, y) * R;
    }

  private:
    ScalarRaster m_raster;
  };
}

#endif
