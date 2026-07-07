varying vec4 diffuse, ambient;
varying vec3 normal, lightDir;
varying float height, intensity, rock_coef, snow_coef, beach_coef;
varying vec4 shadowCoord; // For shadow mapping
uniform mat4 lightMatrix; // Light's view-projection matrix
uniform float heightScale; // world vertical scale in meters
uniform float seaLevel;    // sea level / heightScale
uniform float fogScale;    // distance haze density (per meter)

float smoothstepVar (float edge1, float edge2, float curve, float value) {
  float width = edge2 - edge1;
  float phase = (value - edge1) / width;
  phase = clamp (phase, 0.0, 1.0);
  curve = (curve + 0.025) * 99.075;
  float outValue = pow (phase, curve);
  return outValue;
}  

void main () {
  float fog;
  float density = 0.65; // Reduced for more subtle fog
  vec4 position = ftransform();
  // Normalize against the world's vertical scale so the rock and
  // snow bands below span the terrain's actual range
  height = gl_Vertex.y/heightScale;

  // Gentle distance haze: barely there up close, softening the
  // far mountains into the horizon without hiding them
  float distance = length(position.xyz);
  fog = 1.0 - exp(-pow(distance * fogScale, 1.5));

  // Valley mist: extra haze pools over low ground near sea level
  // and thins out with altitude, so peaks rise clear out of it
  float lowness = 1.0 - smoothstep(45.0, 170.0, gl_Vertex.y);
  fog += 0.3 * lowness * smoothstep(150.0, 1500.0, distance);

  fog = clamp(fog, 0.0, 1.0);
  
  // Add edge detection for ridge lines and silhouettes
  // This will be used in the fragment shader to soften edges
  float edge_factor = abs(dot(normalize(position.xyz), normalize(normal)));
  edge_factor = 1.0 - pow(edge_factor, 4.0); // Higher value = more sensitive edge detection
  
  // Pass edge factor to fragment shader
  gl_FrontSecondaryColor = vec4(edge_factor, 0.0, 0.0, 1.0);
  normal = normalize(gl_NormalMatrix * gl_Normal);
  lightDir = normalize(vec3(gl_LightSource[0].position));
  diffuse = gl_FrontMaterial.diffuse * gl_LightSource[0].diffuse;
  ambient = gl_LightModel.ambient;
  intensity = max(dot(lightDir, normalize(normal)),0.0);
  rock_coef = smoothstep(0.35,0.55,height);
  snow_coef = smoothstep(0.55,0.68,height);
  // Sandy shore in a band just above the waterline
  beach_coef = 1.0 - smoothstep(seaLevel + 0.004, seaLevel + 0.030,
                                height);

  // Transform vertex to light space for shadow lookup
  // Bias matrix transforms from [-1,1] to [0,1] for texture coordinates
  shadowCoord = lightMatrix * vec4(gl_Vertex.xyz, 1.0);
  
  gl_TexCoord[0] = gl_MultiTexCoord0;
  gl_FogFragCoord = fog;
  gl_Position = position;
}
