#include <moppe/app/util.hh>

#include <fstream>

namespace moppe {
namespace util {
  std::string
  slurp_file (const std::string& filename)
  {
    std::ifstream is;
    int length;
    
    is.open (filename.c_str (), std::ios::binary);
    
    if (!is.is_open()) {
      std::cerr << "Error: Could not open file " << filename << std::endl;
      return "";
    }

    is.seekg (0, std::ios::end);
    length = is.tellg ();
    is.seekg (0, std::ios::beg);

    char* buffer = new char [length + 1];
    is.read (buffer, length);
    is.close ();

    buffer[length] = 0;

    std::string s (buffer);
    delete [] buffer;
    return s;
  }
}
}
