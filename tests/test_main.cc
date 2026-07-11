#include <tests/test.hh>

#include <exception>
#include <iostream>

int main () {
  int failures = 0;
  for (const moppe::test::Case& test : moppe::test::registry ()) {
    try {
      test.run ();
      std::cout << "[pass] " << test.name << "\n";
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[fail] " << test.name << "\n  " << error.what () << "\n";
    } catch (...) {
      ++failures;
      std::cerr << "[fail] " << test.name << "\n  unknown exception\n";
    }
  }

  std::cout << moppe::test::registry ().size () << " tests, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
