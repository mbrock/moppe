// Slug: glyph coverage computed per pixel from quadratic outlines.
//
// After Eric Lengyel's reference shaders (github.com/EricLengyel/Slug,
// MIT/Apache-2.0; patent dedicated to the public domain in 2026) and the
// Lisp port in luv.  Each glyph instance becomes one quad, dilated so every
// pixel its filter can touch is rasterized.  A fragment casts one ray along
// +x and one along +y through the curves of the band it falls in, sums a
// signed, filtered crossing for each eligible root, and blends the two
// estimates by how squarely each ray met its nearest edge.  The CPU mirror
// of this file is render::slug_coverage (moppe/render/slug.cc).

#include "common.h"

struct SlugVaryings {
  float4 position [[position]];
  float2 em;                    // outline coordinate, interpolated
  float4 bounds [[flat]];       // exact em bounds for band selection
  float4 color [[flat]];        // straight alpha
  uint4 glyph [[flat]];         // band offset, h bands, v bands
  float filter_pixels [[flat]]; // coverage ramp width
};

// The six corners of two triangles over the unit square.
constant float2 slug_corners[6] = {
  float2 (0, 0), float2 (1, 0), float2 (0, 1),
  float2 (0, 1), float2 (1, 0), float2 (1, 1),
};

vertex SlugVaryings slug_hud_vertex (uint vid [[vertex_id]],
                                     uint iid [[instance_id]],
                                     const device MoppeGlyphQuad* quads
                                     [[buffer (MOPPE_BUF_GLYPH_QUADS)]],
                                     constant MoppeHudUniforms& hud
                                     [[buffer (MOPPE_BUF_FRAME)]]) {
  const MoppeGlyphQuad q = quads[iid];
  const float2 corner = slug_corners[vid];
  const float filter_pixels = max (q.origin.w, 1.0);

  // Grow the quad past the outline by half the filter and a little more,
  // measured in em along each axis from the axis's own screen length.  A
  // flat screen quad has one pixel size everywhere, so this per-vertex
  // dilation is exact; a world-space variant would size it from depth.
  const float pixels_per_point = max (hud.params.y, 1e-3);
  const float2 pixels_per_em =
    max (float2 (length (q.axis_x.xy), length (q.axis_y.xy)) * pixels_per_point,
         float2 (1e-3));
  const float2 dilation = (0.5 * filter_pixels + 0.75) / pixels_per_em;
  const float2 em =
    mix (q.bounds.xy - dilation, q.bounds.zw + dilation, corner);

  const float2 point = q.origin.xy + em.x * q.axis_x.xy + em.y * q.axis_y.xy;
  SlugVaryings out;
  out.position = hud.proj * float4 (point, 0.0, 1.0);
  out.em = em;
  out.bounds = q.bounds;
  out.color = q.color;
  out.glyph = q.glyph;
  out.filter_pixels = filter_pixels;
  return out;
}

// Which of a quadratic's two roots a ray really crosses, from the strict
// signs of its three control coordinates (Table 1 of the paper, packed as
// in the reference).  Bit 0 enables the first root and bit 8 the second.
static inline uint slug_root_code (float y1, float y2, float y3) {
  const uint shift =
    (y1 > 0.0 ? 0u : 1u) | (y2 > 0.0 ? 0u : 2u) | (y3 > 0.0 ? 0u : 4u);
  return (0x2E74u >> shift) & 0x0101u;
}

// Where the curve (translated so the sample is the origin) crosses the
// horizontal line y = 0: the x coordinates of its two roots.
static inline float2 slug_solve_horizontal (float2 p1, float2 p2, float2 p3) {
  const float2 a = p1 - p2 * 2.0 + p3;
  const float2 b = p1 - p2;
  float t1, t2;
  if (abs (a.y) < 1.0 / 65536.0) {
    t1 = t2 = p1.y * 0.5 / b.y;
  } else {
    const float d = sqrt (max (b.y * b.y - a.y * p1.y, 0.0));
    t1 = (b.y - d) / a.y;
    t2 = (b.y + d) / a.y;
  }
  return float2 ((a.x * t1 - b.x * 2.0) * t1 + p1.x,
                 (a.x * t2 - b.x * 2.0) * t2 + p1.x);
}

static inline float2 slug_solve_vertical (float2 p1, float2 p2, float2 p3) {
  return slug_solve_horizontal (p1.yx, p2.yx, p3.yx);
}

static inline uint slug_band (float value, float low, float high, uint n) {
  const float span = high - low;
  const float band = span > 0.0 ? floor ((value - low) / span * float (n)) : 0;
  return uint (clamp (band, 0.0, float (n - 1)));
}

fragment float4 slug_hud_fragment (SlugVaryings in [[stage_in]],
                                   constant MoppeHudUniforms& hud
                                   [[buffer (MOPPE_BUF_FRAME)]],
                                   const device float4* curves
                                   [[buffer (MOPPE_BUF_GLYPH_CURVES)]],
                                   const device uint* bands
                                   [[buffer (MOPPE_BUF_GLYPH_BANDS)]]) {
  const float2 em = in.em;
  // Pixels per em along each outline axis, already divided by the filter
  // width, so the rest works in filter widths.  fwidth matches the
  // reference; for unrotated text it is the exact pixel footprint.
  const float2 pixels_per_em =
    1.0 / max (fwidth (em) * in.filter_pixels, float2 (1.0 / 65536.0));

  const uint base = in.glyph.x;
  const uint row = slug_band (em.y, in.bounds.y, in.bounds.w, in.glyph.y);
  const uint column = slug_band (em.x, in.bounds.x, in.bounds.z, in.glyph.z);

  // Horizontal ray toward +x.  The band is sorted by descending maximum x,
  // so the walk ends at the first curve wholly left of the filter.
  float xcov = 0.0, xwgt = 0.0;
  {
    const uint count = bands[base + 2 * row];
    const uint list = bands[base + 2 * row + 1];
    for (uint i = 0; i < count; ++i) {
      const uint texel = bands[list + i];
      const float4 c = curves[texel];
      const float2 p1 = c.xy - em;
      const float2 p2 = c.zw - em;
      const float2 p3 = curves[texel + 1].xy - em;
      if (max (max (p1.x, p2.x), p3.x) * pixels_per_em.x < -0.5)
        break;
      const uint code = slug_root_code (p1.y, p2.y, p3.y);
      if (code == 0u)
        continue;
      const float2 r = slug_solve_horizontal (p1, p2, p3) * pixels_per_em.x;
      if ((code & 1u) != 0u) {
        xcov += saturate (r.x + 0.5);
        xwgt = max (xwgt, saturate (1.0 - abs (r.x) * 2.0));
      }
      if (code > 1u) {
        xcov -= saturate (r.y + 0.5);
        xwgt = max (xwgt, saturate (1.0 - abs (r.y) * 2.0));
      }
    }
  }

  // Vertical ray toward +y.  Exchanging the axes reverses the sense of
  // the two roots.
  float ycov = 0.0, ywgt = 0.0;
  {
    const uint header = in.glyph.y + column;
    const uint count = bands[base + 2 * header];
    const uint list = bands[base + 2 * header + 1];
    for (uint i = 0; i < count; ++i) {
      const uint texel = bands[list + i];
      const float4 c = curves[texel];
      const float2 p1 = c.xy - em;
      const float2 p2 = c.zw - em;
      const float2 p3 = curves[texel + 1].xy - em;
      if (max (max (p1.y, p2.y), p3.y) * pixels_per_em.y < -0.5)
        break;
      const uint code = slug_root_code (p1.x, p2.x, p3.x);
      if (code == 0u)
        continue;
      const float2 r = slug_solve_vertical (p1, p2, p3) * pixels_per_em.y;
      if ((code & 1u) != 0u) {
        ycov -= saturate (r.x + 0.5);
        ywgt = max (ywgt, saturate (1.0 - abs (r.x) * 2.0));
      }
      if (code > 1u) {
        ycov += saturate (r.y + 0.5);
        ywgt = max (ywgt, saturate (1.0 - abs (r.y) * 2.0));
      }
    }
  }

  // Trust most the ray whose crossing passed closest to the pixel centre;
  // the minimum term keeps coverage where neither ray met an edge nearby.
  // Nonzero fill.
  const float coverage = saturate (
    max (abs (xcov * xwgt + ycov * ywgt) / max (xwgt + ywgt, 1.0 / 65536.0),
         min (abs (xcov), abs (ycov))));

  float4 color = in.color;
  if (hud.params.x > 0.5)
    color.rgb = pow (color.rgb, 2.2);
  color.a *= coverage;
  return color;
}
