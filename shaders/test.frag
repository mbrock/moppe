varying vec4 diffuse, ambient;
varying vec3 normal, lightDir;
varying float height, intensity;
varying float snow_coef, grass_coef;
uniform sampler2D grass, dirt, snow;
void main () {
  vec3 ct, cf;; vec4 texel, grasstexel, snowtexel;; float at, af, fog, density;; vec2 tc;; cf = intensity * diffuse.rgb + ambient.rgb;; af = diffuse.a;; tc = gl_TexCoord[0].st;;
  texel = texture2D (dirt, tc);; grasstexel = texture2D (grass, tc);; snowtexel = texture2D (snow, tc);;
  texel = grass_coef * grasstexel + (1.0 - grass_coef) * texel;;
  texel = snow_coef * snowtexel + (1.0 - snow_coef) * texel;;
  ct = texel.rgb;; at = texel.a;;
  gl_FragColor = vec4 (mix (vec3 (ct * cf), vec3 (0.8, 0.8, 0.9),
			    gl_FogFragCoord), at * af);;
}
