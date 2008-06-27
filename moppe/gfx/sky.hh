
#ifndef MOPPE_SKY_HH
#define MOPPE_SKY_HH

#include <moppe/gfx/texture.hh>

namespace moppe {
namespace gfx {
  class Sky {
  public:
    Sky (const std::string& filename);

    void load ();
    void render () const;
    
  private:
    gl::Texture m_texture;
  };
}
}

#endif
