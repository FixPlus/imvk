#version 450
#extension GL_GOOGLE_include_directive : require
#include "MaterialLightingInterface.h.glsl"

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inUVW;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inWorldNormal;
layout(location = 4) in vec3 inViewPos;
layout(location = 5) in vec4 inWorldTangent;

layout(set = 3, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 3, binding = 1) uniform sampler2D metallicRoughnessTexture;
layout(set = 3, binding = 2) uniform sampler2D normalTexture;
layout(set = 3, binding = 3) uniform sampler2D occlusionTexture;
layout(set = 3, binding = 4) uniform sampler2D emissiveTexture;
layout(set = 3, binding = 5) uniform MaterialData {
  vec4 baseColorFactor;
  vec4 emissiveFactor;
  vec4 pbrFactors;
  vec4 alpha;
} material;

SurfaceInfo Material() {
  SurfaceInfo ret;
  vec4 baseColor = texture(baseColorTexture, inUVW.xy);
  ret.albedo = inColor * material.baseColorFactor * baseColor;
  if (material.alpha.x > 0.5 && ret.albedo.a < material.alpha.y)
    discard;
  if (material.alpha.x < 0.5 && material.alpha.z < 0.5)
    ret.albedo.a = 1.0;

  vec3 normal = normalize(inWorldNormal);
  vec3 tangent = normalize(inWorldTangent.xyz -
                           normal * dot(normal, inWorldTangent.xyz));
  vec3 bitangent = cross(normal, tangent) * inWorldTangent.w;
  vec3 sampledNormal = texture(normalTexture, inUVW.xy).xyz * 2.0 - 1.0;
  sampledNormal.xy *= material.pbrFactors.z;
  ret.normal = normalize(mat3(tangent, bitangent, normal) * sampledNormal);

  vec4 metallicRoughness = texture(metallicRoughnessTexture, inUVW.xy);
  ret.metallic = material.pbrFactors.x * metallicRoughness.b;
  ret.roughness = material.pbrFactors.y * metallicRoughness.g;
  ret.emissive = length(material.emissiveFactor.rgb *
                        texture(emissiveTexture, inUVW.xy).rgb);
  ret.albedo.rgb *= mix(1.0, texture(occlusionTexture, inUVW.xy).r,
                        material.pbrFactors.w);
  ret.cameraOffset = inViewPos;
  ret.position = inWorldPos;
  return ret;
}
