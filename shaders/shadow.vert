// Shadow depth pass: rasterize the terrain from the light's view.
// The terrain's object space is world space, so the light's
// projection*view matrix is the whole transform.
uniform mat4 lightMatrix;

void main() {
    gl_Position = lightMatrix * vec4(gl_Vertex.xyz, 1.0);
}
