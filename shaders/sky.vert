// Enhanced sky vertex shader
varying vec3 vPosition;
varying vec3 vWorldPos;
varying vec2 vUV;

void main()
{
    // Pass vertex position for skybox sampling
    vPosition = gl_Vertex.xyz;
    vWorldPos = gl_Vertex.xyz;
    vUV = gl_MultiTexCoord0.st;
    
    // Use regular transform for sky
    gl_Position = ftransform();
    
    // Force depth to maximum for skybox
    gl_Position.z = gl_Position.w - 0.00001;
}