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

    is.seekg (0, std::ios::end);
    length = is.tellg ();
    is.seekg (0, std::ios::beg);

    char* buffer = new char [length];
    is.read (&buffer[0], length);
    is.close ();

    std::string s (buffer);
    delete [] buffer;
    return s;
  }
}
}
