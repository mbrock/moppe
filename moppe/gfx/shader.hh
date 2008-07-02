
#ifndef MOPPE_SHADER_HH
#define MOPPE_SHADER_HH

#include <moppe/app/gl.hh>

#include <string>

namespace moppe {
namespace gl {

#ifdef MAC
  typedef void *shader_id_t;
#else
  // GLEW!  Why do you ruin my types!
  typedef int   shader_id_t;
#endif

  void print_shader_log (const std::string& title, shader_id_t id);

  class Shader {
  public:
    Shader (int mode, const std::string& filename);

    void load ();

    shader_id_t id () const { return m_id; }

    void print_log () const
    { print_shader_log (m_filename, m_id); }

  private:
    int m_mode;
    std::string m_filename;
    shader_id_t         m_id;
    std::string m_source;
  };

  Shader load_vertex_shader (const std::string& filename);
  Shader load_fragment_shader (const std::string& filename);

  class ShaderProgram {
  public:
    ShaderProgram ();

    void load   ();
    void attach (const Shader& shader);
    void link   () const;
    void use    () const;
    void unuse  () const;

    void set_int (const char *name, int x)
    {
      glUniform1iARB (glGetUniformLocationARB (m_id, name), x);
    }

    void print_log () const
    { print_shader_log ("program", m_id); }

    shader_id_t id () const { return m_id; }

  private:
    shader_id_t m_id;
  };
}
}

#endif
