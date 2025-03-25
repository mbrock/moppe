
#include <moppe/map/generate.hh>

#include <iostream>
#include <fstream>
#include <stdexcept>

int 
main (int argc, char **argv)
{
  using namespace moppe::map;

  int seed = (argc == 2) ? atoi (argv[1]) : 0;

  try
    {
      RandomHeightMap map (513, 513, moppe::Vector3D (1, 1, 1), seed);
      std::cerr << "Randomizing...";
      map.randomize_plasmally (0.7);
      std::cerr << "done.\n";
      
      std::cerr << "Applying bowl edge effect...";
      map.apply_bowl_edge(1.0, 0.01);
      std::cerr << "done.\n";

      std::cerr << "Writing to test.tga...";
      std::ofstream f ("test.tga", std::ofstream::binary);
      write_tga (f, map);
      std::cerr << "done.\n";
    }
  catch (const std::exception& error)
    {
      std::cerr << "Error: " << error.what () << "\n";
      return -1;
    }
}
