// The website's geology natively (docs/site.md): times the steps and writes
// each step's picture as OUTPUT-STEP.ppm, to judge a lattice or a palette
// without a browser:
//
//   moppe-site-geology [LATTICE [SEED [OUTPUT]]]

#include <moppe/site/geology.hh>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main (int argc, char** argv) {
  const int lattice = argc > 1 ? std::stoi (argv[1]) : 256;
  const auto seed =
    static_cast<std::uint32_t> (argc > 2 ? std::stoul (argv[2]) : 123);
  const std::string output = argc > 3 ? argv[3] : "";
  constexpr int size = 512;
  auto last = std::chrono::steady_clock::now ();
  std::vector<std::uint8_t> rgba;
  moppe::site::grow_land (
    lattice,
    seed,
    [&] (int step, int steps, double years, std::span<const float> heights) {
      const auto now = std::chrono::steady_clock::now ();
      std::cout
        << "step " << step << "/" << steps << " at " << years << " years: "
        << std::chrono::duration<double, std::milli> (now - last).count ()
        << " ms\n";
      last = now;
      if (output.empty ())
        return;
      moppe::site::paint_relief (heights, lattice, size, rgba);
      std::ofstream file (output + "-" + std::to_string (step) + ".ppm",
                          std::ios::binary);
      file << "P6\n" << size << ' ' << size << "\n255\n";
      for (std::size_t i = 0; i < rgba.size (); i += 4)
        file.write (reinterpret_cast<const char*> (&rgba[i]), 3);
    });
  return 0;
}
