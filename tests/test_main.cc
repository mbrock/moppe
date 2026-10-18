#include <tests/test.hh>

#include <exception>
#include <iostream>
#include <string_view>

// moppe-tests [WORD...] runs every test, or only those whose names hold
// one of the words.
int main (int argc, char** argv) {
  int failures = 0, run = 0;
  for (const moppe::test::Case& test : moppe::test::registry ()) {
    bool wanted = argc < 2;
    for (int i = 1; i < argc && !wanted; ++i)
      wanted = std::string_view (test.name).find (argv[i]) !=
               std::string_view::npos;
    if (!wanted)
      continue;
    ++run;
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

  std::cout << run << " tests, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
