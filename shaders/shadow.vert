// Shadow mapping vertex shader
varying vec4 shadowCoord;
uniform mat4 lightMatrix;

void main() {
    // Calculate vertex position
    vec4 position = ftransform();
    
    // Transform vertex to light space for shadow lookup
    shadowCoord = lightMatrix * gl_ModelViewMatrix * gl_Vertex;
    
    // Pass through normal and texcoord
    gl_TexCoord[0] = gl_MultiTexCoord0;
    gl_FrontColor = gl_Color;
    
    // Output position
    gl_Position = position;
}