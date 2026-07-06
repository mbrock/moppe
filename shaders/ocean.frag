uniform float time;
uniform vec3 fogColor;

varying vec3 eyePos;
varying vec3 eyeNormal;
varying vec3 worldPos;
varying float fogCoord;

void main () {
  vec3 n = normalize (eyeNormal);

  // Small-scale ripples sparkling on top of the big swells
  n.x += 0.10 * sin (worldPos.x * 0.31 + time * 2.3)
       + 0.05 * sin (worldPos.z * 0.83 - time * 3.1);
  n.z += 0.10 * sin (worldPos.z * 0.27 - time * 2.0)
       + 0.05 * sin (worldPos.x * 0.71 + time * 2.7);
  n = normalize (n);

  vec3 v = normalize (-eyePos);
  float fresnel = pow (1.0 - max (dot (n, v), 0.0), 3.0);

  // Sun glint (light position is already in eye space)
  vec3 sun = normalize (gl_LightSource[0].position.xyz);
  vec3 h = normalize (sun + v);
  float spec = pow (max (dot (n, h), 0.0), 120.0) * 0.9;

  vec3 deep    = vec3 (0.04, 0.16, 0.26);
  vec3 shallow = vec3 (0.12, 0.38, 0.50);

  vec3 water = mix (deep, shallow, 0.25 + 0.5 * fresnel);
  vec3 color = mix (water, fogColor, 0.55 * fresnel) + vec3 (spec);

  float alpha = mix (0.78, 0.95, fresnel);

  gl_FragColor = vec4 (mix (color, fogColor, fogCoord),
                       alpha * (1.0 - 0.4 * fogCoord));
}
