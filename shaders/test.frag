varying vec4 diffuse, ambient;
varying vec3 normal, lightDir;
varying float height, intensity;
varying float snow_coef, rock_coef;
varying vec4 shadowCoord; // For shadow mapping
uniform sampler2D grass, dirt, snow;
uniform sampler2DShadow shadowMap; // Shadow map texture
uniform vec3 fogColor; // Dynamic fog color that matches sky
uniform float shadowStrength; // Control shadow darkness (0.0-1.0)
uniform vec3 sunDirection; // Current sun direction for normal-based shadows

// Helper function for depth-based blur
vec4 blurTexture(sampler2D tex, vec2 uv, float blur_amount) {
  // Simple 5-tap blur based on distance
  vec4 sum = vec4(0.0);
  float blur = blur_amount * 0.01;
  
  // Center sample (highest weight)
  sum += texture2D(tex, uv) * 0.5;
  
  // Four corner samples
  sum += texture2D(tex, uv + vec2(blur, blur)) * 0.125;
  sum += texture2D(tex, uv + vec2(-blur, blur)) * 0.125;
  sum += texture2D(tex, uv + vec2(blur, -blur)) * 0.125;
  sum += texture2D(tex, uv + vec2(-blur, -blur)) * 0.125;
  
  return sum;
}

// PCF shadow mapping - samples multiple points for soft shadows
float calculateShadow() {
  // Check if using normal-based shadows or shadow mapping
  // If shadowStrength is close to zero, we're using normal-based shadows
  bool useNormalBasedShadows = (shadowStrength < 0.01);
  if (useNormalBasedShadows) {
    // Normal-based shadowing only
    // Use the normal direction to approximate shadows
    float dotLight = dot(normalize(normal), normalize(sunDirection));
    float simpleShadow = smoothstep(-0.1, 0.3, dotLight);
    
    // Terrain self-shadowing approximation - steep areas cast shadows
    float steepness = 1.0 - abs(normal.y);
    float terrainShadow = smoothstep(0.7, 0.9, steepness);
    
    // Combine shadowing factors
    float shadow = min(simpleShadow, 1.0 - terrainShadow);
    
    // Distance-based shadow fading (softer in the distance)
    float shadowFade = 1.0 - gl_FogFragCoord;
    shadowFade = max(0.0, shadowFade * 2.0); // Fade shadows in the distance
    
    // Final shadow factor with fake shadow strength
    return 0.0; // mix(1.0, shadow, 0.7 * shadowFade);
  }

  // Normalize shadow coordinates
  vec3 shadowProj = shadowCoord.xyz / shadowCoord.w;
  
  // If outside the light's view frustum, no shadow
  if (shadowProj.x < 0.0 || shadowProj.x > 1.0 ||
      shadowProj.y < 0.0 || shadowProj.y > 1.0 ||
      shadowProj.z < 0.0 || shadowProj.z > 1.0) {
    return 1.0;
  }
  
  // Shadow mapping implementation
  
  // Depth bias to reduce shadow acne
  float bias = 0.0005;
  shadowProj.z -= bias;
  
  // Basic shadow test - returns 0.0 (shadowed) or 1.0 (lit)
  float shadow = shadow2D(shadowMap, shadowProj).r;
  
  // PCF filtering - sample neighboring texels for sharper shadows with soft edges
  float texelSize = 1.0 / 4096.0; // Shadow map size (increased from 2048)
  
  // Sharper PCF filter for more dramatic shadows
  // Using weighted samples for better quality
  float result = 0.0;
  float total_weight = 0.0;
  float weight;
  
  // Center sample gets highest weight
  weight = 0.5;
  result += shadow2D(shadowMap, shadowProj).r * weight;
  total_weight += weight;
  
  // Near samples (higher weight)
  for (float y = -0.5; y <= 0.5; y += 1.0) {
    for (float x = -0.5; x <= 0.5; x += 1.0) {
      if (x == 0.0 && y == 0.0) continue; // Skip center (already sampled)
      
      weight = 0.15;
      vec3 offset = vec3(x * texelSize, y * texelSize, 0.0);
      result += shadow2D(shadowMap, shadowProj + offset).r * weight;
      total_weight += weight;
    }
  }
  
  // Far samples (lower weight)
  for (float y = -1.5; y <= 1.5; y += 1.0) {
    for (float x = -1.5; x <= 1.5; x += 1.0) {
      if (abs(x) <= 0.5 && abs(y) <= 0.5) continue; // Skip near samples (already processed)
      
      weight = 0.02;
      vec3 offset = vec3(x * texelSize, y * texelSize, 0.0);
      result += shadow2D(shadowMap, shadowProj + offset).r * weight;
      total_weight += weight;
    }
  }
  
  // Normalize by total weight
  shadow = result / total_weight;
  
  // Make shadows darker and more dramatic by applying a power function
  shadow = pow(shadow, 2.5); // Darker shadows (higher exponent = darker shadows)
  
  // Distance-based shadow fading with longer shadows
  float shadowFade = 1.0 - gl_FogFragCoord;
  shadowFade = max(0.0, shadowFade * 2.5); // Extended shadow distance
  
  // Final shadow factor with stronger contrast (1.0 = fully lit, 0.0 = fully shadowed)
  return mix(1.0, shadow, min(1.0, shadowStrength * shadowFade * 1.5));
}

void main () {
  vec3 ct, cf;
  vec4 texel, rocktexel, snowtexel;
  float at, af, fog, density;
  vec2 tc;
  
  // Calculate shadow factor
  float shadowFactor = calculateShadow();
  
  // Apply shadow to diffuse component (keep ambient)
  vec3 lit_color = intensity * diffuse.rgb * shadowFactor + ambient.rgb;
  cf = lit_color;
  af = diffuse.a;

  tc = gl_TexCoord[0].st;
  
  // Calculate blur amount based on distance (fog factor)
  float blur_amount = gl_FogFragCoord * 3.0;
  
  // Apply distance-based blur to textures
  texel = blurTexture(grass, tc, blur_amount);
  rocktexel = blurTexture(dirt, tc, blur_amount);
  snowtexel = blurTexture(snow, tc, blur_amount);
  
  // Blend textures based on height
  texel = rock_coef * rocktexel + (1.0 - rock_coef) * texel;
  texel = snow_coef * snowtexel + (1.0 - snow_coef) * texel;

  ct = texel.rgb; at = texel.a;
  
  // Enhanced fog blending with smoother transition and alpha fade
  float fog_factor = smoothstep(0.0, 0.9, gl_FogFragCoord);
  
  // Calculate alpha based on distance for sky blending only at the horizon edges
  // The further away, the more transparent only at silhouette edges
  float edge_alpha = 1.0;
  
  // Get edge detection factor from vertex shader
  float edge_factor = gl_SecondaryColor.r;
  
  // Only apply transparency at the very edges where terrain meets sky
  // This creates soft anti-aliased silhouettes without making mountains transparent
  if (edge_factor > 0.7 && gl_FogFragCoord > 0.6) {
    edge_alpha = mix(1.0, 0.3, (edge_factor - 0.7) * 3.3 * min(1.0, gl_FogFragCoord));
  }
  
  gl_FragColor = vec4(mix(vec3(ct * cf), 
                         fogColor,
                         fog_factor), 
                     at * af * edge_alpha);
}