varying vec4 diffuse, ambient;
varying vec3 normal, lightDir, halfVector;
varying float height;
uniform sampler2D grass, dirt, snow;

void
main ()
{
  vec3 ct, cf, c;
  vec4 texel;
  float intensity, at, af, a, fog, density;
  intensity = max (dot (lightDir, normalize (normal)), 0.0);
  cf = intensity * diffuse.rgb +
         ambient.rgb;
  af = diffuse.a;
  texel = texture2D (dirt, gl_TexCoord[0].st);
  float grass_coef = smoothstep (0.2, 0.6, height);
  float snow_coef = smoothstep (0.7, 0.75, height);
  texel = grass_coef * vec4 (texture2D (grass, gl_TexCoord[0].st))
    + (1.0 - grass_coef) * vec4 (texture2D (dirt, gl_TexCoord[0].st));
  texel = snow_coef * vec4 (texture2D (snow, gl_TexCoord[0].st))
    + (1.0 - snow_coef) * texel;
  ct = texel.rgb;
  at = texel.a;
  c = ct * cf;
  a = at * af;
  gl_FragColor = vec4 (mix (vec3 (c),
			    vec3 (0.9, 0.9, 1.0),
			    gl_FogFragCoord), a);
}
