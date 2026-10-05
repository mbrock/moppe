#include <moppe/render/slug.hh>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace moppe {
  namespace render {
    namespace {
      float coordinate (OutlinePoint point, SlugAxis axis) {
        return axis == SlugAxis::X ? point.x : point.y;
      }

      float curve_min (const OutlineCurve& curve, SlugAxis axis) {
        return std::min ({ coordinate (curve.p1, axis),
                           coordinate (curve.p2, axis),
                           coordinate (curve.p3, axis) });
      }

      float curve_max (const OutlineCurve& curve, SlugAxis axis) {
        return std::max ({ coordinate (curve.p1, axis),
                           coordinate (curve.p2, axis),
                           coordinate (curve.p3, axis) });
      }

      // A ray parallel to a straight segment crosses it nowhere, so a
      // horizontal band omits curves that are flat in y and a vertical
      // band those flat in x.
      bool parallel (const OutlineCurve& curve, SlugAxis axis) {
        const float a = coordinate (curve.p1, axis);
        return a == coordinate (curve.p2, axis) &&
               a == coordinate (curve.p3, axis);
      }

      bool same_point (OutlinePoint a, OutlinePoint b) {
        return a.x == b.x && a.y == b.y;
      }

      // The inclusive range of bands one curve reaches.
      std::pair<int, int> band_range (const OutlineCurve& curve,
                                      SlugAxis axis,
                                      float minimum,
                                      float maximum,
                                      int count) {
        const float span = maximum - minimum;
        if (!(span > 0.0f))
          return { 0, 0 };
        const float size = span / count;
        const int low = static_cast<int> (std::floor (
          (curve_min (curve, axis) - minimum - slug_band_epsilon) / size));
        const int high = static_cast<int> (std::floor (
          (curve_max (curve, axis) - minimum + slug_band_epsilon) / size));
        return { std::clamp (low, 0, count - 1),
                 std::clamp (high, 0, count - 1) };
      }

      int fullest_band (std::span<const OutlineCurve> curves,
                        SlugAxis axis,
                        float minimum,
                        float maximum,
                        int count) {
        std::vector<int> loads (count, 0);
        for (const OutlineCurve& curve : curves) {
          if (parallel (curve, axis))
            continue;
          const auto [low, high] =
            band_range (curve, axis, minimum, maximum, count);
          for (int band = low; band <= high; ++band)
            ++loads[band];
        }
        return *std::max_element (loads.begin (), loads.end ());
      }

      // Each band's curve indices (into the glyph's own curve list),
      // sorted by descending maximum along the ray so the shader may stop
      // at the first curve wholly behind its sample.
      std::vector<std::vector<std::uint32_t>>
      make_bands (std::span<const OutlineCurve> curves,
                  SlugAxis membership,
                  float minimum,
                  float maximum,
                  int count) {
        const SlugAxis ray =
          membership == SlugAxis::Y ? SlugAxis::X : SlugAxis::Y;
        std::vector<std::vector<std::uint32_t>> bands (count);
        for (std::uint32_t index = 0; index < curves.size (); ++index) {
          if (parallel (curves[index], membership))
            continue;
          const auto [low, high] =
            band_range (curves[index], membership, minimum, maximum, count);
          for (int band = low; band <= high; ++band)
            bands[band].push_back (index);
        }
        for (std::vector<std::uint32_t>& band : bands)
          std::stable_sort (band.begin (),
                            band.end (),
                            [&] (std::uint32_t left, std::uint32_t right) {
                              return curve_max (curves[left], ray) >
                                     curve_max (curves[right], ray);
                            });
        return bands;
      }

      std::uint32_t root_code (float y1, float y2, float y3) {
        // Table 1 of the paper, packed as in the reference shader: three
        // sign classes select which of the two roots the ray really
        // crosses.  Zero counts as not positive, so a shared endpoint
        // lying exactly on the ray is classified identically by both
        // curves that meet there.
        const std::uint32_t shift =
          (y1 > 0.0f ? 0u : 1u) | (y2 > 0.0f ? 0u : 2u) | (y3 > 0.0f ? 0u : 4u);
        return (0x2E74u >> shift) & 0x0101u;
      }

      float saturate (float value) {
        return std::clamp (value, 0.0f, 1.0f);
      }

      // The ray-axis coordinates of the two roots where the curve, already
      // translated so the sample is the origin, crosses the other axis.
      std::pair<float, float>
      solve (OutlinePoint p1, OutlinePoint p2, OutlinePoint p3, SlugAxis ray) {
        const SlugAxis cross = ray == SlugAxis::X ? SlugAxis::Y : SlugAxis::X;
        const float a_ray = coordinate (p1, ray) - 2.0f * coordinate (p2, ray) +
                            coordinate (p3, ray);
        const float b_ray = coordinate (p1, ray) - coordinate (p2, ray);
        const float a = coordinate (p1, cross) - 2.0f * coordinate (p2, cross) +
                        coordinate (p3, cross);
        const float b = coordinate (p1, cross) - coordinate (p2, cross);
        const float c = coordinate (p1, cross);
        float t1, t2;
        if (std::fabs (a) < 1.0f / 65536.0f) {
          t1 = t2 = c * 0.5f / b;
        } else {
          const float d = std::sqrt (std::max (b * b - a * c, 0.0f));
          t1 = (b - d) / a;
          t2 = (b + d) / a;
        }
        const float origin = coordinate (p1, ray);
        return { (a_ray * t1 - b_ray * 2.0f) * t1 + origin,
                 (a_ray * t2 - b_ray * 2.0f) * t2 + origin };
      }
    }

    int choose_slug_band_count (std::span<const OutlineCurve> curves,
                                SlugAxis axis,
                                float minimum,
                                float maximum,
                                int maximum_count) {
      const int limit =
        std::max (1, std::min<int> (maximum_count, curves.size ()));
      int best_count = 1;
      int best_load = std::numeric_limits<int>::max ();
      for (int count = 1; count <= limit; ++count) {
        const int load = fullest_band (curves, axis, minimum, maximum, count);
        if (load < best_load) {
          best_count = count;
          best_load = load;
        }
      }
      return best_count;
    }

    SlugGlyph SlugAtlasBuilder::add (std::span<const OutlineContour> contours) {
      std::vector<OutlineCurve> curves;
      for (const OutlineContour& contour : contours) {
        for (std::size_t i = 0; i < contour.size (); ++i) {
          const OutlineCurve& next = contour[(i + 1) % contour.size ()];
          if (!same_point (contour[i].p3, next.p1))
            throw std::invalid_argument ("Slug contour is not closed");
        }
        curves.insert (curves.end (), contour.begin (), contour.end ());
      }

      SlugGlyph glyph;
      if (curves.empty ())
        return glyph;

      glyph.min_x = glyph.min_y = std::numeric_limits<float>::max ();
      glyph.max_x = glyph.max_y = std::numeric_limits<float>::lowest ();
      for (const OutlineCurve& curve : curves) {
        glyph.min_x = std::min (glyph.min_x, curve_min (curve, SlugAxis::X));
        glyph.min_y = std::min (glyph.min_y, curve_min (curve, SlugAxis::Y));
        glyph.max_x = std::max (glyph.max_x, curve_max (curve, SlugAxis::X));
        glyph.max_y = std::max (glyph.max_y, curve_max (curve, SlugAxis::Y));
      }

      // Curve texels: one per curve, plus an endpoint texel closing each
      // contour so the last curve also finds its p3 in the next texel.
      std::vector<std::uint32_t> texel_of (curves.size ());
      {
        std::size_t index = 0;
        for (const OutlineContour& contour : contours) {
          for (const OutlineCurve& curve : contour) {
            texel_of[index++] = m_data.curves.size ();
            m_data.curves.push_back (
              { curve.p1.x, curve.p1.y, curve.p2.x, curve.p2.y });
          }
          if (!contour.empty ())
            m_data.curves.push_back (
              { contour.back ().p3.x, contour.back ().p3.y, 0.0f, 0.0f });
        }
      }

      const int horizontal =
        choose_slug_band_count (curves, SlugAxis::Y, glyph.min_y, glyph.max_y);
      const int vertical =
        choose_slug_band_count (curves, SlugAxis::X, glyph.min_x, glyph.max_x);
      glyph.horizontal_bands = static_cast<std::uint16_t> (horizontal);
      glyph.vertical_bands = static_cast<std::uint16_t> (vertical);

      std::vector<std::vector<std::uint32_t>> bands =
        make_bands (curves, SlugAxis::Y, glyph.min_y, glyph.max_y, horizontal);
      {
        std::vector<std::vector<std::uint32_t>> columns =
          make_bands (curves, SlugAxis::X, glyph.min_x, glyph.max_x, vertical);
        bands.insert (bands.end (),
                      std::make_move_iterator (columns.begin ()),
                      std::make_move_iterator (columns.end ()));
      }

      // Reserve the header block, then write each band's list -- unless
      // its whole sorted list already stands as a contiguous run of one
      // written earlier, which neighbouring bands very often do.  Sharing
      // shrinks the buffer and keeps the cache warm.
      std::vector<std::uint32_t>& out = m_data.bands;
      glyph.band_offset = out.size ();
      out.resize (out.size () + 2 * bands.size (), 0u);
      std::vector<std::pair<std::size_t, std::size_t>> written;
      for (std::size_t band = 0; band < bands.size (); ++band) {
        std::vector<std::uint32_t> list;
        list.reserve (bands[band].size ());
        for (std::uint32_t curve : bands[band])
          list.push_back (texel_of[curve]);

        std::size_t offset = out.size ();
        bool shared = false;
        for (const auto& [start, length] : written) {
          const auto begin = out.begin () + start;
          const auto end = begin + length;
          const auto found =
            std::search (begin, end, list.begin (), list.end ());
          if (!list.empty () && found != end) {
            offset = static_cast<std::size_t> (found - out.begin ());
            shared = true;
            break;
          }
        }
        if (!shared) {
          if (!list.empty ())
            written.emplace_back (out.size (), list.size ());
          out.insert (out.end (), list.begin (), list.end ());
        }
        out[glyph.band_offset + 2 * band] = list.size ();
        out[glyph.band_offset + 2 * band + 1] = offset;
      }
      return glyph;
    }

    float slug_coverage (const SlugGlyphData& data,
                         const SlugGlyph& glyph,
                         OutlinePoint sample,
                         OutlinePoint pixels_per_em) {
      if (glyph.empty ())
        return 0.0f;

      const auto band_index = [] (float value, float low, float high, int n) {
        const float span = high - low;
        const int band =
          span > 0.0f ? static_cast<int> (std::floor ((value - low) / span * n))
                      : 0;
        return std::clamp (band, 0, n - 1);
      };
      const int row =
        band_index (sample.y, glyph.min_y, glyph.max_y, glyph.horizontal_bands);
      const int column =
        band_index (sample.x, glyph.min_x, glyph.max_x, glyph.vertical_bands);

      const auto traverse = [&] (int header, SlugAxis ray, float scale) {
        const SlugAxis cross = ray == SlugAxis::X ? SlugAxis::Y : SlugAxis::X;
        const std::uint32_t count = data.bands[glyph.band_offset + 2 * header];
        const std::uint32_t list =
          data.bands[glyph.band_offset + 2 * header + 1];
        float coverage = 0.0f;
        float weight = 0.0f;
        for (std::uint32_t i = 0; i < count; ++i) {
          const std::uint32_t texel = data.bands[list + i];
          const SlugCurveTexel& c = data.curves[texel];
          const SlugCurveTexel& n = data.curves[texel + 1];
          const OutlinePoint p1 { c.x1 - sample.x, c.y1 - sample.y };
          const OutlinePoint p2 { c.x2 - sample.x, c.y2 - sample.y };
          const OutlinePoint p3 { n.x1 - sample.x, n.y1 - sample.y };
          const float reach = std::max ({ coordinate (p1, ray),
                                          coordinate (p2, ray),
                                          coordinate (p3, ray) });
          if (reach * scale < -0.5f)
            break;
          const std::uint32_t code = root_code (coordinate (p1, cross),
                                                coordinate (p2, cross),
                                                coordinate (p3, cross));
          if (code == 0)
            continue;
          const auto [r1, r2] = solve (p1, p2, p3, ray);
          // The vertical ray sees the same crossing with its axes
          // exchanged, which reverses the sense of the two roots.
          const float sense = ray == SlugAxis::X ? 1.0f : -1.0f;
          if (code & 1u) {
            coverage += sense * saturate (r1 * scale + 0.5f);
            weight =
              std::max (weight, saturate (1.0f - std::fabs (r1 * scale) * 2));
          }
          if (code > 1u) {
            coverage -= sense * saturate (r2 * scale + 0.5f);
            weight =
              std::max (weight, saturate (1.0f - std::fabs (r2 * scale) * 2));
          }
        }
        return std::pair<float, float> { coverage, weight };
      };

      const auto [xcov, xwgt] = traverse (row, SlugAxis::X, pixels_per_em.x);
      const auto [ycov, ywgt] = traverse (
        glyph.horizontal_bands + column, SlugAxis::Y, pixels_per_em.y);
      const float coverage =
        std::max (std::fabs (xcov * xwgt + ycov * ywgt) /
                    std::max (xwgt + ywgt, 1.0f / 65536.0f),
                  std::min (std::fabs (xcov), std::fabs (ycov)));
      return saturate (coverage);
    }
  }
}
