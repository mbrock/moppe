uniform sampler2D scene;
uniform float time;

varying vec2 uv;

void main () {
  // Wavy refraction wobble
  vec2 w = uv;
  w.x += 0.007 * sin (uv.y * 32.0 + time * 2.3);
  w.y += 0.007 * sin (uv.x * 27.0 - time * 1.9);
  w = clamp (w, 0.0, 1.0);

  vec3 c = texture2D (scene, w).rgb;

  // Blue-green underwater grade, murkier toward the bottom
  c = mix (c, c * vec3 (0.30, 0.62, 0.85), 0.5);
  c *= 0.75 + 0.25 * uv.y;

  // Faint drifting light shimmer
  c *= 0.95 + 0.05 * sin (time * 1.7 + uv.x * 9.0 + uv.y * 4.0);

  gl_FragColor = vec4 (c, 1.0);
}
