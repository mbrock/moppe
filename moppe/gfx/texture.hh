#ifndef MOPPE_TEXTURE_HH
#define MOPPE_TEXTURE_HH

#include <moppe/app/gl.hh>
#include <moppe/gfx/tga.hh>

namespace moppe {
namespace gl {
  class Texture {
  public:
    Texture (const std::string& tga);

    void load ();
    void bind (int unit);
    
  private:
    std::string m_filename;
    GLuint m_id;
    tga::TGAImg m_tga;
  };
}
}

#endif
