#include <math.h>

// Clang lowers __builtin_roundeven to this missing UCRT symbol when the
// target lacks a rounding instruction. CORE-MATH requires ties to even,
// independently of the current floating-point rounding mode.
double roundeven (double x) {
  double rounded = round (x);
  if (isfinite (x) && fabs (rounded - x) == 0.5 &&
      fmod (fabs (rounded), 2.0) == 1.0)
    return copysign (fabs (rounded) - 1.0, x);
  return rounded;
}
