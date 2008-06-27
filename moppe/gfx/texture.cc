
#include <moppe/gfx/texture.hh>

namespace moppe {
namespace gl {
  Texture::Texture (const std::string& tga)
    : m_filename (tga)
  { }

  void
  Texture::load ()
  {
    m_tga.Load (const_cast<char*> (m_filename.c_str ()));
    glGenTextures (1, &m_id);
    glBindTexture (GL_TEXTURE_2D, m_id);
    gluBuild2DMipmaps (GL_TEXTURE_2D, m_tga.GetBPP () / 8,
		       m_tga.GetWidth (), m_tga.GetHeight (),
		       GL_RGB, GL_UNSIGNED_BYTE,
		       m_tga.GetImg ());
  }

  void
  Texture::bind (int unit) const
  {
    glActiveTextureARB (GL_TEXTURE0_ARB + unit);
    glEnable (GL_TEXTURE_2D);
    glBindTexture (GL_TEXTURE_2D, m_id);
    glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		     GL_LINEAR_MIPMAP_NEAREST);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
}
}
