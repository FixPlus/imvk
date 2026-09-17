#version 450
#extension GL_GOOGLE_include_directive : require
#include "MaterialLightingInterface.h.glsl"

layout(location = 0) out vec4 outFragColor;

layout(set = 4, binding = 0) uniform DirectionalLightData {
  vec4 direction;
  vec4 colorIntensity;
  vec4 ambientColorIntensity;
} light;

const float PI = 3.14159265359;

float distributionGGX(vec3 normal, vec3 halfway, float roughness) {
  float alpha = roughness * roughness;
  float alphaSquared = alpha * alpha;
  float normalDotHalfway = max(dot(normal, halfway), 0.0);
  float denominator = normalDotHalfway * normalDotHalfway *
                          (alphaSquared - 1.0) +
                      1.0;
  return alphaSquared / max(PI * denominator * denominator, 0.0001);
}

float geometrySchlickGGX(float normalDotDirection, float roughness) {
  float remappedRoughness = roughness + 1.0;
  float k = remappedRoughness * remappedRoughness / 8.0;
  return normalDotDirection /
         max(normalDotDirection * (1.0 - k) + k, 0.0001);
}

float geometrySmith(vec3 normal, vec3 view, vec3 lightDirection,
                    float roughness) {
  return geometrySchlickGGX(max(dot(normal, view), 0.0), roughness) *
         geometrySchlickGGX(max(dot(normal, lightDirection), 0.0), roughness);
}

vec3 fresnelSchlick(float cosine, vec3 reflectance) {
  return reflectance +
         (1.0 - reflectance) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

void Lighting(SurfaceInfo surface) {
  vec3 albedo = max(surface.albedo.rgb, vec3(0.0));
  float metallic = clamp(surface.metallic, 0.0, 1.0);
  float roughness = clamp(surface.roughness, 0.045, 1.0);
  vec3 view = normalize(surface.cameraOffset);
  vec3 normal = normalize(surface.normal);
  if (dot(normal, view) < 0.0)
    normal = -normal;

  // The uniform stores the direction in which the light rays travel.
  vec3 toLight = normalize(-light.direction.xyz);
  vec3 halfwaySum = view + toLight;
  vec3 halfway = dot(halfwaySum, halfwaySum) > 0.0001
                     ? normalize(halfwaySum)
                     : normal;
  float normalDotLight = max(dot(normal, toLight), 0.0);
  float normalDotView = max(dot(normal, view), 0.0);

  vec3 dielectricReflectance = vec3(0.04);
  vec3 baseReflectance = mix(dielectricReflectance, albedo, metallic);
  vec3 fresnel = fresnelSchlick(max(dot(halfway, view), 0.0),
                                baseReflectance);
  float distribution = distributionGGX(normal, halfway, roughness);
  float geometry = geometrySmith(normal, view, toLight, roughness);
  vec3 specular = distribution * geometry * fresnel /
                  max(4.0 * normalDotView * normalDotLight, 0.0001);

  vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
  vec3 radiance = max(light.colorIntensity.rgb, vec3(0.0)) *
                  max(light.colorIntensity.a, 0.0);
  vec3 direct =
      (diffuseWeight * albedo / PI + specular) * radiance * normalDotLight;

  // Approximate diffuse irradiance and environment reflection without an
  // environment map. This keeps indirectly lit surfaces visible while still
  // respecting the material's metallic response and view-dependent Fresnel.
  vec3 ambientFresnel = fresnelSchlick(normalDotView, baseReflectance);
  vec3 ambientDiffuse =
      (vec3(1.0) - ambientFresnel) * (1.0 - metallic) * albedo;
  vec3 ambientSpecular = ambientFresnel * mix(1.0, 0.5, roughness);
  vec3 ambientRadiance = max(light.ambientColorIntensity.rgb, vec3(0.0)) *
                         max(light.ambientColorIntensity.a, 0.0);
  vec3 ambient = (ambientDiffuse + ambientSpecular) * ambientRadiance;
  vec3 emissive = vec3(max(surface.emissive, 0.0));

  // Reinhard maps potentially HDR lighting into the swapchain's display range.
  vec3 color = direct + ambient + emissive;
  color = color / (color + vec3(1.0));
  outFragColor = vec4(color, surface.albedo.a);
}