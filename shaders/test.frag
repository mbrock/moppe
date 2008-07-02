varying vec4 diffuse, ambient;
varying vec3 normal, lightDir;
varying float height, intensity;
varying float snow_coef, rock_coef;
uniform sampler2D grass, dirt, snow;

void main () {
  vec3 ct, cf;
  vec4 texel, rocktexel, snowtexel;
  float at, af, fog, density;
  vec2 tc;

  cf = intensity * diffuse.rgb + ambient.rgb;
  af = diffuse.a;

  tc = gl_TexCoord[0].st;
  texel = texture2D (grass, tc);
  rocktexel = texture2D (dirt, tc);
  snowtexel = texture2D (snow, tc);
  texel = rock_coef * rocktexel + (1.0 - rock_coef) * texel;
  texel = snow_coef * snowtexel + (1.0 - snow_coef) * texel;

  ct = texel.rgb; at = texel.a;
  gl_FragColor = vec4 (mix (vec3 (ct * cf), 
			    vec3 (0.5, 0.5, 0.5),
			    gl_FogFragCoord), 
		       at * af * 0.3);
}
