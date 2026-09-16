#pragma once
#include "imvk/graph/Nodes.hpp"
#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Pipeline.hpp"

#include "IMVKShaderLoader.hpp"

namespace imvk::examples {

class GeometryStage : public imvk::GraphicsPipelineStage {
public:
  GeometryStage(FramedEngine &engine, ShaderLoader &shaderFactory,
                std::string_view shaderName,
                std::unique_ptr<vkw::VertexInputStateCreateInfoBase>
                    vertexState = nullptr);

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;

private:
  std::unique_ptr<vkw::VertexInputStateCreateInfoBase> m_vertexState;
};

class ProjectionStage : public imvk::GraphicsPipelineStage {
public:
  ProjectionStage(FramedEngine &engine, ShaderLoader &shaderFactory,
                  std::string_view shaderName);

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override {}
};

class MaterialStage : public imvk::GraphicsPipelineStage {
public:
  MaterialStage(
      FramedEngine &engine, ShaderLoader &shaderFactory,
      std::string_view shaderName,
      vkw::RasterizationStateCreateInfo rasterization,
      std::optional<vkw::DepthTestStateCreateInfo> depthTest = std::nullopt,
      std::optional<VkPipelineColorBlendAttachmentState> blend = std::nullopt);

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;

private:
  vkw::RasterizationStateCreateInfo m_rasterizationState;
  std::optional<vkw::DepthTestStateCreateInfo> m_depthTestState;
  std::optional<VkPipelineColorBlendAttachmentState> m_blendState;
};

class LightingStage : public imvk::GraphicsPipelineStage {
public:
  LightingStage(FramedEngine &engine, ShaderLoader &shaderFactory,
                std::string_view shaderName,
                const vkw::RenderingFormatInfo &renderingInfo,
                std::span<const VkPipelineColorBlendAttachmentState> = {});
  bool isProvoking() const override { return true; }

  vkw::GraphicsPipelineCreateInfo
  initCreateInfo(const vkw::PipelineLayout &layout) const override;

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;

private:
  vkw::RenderingFormatInfo m_renderingInfo;
  boost::container::small_vector<VkPipelineColorBlendAttachmentState, 2>
      m_blends;
};

class ComputeStage;

struct ComputePipelineTraits {
  using HandleTy = vkw::ComputePipeline;
  using StageTy = ComputeStage;
  static vkw::ComputePipeline
  create(FramedEngine &engine,
         imvk::PipelineLayout<ComputePipelineTraits> &layout);
};

class ComputeStage : public imvk::Stage {
public:
  using PipelineTraits = ComputePipelineTraits;
  ComputeStage(FramedEngine &engine, ShaderLoader &shaderFactory,
               std::string_view shaderName);
};

using ComputePipeline = imvk::Pipeline<ComputePipelineTraits>;
using ComputePipelineLayout = imvk::PipelineLayout<ComputePipelineTraits>;

using PipelinePool = imvk::GraphicsPipelinePool<
    imvk::examples::GeometryStage, imvk::examples::ProjectionStage,
    imvk::examples::MaterialStage, imvk::examples::LightingStage>;
using PipelineManager = imvk::GraphicsPipelineManager<
    imvk::examples::GeometryStage, imvk::examples::ProjectionStage,
    imvk::examples::MaterialStage, imvk::examples::LightingStage>;

} // namespace imvk::examples