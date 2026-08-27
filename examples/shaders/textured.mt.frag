#version 450
#extension GL_GOOGLE_include_directive : require
#include "MaterialLightingInterface.h.glsl"

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inUVW;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inWorldNormal;
layout(location = 4) in vec3 inViewPos;

layout(set = 3, binding = 0) uniform sampler2D myTex;

SurfaceInfo Material() {
  SurfaceInfo ret;
  ret.albedo = inColor * vec4(texture(myTex, inUVW.rg));
  ret.cameraOffset = inViewPos;
  ret.normal = inWorldNormal;
  return ret;
}