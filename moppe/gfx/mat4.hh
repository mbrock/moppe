#ifndef MOPPE_MAT4_HH
#define MOPPE_MAT4_HH

#include <moppe/gfx/math.hh>

#include <cmath>
#include <cstddef>
#include <type_traits>

namespace moppe {
  // Column-major 4x4 matrix (GL-style layout: translation in elements 12..14).
  // The byte layout matches MSL float4x4. Apple builds use the native SIMD
  // matrix directly; portable builds retain the same 16-float layout.
  //
  // Conventions: right-handed eye space looking down -Z; clip-space
  // depth is Metal's [0,1].  A * B applies B first, like the GL
  // matrix stack.
  class Mat4 {
  public:
    Mat4 () {
#ifdef __APPLE__
      m_value = matrix_identity_float4x4;
#else
      for (int i = 0; i < 16; ++i)
        m_value[i] = (i % 5 == 0) ? 1.0f : 0.0f;
#endif
    }

    float at (int col, int row) const {
#ifdef __APPLE__
      return m_value.columns[col][row];
#else
      return m_value[col * 4 + row];
#endif
    }

    void set (int col, int row, float value) {
#ifdef __APPLE__
      m_value.columns[col][row] = value;
#else
      m_value[col * 4 + row] = value;
#endif
    }

    float element (int index) const {
      return at (index / 4, index % 4);
    }

    void set_element (int index, float value) {
      set (index / 4, index % 4, value);
    }

    const void* bytes () const {
      return &m_value;
    }

#ifdef __APPLE__
    const simd_float4x4& native () const {
      return m_value;
    }
#endif

    static Mat4 identity () {
      return Mat4 ();
    }

    static Mat4 translation (const Vec3& v) {
      Mat4 r;
      r.set_element (12, v[0]);
      r.set_element (13, v[1]);
      r.set_element (14, v[2]);
      return r;
    }

    static Mat4 scaling (const Vec3& v) {
      Mat4 r;
      r.set_element (0, v[0]);
      r.set_element (5, v[1]);
      r.set_element (10, v[2]);
      return r;
    }

    // Rotation about an arbitrary (not necessarily unit) axis.
    static Mat4 rotation (radians_t angle, const Vec3& axis) {
      const Vec3 a = normalized (axis);
      const float c = cos (angle), s = sin (angle);
      const float t = 1.0f - c;

      Mat4 r;
      r.set_element (0, t * a[0] * a[0] + c);
      r.set_element (1, t * a[0] * a[1] + s * a[2]);
      r.set_element (2, t * a[0] * a[2] - s * a[1]);
      r.set_element (4, t * a[0] * a[1] - s * a[2]);
      r.set_element (5, t * a[1] * a[1] + c);
      r.set_element (6, t * a[1] * a[2] + s * a[0]);
      r.set_element (8, t * a[0] * a[2] + s * a[1]);
      r.set_element (9, t * a[1] * a[2] - s * a[0]);
      r.set_element (10, t * a[2] * a[2] + c);
      return r;
    }

    // Columns of the upper-left 3x3 (a basis frame) plus an origin.
    static Mat4 basis (const Vec3& x,
                       const Vec3& y,
                       const Vec3& z,
                       const Vec3& origin = Vec3 ()) {
      Mat4 r;
      r.set_element (0, x[0]);
      r.set_element (1, x[1]);
      r.set_element (2, x[2]);
      r.set_element (4, y[0]);
      r.set_element (5, y[1]);
      r.set_element (6, y[2]);
      r.set_element (8, z[0]);
      r.set_element (9, z[1]);
      r.set_element (10, z[2]);
      r.set_element (12, origin[0]);
      r.set_element (13, origin[1]);
      r.set_element (14, origin[2]);
      return r;
    }

    // View matrix a la gluLookAt.
    static Mat4 look_at (const Vec3& eye, const Vec3& center, const Vec3& up) {
      const Vec3 f = normalized (center - eye);
      const Vec3 s = normalized (cross (f, up));
      const Vec3 u = cross (s, f);

      Mat4 r;
      r.set_element (0, s[0]);
      r.set_element (4, s[1]);
      r.set_element (8, s[2]);
      r.set_element (1, u[0]);
      r.set_element (5, u[1]);
      r.set_element (9, u[2]);
      r.set_element (2, -f[0]);
      r.set_element (6, -f[1]);
      r.set_element (10, -f[2]);
      r.set_element (12, -dot (s, eye));
      r.set_element (13, -dot (u, eye));
      r.set_element (14, dot (f, eye));
      return r;
    }

    // Perspective projection with reversed Z: near maps to 1, far to
    // 0.  Pair with a GREATER_EQUAL depth test and a clear value of 0.
    static Mat4
    perspective_reversed (radians_t fovy, float aspect, float near, float far) {
      const float f = 1.0f / tan (fovy / 2);
      Mat4 r;
      r.set_element (0, f / aspect);
      r.set_element (5, f);
      r.set_element (10, near / (far - near));
      r.set_element (11, -1.0f);
      r.set_element (14, near * far / (far - near));
      r.set_element (15, 0.0f);
      return r;
    }

    // Orthographic projection, standard [0,1] depth (near -> 0).
    static Mat4
    ortho (float l, float r_, float b, float t, float near, float far) {
      Mat4 r;
      r.set_element (0, 2 / (r_ - l));
      r.set_element (5, 2 / (t - b));
      r.set_element (10, -1 / (far - near));
      r.set_element (12, -(r_ + l) / (r_ - l));
      r.set_element (13, -(t + b) / (t - b));
      r.set_element (14, -near / (far - near));
      return r;
    }

    // Pixel-coordinate 2D projection for the HUD: x in [0,w] left to
    // right, y in [0,h] TOP to bottom, z pinned mid-range.
    static Mat4 hud_ortho (float w, float h) {
      Mat4 r;
      r.set_element (0, 2 / w);
      r.set_element (5, -2 / h);
      r.set_element (10, 0);
      r.set_element (12, -1);
      r.set_element (13, 1);
      r.set_element (14, 0.5f);
      return r;
    }

    Mat4 operator* (const Mat4& o) const {
#ifdef __APPLE__
      return Mat4 (simd_mul (m_value, o.m_value));
#else
      Mat4 r;
      for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
          float sum = 0;
          for (int k = 0; k < 4; ++k)
            sum += at (k, row) * o.at (c, k);
          r.set (c, row, sum);
        }
      return r;
#endif
    }

    Vec3 transform_point (const Vec3& v) const {
#ifdef __APPLE__
      const simd_float4 transformed =
        simd_mul (m_value, simd_float4 { v[0], v[1], v[2], 1.0f });
      return Vec3 (transformed.xyz);
#else
      return Vec3 (element (0) * v[0] + element (4) * v[1] +
                     element (8) * v[2] + element (12),
                   element (1) * v[0] + element (5) * v[1] +
                     element (9) * v[2] + element (13),
                   element (2) * v[0] + element (6) * v[1] +
                     element (10) * v[2] + element (14));
#endif
    }

    Vec3 transform_vector (const Vec3& v) const {
#ifdef __APPLE__
      const simd_float4 transformed =
        simd_mul (m_value, simd_float4 { v[0], v[1], v[2], 0.0f });
      return Vec3 (transformed.xyz);
#else
      return Vec3 (element (0) * v[0] + element (4) * v[1] + element (8) * v[2],
                   element (1) * v[0] + element (5) * v[1] + element (9) * v[2],
                   element (2) * v[0] + element (6) * v[1] +
                     element (10) * v[2]);
#endif
    }

  private:
#ifdef __APPLE__
    explicit Mat4 (simd_float4x4 value) : m_value (value) {}

    simd_float4x4 m_value;
#else
    float m_value[16];
#endif
  };

  static_assert (sizeof (Mat4) == 16 * sizeof (float));
  static_assert (std::is_trivially_copyable_v<Mat4>);
  static_assert (std::is_standard_layout_v<Mat4>);
#ifdef __APPLE__
  static_assert (alignof (Mat4) == alignof (simd_float4x4));
#endif

  // Inverse-transpose of a Mat4's upper 3x3: the correct transform
  // for normals under non-uniform scaling (what fixed-function GL
  // did, with GL_NORMALIZE folded in by the caller normalizing).
  struct NormalMat {
    Vec3 c0, c1, c2;

    static NormalMat from (const Mat4& m) {
      const float a = m.element (0), b = m.element (4), c = m.element (8);
      const float d = m.element (1), e = m.element (5), f = m.element (9);
      const float g = m.element (2), h = m.element (6), i = m.element (10);

      const float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
      float det = a * A + b * B + c * C;
      if (std::fabs (det) < 1e-20f)
        det = 1.0f;
      const float k = 1.0f / det;

      // inverse(M)^T = cof(M)/det: c0..c2 are the COLUMNS of the
      // cofactor matrix (adj(M) = cof(M)^T would give plain
      // inverse(M) and counter-rotate normals).
      NormalMat n;
      n.c0 = Vec3 (A, c * h - b * i, b * f - c * e) * k;
      n.c1 = Vec3 (B, a * i - c * g, c * d - a * f) * k;
      n.c2 = Vec3 (C, b * g - a * h, a * e - b * d) * k;
      return n;
    }

    Vec3 apply (const Vec3& v) const {
      return normalized (Vec3 (c0[0] * v[0] + c1[0] * v[1] + c2[0] * v[2],
                               c0[1] * v[0] + c1[1] * v[1] + c2[1] * v[2],
                               c0[2] * v[0] + c1[2] * v[1] + c2[2] * v[2]));
    }
  };
}

#endif
