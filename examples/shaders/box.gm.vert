#version 450
#extension GL_GOOGLE_include_directive : require
#include "GeomProjInterface.h.glsl"

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(set = 1, binding = 0) uniform Data { vec4 values; }
data;
WorldVertexInfo Geometry() {

  WorldVertexInfo ret;
  ret.UVW = vec3(inUV, 0.0f);
  vec2[3] values = {(data.values.xz - vec2(0.5)) / 3,
                    (data.values.yx - vec2(0.5)) / 2,
                    (data.values.zy - vec2(0.5)) / 3};
  ret.color = vec4(inColor * data.values.xyz, 1.0f);
  ret.position = vec3(inPos + values[gl_VertexIndex], 0.0);
  ret.normal = vec3(1.0, 0.0, 0.0);
  return ret;
}