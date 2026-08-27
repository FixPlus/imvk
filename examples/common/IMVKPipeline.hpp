#pragma once
#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Pipeline.hpp"

#include "IMVKShaderLoader.hpp"

namespace imvk::examples {

class GeometryStage : public imvk::GraphicsPipelineStage {
public:
  GeometryStage(GraphicsEngine &engine, ShaderLoader &shaderFactory,
                std::string_view shaderName,
                std::unique_ptr<vkw::VertexInputStateCreateInfoBase>
                    vertexState = nullptr);

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;

private:
  std::unique_ptr<vkw::VertexInputStateCreateInfoBase> m_vertexState;
};

class ProjectionStage : public imvk::GraphicsPipelineStage {
public:
  ProjectionStage(GraphicsEngine &engine, ShaderLoader &shaderFactory,
                  std::string_view shaderName);

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override {}
};

class MaterialStage : public imvk::GraphicsPipelineStage {
public:
  MaterialStage(
      GraphicsEngine &engine, ShaderLoader &shaderFactory,
      std::string_view shaderName,
      vkw::RasterizationStateCreateInfo rasterization,
      std::optional<vkw::DepthTestStateCreateInfo> depthTest = std::nullopt);

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;

private:
  vkw::RasterizationStateCreateInfo m_rasterizationState;
  std::optional<vkw::DepthTestStateCreateInfo> m_depthTestState;
};

class LightingStage : public imvk::GraphicsPipelineStage {
public:
  LightingStage(GraphicsEngine &engine, ShaderLoader &shaderFactory,
                std::string_view shaderName,
                std::span<const VkPipelineColorBlendAttachmentState> = {});

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;

private:
  boost::container::small_vector<VkPipelineColorBlendAttachmentState, 2>
      m_blends;
};

} // namespace imvk::examples