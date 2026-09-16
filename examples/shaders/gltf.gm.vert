#version 450
#extension GL_GOOGLE_include_directive : require
#include "GeomProjInterface.h.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
layout(location = 4) in vec4 inColor;

WorldVertexInfo Geometry() {
  WorldVertexInfo ret;
  ret.position = inPosition;
  ret.normal = inNormal;
  ret.tangent = inTangent;
  ret.UVW = vec3(inUV, 0.0);
  ret.color = inColor;
  return ret;
}
