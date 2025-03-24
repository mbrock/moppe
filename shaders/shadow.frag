// Shadow mapping fragment shader
// This is only used for the shadow pass - it's just a depth writer
void main() {
    // This shader doesn't need to output anything.
    // The depth values are automatically written to the depth buffer
    // We just need an empty shader to satisfy OpenGL.
    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}