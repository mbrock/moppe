varying vec4 diffuse, ambient;
varying vec3 normal, lightDir;
varying float height, intensity, grass_coef, snow_coef;
void main () {
  float fog;
  float density = 0.75;
  vec4 position = ftransform();
  height = gl_Vertex.y/6.0;
  fog = length(position);
  fog = exp2(-density*density*fog*fog*1.442695);
  fog = clamp(fog,0.0,1.0);
  normal = normalize(gl_NormalMatrix * gl_Normal);
  lightDir = normalize(vec3(gl_LightSource[0].position));
  diffuse = gl_FrontMaterial.diffuse * gl_LightSource[0].diffuse;
  ambient = gl_LightModel.ambient;
  intensity = max(dot(lightDir, normalize(normal)),0.0);
  grass_coef = smoothstep(0.2,0.6,height);
  snow_coef = smoothstep(0.7,0.75,height);
  gl_TexCoord[0] = gl_MultiTexCoord0;
  gl_FogFragCoord = 1.0 - fog;
  gl_Position = position;
}
