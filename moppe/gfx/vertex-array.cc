
#include <moppe/app/gl.hh>
#include <moppe/gfx/vertex-array.hh>

namespace moppe {
namespace gfx {
  void render_vertex_arrays (int mode,
			     int from,
			     int count,
			     const VertexArray& vertices,
			     const VertexArray& normals,
			     const VertexArray& texcoords) {
    glEnableClientState (GL_VERTEX_ARRAY);
    glEnableClientState (GL_NORMAL_ARRAY);
    glEnableClientState (GL_TEXTURE_COORD_ARRAY);

    glVertexPointer (3, GL_FLOAT, 0, vertices.address (from));
    glNormalPointer (GL_FLOAT, 0, normals.address (from));
    glTexCoordPointer (3, GL_FLOAT, 0, texcoords.address (from));

    glDrawArrays (mode, 0, count);

    glDisableClientState (GL_VERTEX_ARRAY);
    glDisableClientState (GL_NORMAL_ARRAY);
    glDisableClientState (GL_TEXTURE_COORD_ARRAY);
  }
}
}
