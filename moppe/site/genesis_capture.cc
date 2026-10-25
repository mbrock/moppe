// The genesis view natively (docs/site.md): grows the website's land and
// renders the view at scroll progresses through Vulkan without a window,
// writing each as OUTPUT-PROGRESS.ppm, to judge the looks and the
// choreography without a browser. The land's age follows the progress,
// as the page's does when it has finished growing.
//
//   moppe-site-genesis-capture OUTPUT [WIDTHxHEIGHT [PROGRESS...]]

#include <moppe/nhal/vulkan/vulkan_device.hh>
#include <moppe/site/genesis.hh>
#include <moppe/site/geology.hh>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace moppe;

namespace {
  constexpr int lattice = 256;

  std::vector<std::uint32_t> read_spirv (const std::string& path) {
    std::ifstream file (path, std::ios::binary | std::ios::ate);
    if (!file)
      throw std::runtime_error ("cannot read " + path);
    const std::streamsize size = file.tellg ();
    std::vector<std::uint32_t> words (std::size_t (size) / 4);
    file.seekg (0);
    file.read (reinterpret_cast<char*> (words.data ()), size);
    return words;
  }

  void write_ppm (const nhal::Capture& capture, const std::string& path) {
    std::ofstream file (path, std::ios::binary);
    file << "P6\n" << capture.width << ' ' << capture.height << "\n255\n";
    const bool bgra = capture.format == nhal::Format::bgra8_unorm;
    for (std::uint32_t y = 0; y < capture.height; ++y) {
      const auto* row = reinterpret_cast<const unsigned char*> (
        capture.pixels.data () + std::size_t (y) * capture.row_bytes);
      for (std::uint32_t x = 0; x < capture.width; ++x) {
        const unsigned char* p = row + x * 4;
        const char rgb[3] = { char (bgra ? p[2] : p[0]),
                              char (p[1]),
                              char (bgra ? p[0] : p[2]) };
        file.write (rgb, 3);
      }
    }
  }
}

int main (int argc, char** argv) {
  try {
    if (argc < 2)
      throw std::invalid_argument (
        "usage: moppe-site-genesis-capture OUTPUT [WIDTHxHEIGHT "
        "[PROGRESS...]]");
    const std::string output = argv[1];
    std::uint32_t width = 390 * 2, height = 844 * 2;
    if (argc > 2 && std::sscanf (argv[2], "%ux%u", &width, &height) != 2)
      throw std::invalid_argument ("size must be WIDTHxHEIGHT");
    std::vector<float> progresses;
    for (int i = 3; i < argc; ++i)
      progresses.push_back (std::stof (argv[i]));
    if (progresses.empty ())
      progresses = { 0.0f, 0.3f, 0.45f, 0.7f, 1.0f };

    std::vector<std::vector<float>> steps;
    site::grow_land (
      lattice, 123, [&] (int, int, double, std::span<const float> heights) {
        steps.emplace_back (heights.begin (), heights.end ());
      });
    std::cerr << "genesis: grew " << steps.size () << " steps" << std::endl;

    auto device =
      nhal::create_vulkan_device ({}, width, height, nhal::Format::bgra8_unorm);
    std::cerr << "genesis: " << device->info ().backend << " on "
              << device->info ().adapter << std::endl;
    const std::string shaders = MOPPE_SITE_SHADERS;
    std::vector<std::uint32_t> code[4] = {
      read_spirv (shaders + "/genesis_terrain.vertex.spv"),
      read_spirv (shaders + "/genesis_terrain.fragment.spv"),
      read_spirv (shaders + "/genesis_sky.vertex.spv"),
      read_spirv (shaders + "/genesis_sky.fragment.spv"),
    };
    auto stage = [&] (int i) { return nhal::StageCode { {}, {}, code[i] }; };
    site::Genesis genesis (*device,
                           { stage (0), stage (1), stage (2), stage (3) },
                           lattice,
                           site::land_period_m,
                           site::land_sea_level_m);

    for (const float progress : progresses) {
      // The age runs with the progress over its first half, as if the
      // reader had scrolled while the land grew.
      const float age =
        std::clamp (progress / 0.6f, 0.0f, 1.0f) * float (steps.size () - 1);
      const std::size_t step = std::min (std::size_t (age), steps.size () - 2);
      genesis.set_heights (steps[step], steps[step + 1]);
      const float aspect = float (width) / float (height);
      if (!genesis.render (site::genesis_choreography (progress, 30.0, aspect),
                           age - float (step)))
        throw std::runtime_error ("no drawable");
      char name[32];
      std::snprintf (name, sizeof name, "-%.2f.ppm", progress);
      const std::string path = output + name;
      device->capture_frame ([path] (const nhal::Capture& capture) {
        write_ppm (capture, path);
        std::cerr << "genesis: wrote " << path << std::endl;
      });
      device->end_frame ();
      device->wait_idle ();
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "genesis: " << error.what () << std::endl;
    return -1;
  }
}
