#version 450

layout(location = 0) out vec3 outColor;

const vec3 colors[3] = {vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0),
                        vec3(0.0, 0.0, 1.0)};

void main() {
  gl_Position = vec4(-0.5f + (gl_VertexIndex / 2.0),
                     0.5f - (gl_VertexIndex % 2), 0.0f, 1.0f);

  outColor = colors[gl_VertexIndex];
}
