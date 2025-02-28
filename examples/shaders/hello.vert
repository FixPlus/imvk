#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 outColor;

layout(set = 0, binding = 0) uniform Data { vec4 values; }
data;

void main() {
  gl_Position = vec4(inPos, 0.0f, 1.0f);

  outColor = inColor * data.values.xyz;
}
