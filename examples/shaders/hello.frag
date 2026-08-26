#version 450
layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec4 outFragColor;

layout(set = 2, binding = 0) uniform sampler2D myTex;
layout(set = 0, binding = 0) uniform sampler2D myTex2;

void main() {
  outFragColor = vec4(
      inColor * vec3(texture(myTex, inUV)) * vec3(texture(myTex2, inUV)), 1.0);
}
