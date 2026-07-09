// Terrain fragment shader: per-pixel lighting, slope-aware
// grass/rock/snow/sand bands, PCF shadows, aerial-perspective fog.
varying vec3 normal;
varying vec3 wnormal;
varying vec3 ecPos;
varying float height;
varying vec4 shadowCoord;

uniform sampler2D grass, dirt, snow;
uniform sampler2DShadow shadowMap;
uniform vec3 fogColor;
uniform float shadowStrength; // 0 disables shadow lookups
uniform float seaLevel;       // sea level / heightScale

float calculateShadow(vec3 n, vec3 L) {
  if (shadowStrength < 0.01)
    return 1.0;

  vec3 shadowProj = shadowCoord.xyz / shadowCoord.w;
  if (shadowProj.x < 0.0 || shadowProj.x > 1.0 ||
      shadowProj.y < 0.0 || shadowProj.y > 1.0 ||
      shadowProj.z < 0.0 || shadowProj.z > 1.0)
    return 1.0;

  // Slope-scaled bias against acne on raking ground
  float bias = 0.0006 + 0.0025 * (1.0 - max(dot(n, L), 0.0));
  shadowProj.z -= bias;

  // Center + 4 diagonal taps; LINEAR depth compare gives free
  // 2x2 PCF inside each tap
  const float texel = 1.0 / 4096.0;
  float shadow = 0.4 * shadow2D(shadowMap, shadowProj).r;
  for (float y = -1.5; y <= 1.5; y += 3.0)
    for (float x = -1.5; x <= 1.5; x += 3.0)
      shadow += 0.15 * shadow2D(shadowMap,
                                shadowProj
                                + vec3(x * texel, y * texel,
                                       0.0)).r;
  shadow = pow(shadow, 1.3);

  // Fade shadows out into the haze
  float fade = clamp(2.5 * (1.0 - gl_FogFragCoord), 0.0, 1.0);
  return mix(1.0, shadow, shadowStrength * fade);
}

void main () {
  // Aerial perspective: haze brightens and warms toward the sun
  vec3 V = normalize(ecPos); // eye -> fragment
  vec3 L = normalize(gl_LightSource[0].position.xyz);
  float sunAmt = pow(max(dot(V, L), 0.0), 8.0);
  vec3 fogC = mix(fogColor, fogColor * vec3(1.25, 1.12, 0.9),
                  sunAmt);

  // Fully fogged: skip all texture and shadow work
  float fog_factor = smoothstep(0.0, 0.9, gl_FogFragCoord);
  if (fog_factor >= 0.995) {
    gl_FragColor = vec4(fogC, 1.0);
    return;
  }

  vec3 n = normalize(normal);
  vec3 wn = normalize(wnormal);

  // Texture bands: by altitude AND slope -- rock breaks through on
  // steep faces, snow only settles on flatter ground, sand stays
  // off the cliffs
  float rock_coef = max(smoothstep(0.35, 0.55, height),
                        1.0 - smoothstep(0.68, 0.84, wn.y));
  float snow_coef = smoothstep(0.55, 0.68, height)
                    * smoothstep(0.58, 0.78, wn.y);
  float beach_coef = (1.0 - smoothstep(seaLevel + 0.004,
                                       seaLevel + 0.030, height))
                     * smoothstep(0.55, 0.75, wn.y);

  vec2 tc = gl_TexCoord[0].st;
  vec4 texel = texture2D(grass, tc);
  vec4 rocktexel = texture2D(dirt, tc);
  vec4 snowtexel = texture2D(snow, tc);

  // Break up the tiling: modulate each layer with itself at a much
  // larger scale (mutually uncorrelated offsets)
  vec3 coarse = texture2D(grass, tc * 0.083 + vec2(0.37, 0.19)).rgb;
  texel.rgb *= (0.55 + 1.1 * coarse);
  rocktexel.rgb *= (0.7 + 0.6 * texture2D(dirt, tc * 0.061).r);
  snowtexel.rgb *= (0.75 + 0.5 * texture2D(snow,
                                           tc * 0.053
                                           + vec2(0.21, 0.43)).r);

  // Altitude tint: sun-dried gold near the coast, lusher higher up
  texel.rgb *= mix(vec3(1.06, 1.00, 0.82), vec3(0.92, 1.02, 0.90),
                   smoothstep(0.08, 0.30, height));

  texel = mix(texel, rocktexel, rock_coef);
  texel = mix(texel, snowtexel, snow_coef);
  vec4 sandtexel = rocktexel * vec4(1.45, 1.30, 0.95, 1.0);
  texel = mix(texel, sandtexel, beach_coef);

  // Per-pixel Lambert with real cast shadows
  float shadowFactor = calculateShadow(n, L);
  float intensity = max(dot(L, n), 0.0);
  vec3 lit = intensity * shadowFactor
                 * (gl_FrontMaterial.diffuse
                    * gl_LightSource[0].diffuse).rgb
             + gl_LightModel.ambient.rgb;

  // Snowfields sparkle in the sun
  vec3 h = normalize(L - V);
  lit += snow_coef * shadowFactor
         * pow(max(dot(n, h), 0.0), 32.0) * 0.4;

  vec3 color = texel.rgb * lit;
  gl_FragColor = vec4(mix(color, fogC, fog_factor), 1.0);
}
