
varying vec4 diffuse, ambient;
varying vec3 normal, lightDir, halfVector;
varying float height, intensity;

void
main ()
{
  float fog;
  height = gl_Vertex.y / 6.0;

  vec4 position = ftransform ();
  gl_Position = position;

  const float density = 1.0;
  fog = length (position);
  fog = exp2 (-density * density *
	      fog * fog * 1.442695);
  fog = clamp (fog, 0.0, 1.0);

  gl_FogFragCoord = 1.0 - fog;

  normal = normalize (gl_NormalMatrix * gl_Normal);
  lightDir = normalize (vec3 (gl_LightSource[0].position));
  halfVector = normalize (gl_LightSource[0].halfVector.xyz);

  diffuse = gl_FrontMaterial.diffuse * gl_LightSource[0].diffuse;
  ambient = gl_LightModel.ambient;

  intensity = max (dot (lightDir, normalize (normal)), 0.0);

  gl_TexCoord[0] = gl_MultiTexCoord0;
}
