#include <moppe/terrain/raster.hh>

#include <algorithm>
#include <stdexcept>

namespace moppe::terrain {
  ScalarRaster::ScalarRaster (RasterDomain domain, std::vector<float> values)
      : m_domain (domain), m_values (std::move (values)) {
    if (m_values.size () != m_domain.width * m_domain.height)
      throw std::invalid_argument (
        "raster value count does not match its domain");
  }

  float ScalarRaster::at (std::size_t x, std::size_t y) const {
    if (x >= m_domain.width || y >= m_domain.height)
      throw std::out_of_range ("raster coordinate is out of range");
    return m_values[y * m_domain.width + x];
  }

  float ScalarRaster::min_value () const {
    return *std::ranges::min_element (m_values);
  }

  float ScalarRaster::max_value () const {
    return *std::ranges::max_element (m_values);
  }
}
