varying vec4 diffuse, ambient;
varying vec3 normal, lightDir, halfVector;
varying float height, intensity;
uniform sampler2D grass, dirt, snow;
void main () {vec3 ct, cf, c; vec4 texel; float at, af, a, fog, density; vec2 tc;
  cf = intensity * diffuse.rgb + ambient.rgb;
  af = diffuse.a;
  tc = gl_TexCoord[0].st;
  texel = texture2D (dirt, tc);
  float grass_coef = smoothstep (0.2, 0.6, height);
  float snow_coef = smoothstep (0.7, 0.75, height);
  texel = grass_coef * texture2D (grass, tc)
    + (1.0 - grass_coef) * texture2D (dirt, tc);
  texel = snow_coef * texture2D (snow, tc)
    + (1.0 - snow_coef) * texel;
  ct = texel.rgb;
  at = texel.a;
  c = ct * cf;
  a = at * af;
  gl_FragColor = vec4 (mix (vec3 (c),
			    vec3 (0.9, 0.9, 1.0),
			    gl_FogFragCoord), a);
}
