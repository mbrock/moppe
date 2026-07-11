#ifndef MOPPE_TEST_HH
#define MOPPE_TEST_HH

#include <cmath>
#include <functional>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace moppe::test {
  struct Case {
    std::string name;
    std::function<void ()> run;
  };

  inline std::vector<Case>& registry () {
    static std::vector<Case> cases;
    return cases;
  }

  struct Registrar {
    Registrar (std::string name, std::function<void ()> run) {
      registry ().push_back ({ std::move (name), std::move (run) });
    }
  };

  inline void
  check (bool condition,
         const char* expression,
         std::source_location location = std::source_location::current ()) {
    if (condition)
      return;
    std::ostringstream message;
    message << location.file_name () << ":" << location.line ()
            << ": check failed: " << expression;
    throw std::runtime_error (message.str ());
  }

  inline void check_near (
    float actual,
    float expected,
    float tolerance,
    const char* actual_expression,
    const char* expected_expression,
    std::source_location location = std::source_location::current ()) {
    if (std::fabs (actual - expected) <= tolerance)
      return;
    std::ostringstream message;
    message << location.file_name () << ":" << location.line () << ": "
            << actual_expression << " was " << actual << ", expected "
            << expected_expression << " = " << expected << " within "
            << tolerance;
    throw std::runtime_error (message.str ());
  }
}

#define MOPPE_TEST(name)                                                       \
  static void name ();                                                         \
  static ::moppe::test::Registrar name##_registrar (#name, &name);             \
  static void name ()

#define MOPPE_CHECK(expression) ::moppe::test::check ((expression), #expression)

#define MOPPE_CHECK_NEAR(actual, expected, tolerance)                          \
  ::moppe::test::check_near (                                                  \
    (actual), (expected), (tolerance), #actual, #expected)

#endif
