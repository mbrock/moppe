
#ifndef MOPPE_SHADER_HH
#define MOPPE_SHADER_HH

#include <string>

namespace moppe {
namespace gl {
  void print_shader_log (const std::string& title, unsigned int id);

  class Shader {
  public:
    Shader (int mode, const std::string& filename);

    void load ();

    unsigned int id () const { return m_id; }

    void print_log () const
    { print_shader_log (m_filename, m_id); }

  private:
    int m_mode;
    std::string m_filename;
    unsigned int         m_id;
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

    void print_log () const
    { print_shader_log ("program", m_id); }

    unsigned int id () const { return m_id; }

  private:
    unsigned int m_id;
  };
}
}

#endif
