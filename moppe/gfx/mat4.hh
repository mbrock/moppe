#ifndef MOPPE_MAT4_HH
#define MOPPE_MAT4_HH

#include <moppe/gfx/math.hh>

#include <cmath>

namespace moppe {
  // Column-major 4x4 matrix (GL-style layout: translation in m[12..14]).
  // The same byte layout matches MSL float4x4, so instances can be
  // memcpy'd straight into uniform buffers.
  //
  // Conventions: right-handed eye space looking down -Z; clip-space
  // depth is Metal's [0,1].  A * B applies B first, like the GL
  // matrix stack.
  struct Mat4 {
    float m[16];

    Mat4 () {
      for (int i = 0; i < 16; ++i)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    float& at (int col, int row) {
      return m[col * 4 + row];
    }
    float at (int col, int row) const {
      return m[col * 4 + row];
    }

    static Mat4 identity () {
      return Mat4 ();
    }

    static Mat4 translation (const Vector3D& v) {
      Mat4 r;
      r.m[12] = v.x;
      r.m[13] = v.y;
      r.m[14] = v.z;
      return r;
    }

    static Mat4 scaling (const Vector3D& v) {
      Mat4 r;
      r.m[0] = v.x;
      r.m[5] = v.y;
      r.m[10] = v.z;
      return r;
    }

    // Rotation about an arbitrary (not necessarily unit) axis.
    static Mat4 rotation (radians_t angle, const Vector3D& axis) {
      const Vector3D a = axis.normalized ();
      const float c = std::cos (angle), s = std::sin (angle);
      const float t = 1.0f - c;

      Mat4 r;
      r.m[0] = t * a.x * a.x + c;
      r.m[1] = t * a.x * a.y + s * a.z;
      r.m[2] = t * a.x * a.z - s * a.y;
      r.m[4] = t * a.x * a.y - s * a.z;
      r.m[5] = t * a.y * a.y + c;
      r.m[6] = t * a.y * a.z + s * a.x;
      r.m[8] = t * a.x * a.z + s * a.y;
      r.m[9] = t * a.y * a.z - s * a.x;
      r.m[10] = t * a.z * a.z + c;
      return r;
    }

    // Columns of the upper-left 3x3 (a basis frame) plus an origin.
    static Mat4 basis (const Vector3D& x,
                       const Vector3D& y,
                       const Vector3D& z,
                       const Vector3D& origin = Vector3D ()) {
      Mat4 r;
      r.m[0] = x.x;
      r.m[1] = x.y;
      r.m[2] = x.z;
      r.m[4] = y.x;
      r.m[5] = y.y;
      r.m[6] = y.z;
      r.m[8] = z.x;
      r.m[9] = z.y;
      r.m[10] = z.z;
      r.m[12] = origin.x;
      r.m[13] = origin.y;
      r.m[14] = origin.z;
      return r;
    }

    // View matrix a la gluLookAt.
    static Mat4
    look_at (const Vector3D& eye, const Vector3D& center, const Vector3D& up) {
      const Vector3D f = (center - eye).normalized ();
      const Vector3D s = f.cross (up).normalized ();
      const Vector3D u = s.cross (f);

      Mat4 r;
      r.m[0] = s.x;
      r.m[4] = s.y;
      r.m[8] = s.z;
      r.m[1] = u.x;
      r.m[5] = u.y;
      r.m[9] = u.z;
      r.m[2] = -f.x;
      r.m[6] = -f.y;
      r.m[10] = -f.z;
      r.m[12] = -s.dot (eye);
      r.m[13] = -u.dot (eye);
      r.m[14] = f.dot (eye);
      return r;
    }

    // Perspective projection with reversed Z: near maps to 1, far to
    // 0.  Pair with a GREATER_EQUAL depth test and a clear value of 0.
    static Mat4
    perspective_reversed (radians_t fovy, float aspect, float near, float far) {
      const float f = 1.0f / std::tan (fovy / 2);
      Mat4 r;
      r.m[0] = f / aspect;
      r.m[5] = f;
      r.m[10] = near / (far - near);
      r.m[11] = -1.0f;
      r.m[14] = near * far / (far - near);
      r.m[15] = 0.0f;
      return r;
    }

    // Orthographic projection, standard [0,1] depth (near -> 0).
    static Mat4
    ortho (float l, float r_, float b, float t, float near, float far) {
      Mat4 r;
      r.m[0] = 2 / (r_ - l);
      r.m[5] = 2 / (t - b);
      r.m[10] = -1 / (far - near);
      r.m[12] = -(r_ + l) / (r_ - l);
      r.m[13] = -(t + b) / (t - b);
      r.m[14] = -near / (far - near);
      return r;
    }

    // Pixel-coordinate 2D projection for the HUD: x in [0,w] left to
    // right, y in [0,h] TOP to bottom, z pinned mid-range.
    static Mat4 hud_ortho (float w, float h) {
      Mat4 r;
      r.m[0] = 2 / w;
      r.m[5] = -2 / h;
      r.m[10] = 0;
      r.m[12] = -1;
      r.m[13] = 1;
      r.m[14] = 0.5f;
      return r;
    }

    Mat4 operator* (const Mat4& o) const {
      Mat4 r;
      for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
          float sum = 0;
          for (int k = 0; k < 4; ++k)
            sum += at (k, row) * o.at (c, k);
          r.at (c, row) = sum;
        }
      return r;
    }

    Vector3D transform_point (const Vector3D& v) const {
      return Vector3D (m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
                       m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
                       m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]);
    }

    Vector3D transform_vector (const Vector3D& v) const {
      return Vector3D (m[0] * v.x + m[4] * v.y + m[8] * v.z,
                       m[1] * v.x + m[5] * v.y + m[9] * v.z,
                       m[2] * v.x + m[6] * v.y + m[10] * v.z);
    }
  };

  // Inverse-transpose of a Mat4's upper 3x3: the correct transform
  // for normals under non-uniform scaling (what fixed-function GL
  // did, with GL_NORMALIZE folded in by the caller normalizing).
  struct NormalMat {
    Vector3D c0, c1, c2;

    static NormalMat from (const Mat4& m) {
      const float a = m.m[0], b = m.m[4], c = m.m[8];
      const float d = m.m[1], e = m.m[5], f = m.m[9];
      const float g = m.m[2], h = m.m[6], i = m.m[10];

      const float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
      float det = a * A + b * B + c * C;
      if (std::fabs (det) < 1e-20f)
        det = 1.0f;
      const float k = 1.0f / det;

      // inverse(M)^T = cof(M)/det: c0..c2 are the COLUMNS of the
      // cofactor matrix (adj(M) = cof(M)^T would give plain
      // inverse(M) and counter-rotate normals).
      NormalMat n;
      n.c0 = Vector3D (A, c * h - b * i, b * f - c * e) * k;
      n.c1 = Vector3D (B, a * i - c * g, c * d - a * f) * k;
      n.c2 = Vector3D (C, b * g - a * h, a * e - b * d) * k;
      return n;
    }

    Vector3D apply (const Vector3D& v) const {
      return Vector3D (c0.x * v.x + c1.x * v.y + c2.x * v.z,
                       c0.y * v.x + c1.y * v.y + c2.y * v.z,
                       c0.z * v.x + c1.z * v.y + c2.z * v.z)
        .normalized ();
    }
  };
}

#endif
