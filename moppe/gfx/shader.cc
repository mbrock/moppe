
#include <moppe/gfx/shader.hh>
#include <moppe/app/util.hh>
#include <moppe/app/gl.hh>

namespace moppe {
namespace gl {
  Shader::Shader (int mode, const std::string& filename)
    : m_mode (mode), m_filename (filename)
  {
    m_source = util::slurp_file (filename);
  }

  void
  Shader::load ()
  {
    m_id = glCreateShaderObjectARB (m_mode);
    const char *s = m_source.c_str ();
    glShaderSourceARB (m_id, 1, &s, 0);

    std::cerr << "compiling " << m_filename << std::endl;
    glCompileShaderARB (m_id);
  }

  Shader
  load_fragment_shader (const std::string& filename)
  { return Shader (GL_FRAGMENT_SHADER_ARB, filename); }

  Shader
  load_vertex_shader (const std::string& filename)
  { return Shader (GL_VERTEX_SHADER_ARB, filename); }

  ShaderProgram::ShaderProgram ()
  { }

  void
  ShaderProgram::load ()
  {
    m_id = glCreateProgramObjectARB ();
  }

  void
  ShaderProgram::attach (const Shader& shader)
  {
    glAttachObjectARB (m_id, shader.id ());
  }

  void
  ShaderProgram::link () const
  {
    glLinkProgramARB (m_id);
  }

  void
  ShaderProgram::use () const
  {
    glUseProgramObjectARB (m_id);
  }

  void
  ShaderProgram::unuse () const
  {
    glUseProgramObjectARB (0);
  }

  void
  print_shader_log (const std::string& name, shader_id_t id)
  {
    GLint length = 0;
    GLsizei written = 0;

    glGetObjectParameterivARB (id, GL_OBJECT_INFO_LOG_LENGTH_ARB,
			       &length);

    if (length > 0)
      {
	char *log = new char [length];
	glGetInfoLogARB (id, length, &written, log);
	std::cerr << name << " shader log:\n" << log << "\n";
	delete [] log;
      }
  }
}
}
