varying vec4 diffuse, ambient;
varying vec3 normal, lightDir;
varying float height, intensity, rock_coef, snow_coef;

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
  float density = 0.75;
  vec4 position = ftransform();
  height = gl_Vertex.y/900.0;
  fog = length(position) * 0.001;
  fog = 1.0 - clamp (0.0, 1.0, fog);
  normal = normalize(gl_NormalMatrix * gl_Normal);
  lightDir = normalize(vec3(gl_LightSource[0].position));
  diffuse = gl_FrontMaterial.diffuse * gl_LightSource[0].diffuse;
  ambient = gl_LightModel.ambient;
  intensity = max(dot(lightDir, normalize(normal)),0.0);
  rock_coef = smoothstep(0.4,0.6,height);
  snow_coef = smoothstep(0.78,0.79,height);
  gl_TexCoord[0] = gl_MultiTexCoord0;
  gl_FogFragCoord = 1.0 - fog;
  gl_Position = position;
}
