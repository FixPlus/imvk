#version 450
#extension GL_GOOGLE_include_directive : require
#include "GeomProjInterface.h.glsl"

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outUVW;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec3 outWorldNormal;
layout(location = 4) out vec3 outViewPos;
layout(location = 5) out vec4 outWorldTangent;

layout(set = 2, binding = 0) uniform CameraData {
  mat4 viewProjection;
  vec4 position;
} camera;

void Projection(WorldVertexInfo worldVertexInfo) {
  outColor = worldVertexInfo.color;
  outUVW = worldVertexInfo.UVW;
  outWorldPos = worldVertexInfo.position;
  outWorldNormal = worldVertexInfo.normal;
  outViewPos = camera.position.xyz - outWorldPos;
  outWorldTangent = worldVertexInfo.tangent;
  gl_Position = camera.viewProjection * vec4(outWorldPos, 1.0);
}