uniform float time;
uniform vec3 fogColor;

varying vec3 eyePos;
varying vec3 eyeNormal;
varying vec3 worldPos;
varying float fogCoord;

void main () {
  vec3 n = normalize (eyeNormal);

  // Small-scale ripples sparkling on top of the big swells, faded
  // with distance so the horizon doesn't shimmer with aliasing
  float rippleFade = exp (-length (eyePos) * 0.002);
  n.x += rippleFade * (0.10 * sin (worldPos.x * 0.31 + time * 2.3)
       + 0.05 * sin (worldPos.z * 0.83 - time * 3.1));
  n.z += rippleFade * (0.10 * sin (worldPos.z * 0.27 - time * 2.0)
       + 0.05 * sin (worldPos.x * 0.71 + time * 2.7));
  n = normalize (n);

  vec3 v = normalize (-eyePos);

  // Schlick fresnel with a proper F0 floor
  float fresnel = 0.02
      + 0.98 * pow (1.0 - max (dot (n, v), 0.0), 5.0);

  // Sun glint (light position is already in eye space), dimmed by
  // haze so it doesn't ghost through the fog
  vec3 sun = normalize (gl_LightSource[0].position.xyz);
  vec3 h = normalize (sun + v);
  float spec = pow (max (dot (n, h), 0.0), 120.0) * 0.9
               * (1.0 - fogCoord);

  vec3 deep    = vec3 (0.04, 0.16, 0.26);
  vec3 shallow = vec3 (0.12, 0.38, 0.50);

  vec3 water = mix (deep, shallow, 0.25 + 0.5 * fresnel);

  // Aerial perspective: same sun-warmed haze as the terrain
  float sunAmt = pow (max (dot (normalize (eyePos), sun), 0.0), 8.0);
  vec3 fogC = mix (fogColor, fogColor * vec3 (1.25, 1.12, 0.9),
                   sunAmt);

  vec3 color = mix (water, fogC, 0.55 * fresnel) + vec3 (spec);

  float alpha = mix (0.78, 0.95, fresnel);

  // Identical fog curve to the terrain so shorelines match
  float ff = smoothstep (0.0, 0.9, fogCoord);
  gl_FragColor = vec4 (mix (color, fogC, ff),
                       alpha * (1.0 - 0.4 * ff));
}
