#ifndef MOPPE_CORRECT_MATH_HH
#define MOPPE_CORRECT_MATH_HH

// Correctly rounded transcendental functions (CORE-MATH,
// third_party/core-math). Each returns the exact result rounded once, which
// is unique, so every platform computes the same bits; the C libraries'
// std::sin, std::pow, and std::hypot differ in the last place between Apple,
// glibc, and Microsoft. World generation uses these so a seed makes the same
// world everywhere (docs/determinism.md). The exact operations -- sqrt,
// floor, ceil, round, fma, fmod -- are the same everywhere already.

extern "C" {
float cr_sinf (float);
float cr_cosf (float);
float cr_tanf (float);
float cr_atan2f (float, float);
float cr_expf (float);
float cr_logf (float);
float cr_log2f (float);
float cr_powf (float, float);
float cr_hypotf (float, float);
double cr_sin (double);
double cr_cos (double);
double cr_tan (double);
double cr_atan2 (double, double);
double cr_exp (double);
double cr_log (double);
double cr_log2 (double);
double cr_pow (double, double);
double cr_hypot (double, double);
}

namespace moppe::cr {
  inline float sin (float x) {
    return cr_sinf (x);
  }
  inline float cos (float x) {
    return cr_cosf (x);
  }
  inline float tan (float x) {
    return cr_tanf (x);
  }
  inline float atan2 (float y, float x) {
    return cr_atan2f (y, x);
  }
  inline float exp (float x) {
    return cr_expf (x);
  }
  inline float log (float x) {
    return cr_logf (x);
  }
  inline float log2 (float x) {
    return cr_log2f (x);
  }
  inline float pow (float x, float y) {
    return cr_powf (x, y);
  }
  inline float hypot (float x, float y) {
    return cr_hypotf (x, y);
  }

  inline double sin (double x) {
    return cr_sin (x);
  }
  inline double cos (double x) {
    return cr_cos (x);
  }
  inline double tan (double x) {
    return cr_tan (x);
  }
  inline double atan2 (double y, double x) {
    return cr_atan2 (y, x);
  }
  inline double exp (double x) {
    return cr_exp (x);
  }
  inline double log (double x) {
    return cr_log (x);
  }
  inline double log2 (double x) {
    return cr_log2 (x);
  }
  inline double pow (double x, double y) {
    return cr_pow (x, y);
  }
  inline double hypot (double x, double y) {
    return cr_hypot (x, y);
  }
}

#endif
