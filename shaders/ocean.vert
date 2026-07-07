uniform float time;
uniform float fogScale;

varying vec3 eyePos;
varying vec3 eyeNormal;
varying vec3 worldPos;
varying float fogCoord;

void main () {
  vec3 p = gl_Vertex.xyz;

  // Three overlapping swells with different directions and speeds
  float a1 = p.x * 0.020 + time * 1.1;
  float a2 = p.z * 0.023 - time * 0.9;
  float a3 = (p.x + p.z) * 0.011 + time * 0.6;

  p.y += 1.2 * sin (a1) + 1.0 * sin (a2) + 1.8 * sin (a3);

  // Analytic surface normal from the wave derivatives
  float dx = 1.2 * 0.020 * cos (a1) + 1.8 * 0.011 * cos (a3);
  float dz = 1.0 * 0.023 * cos (a2) + 1.8 * 0.011 * cos (a3);
  vec3 n = normalize (vec3 (-dx, 1.0, -dz));

  worldPos = p;
  eyeNormal = gl_NormalMatrix * n;

  vec4 eye = gl_ModelViewMatrix * vec4 (p, 1.0);
  eyePos = eye.xyz;
  gl_Position = gl_ProjectionMatrix * eye;

  // Same haze curve as the terrain shader, plus the sea mist that
  // pools over the water (matching the terrain's valley mist)
  float dist = length (eye.xyz);
  fogCoord = 1.0 - exp (-pow (dist * fogScale, 1.5));
  fogCoord += 0.3 * smoothstep (150.0, 1500.0, dist);
  fogCoord = clamp (fogCoord, 0.0, 1.0);
}
