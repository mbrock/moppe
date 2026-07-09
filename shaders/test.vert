// Terrain vertex shader: passes geometry to the fragment shader,
// which does per-pixel lighting, texturing bands, and shadows.
varying vec3 normal;   // eye-space normal
varying vec3 wnormal;  // world-space normal (for slope bands)
varying vec3 ecPos;    // eye-space position
varying float height;  // altitude / heightScale
varying vec4 shadowCoord;

uniform mat4 lightMatrix;  // bias * lightProj * lightView
uniform float heightScale; // world vertical scale in meters
uniform float fogScale;    // distance haze density (per meter)
uniform float texScale;    // texture repeat per world meter

void main () {
  height = gl_Vertex.y / heightScale;

  ecPos = (gl_ModelViewMatrix * gl_Vertex).xyz;
  float dist = length(ecPos);

  // Gentle distance haze plus valley mist that pools on low ground
  float fog = 1.0 - exp(-pow(dist * fogScale, 1.5));
  float lowness = 1.0 - smoothstep(45.0, 170.0, gl_Vertex.y);
  fog += 0.3 * lowness * smoothstep(150.0, 1500.0, dist);
  gl_FogFragCoord = clamp(fog, 0.0, 1.0);

  normal = gl_NormalMatrix * gl_Normal;
  wnormal = gl_Normal;

  shadowCoord = lightMatrix * vec4(gl_Vertex.xyz, 1.0);

  // Texture coordinates derive from position: no texcoord stream
  gl_TexCoord[0] = vec4(gl_Vertex.x * texScale,
                        gl_Vertex.z * texScale, 0.0, 0.0);

  gl_Position = ftransform();
}
