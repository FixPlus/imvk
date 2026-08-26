#pragma once
#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Pipeline.hpp"
#include "imvk/graphics/Swapchain.hpp"

#include "vkw/Framebuffer.hpp"
#include "vkw/Image.hpp"
#include "vkw/RenderPass.hpp"

#include "IMVKShaderLoader.hpp"

namespace imvk::examples {

class BasicVertexStage : public imvk::GraphicsPipelineStage {
public:
  BasicVertexStage(GraphicsEngine &engine, ShaderLoader &shaderFactory,
                   std::string_view shaderName,
                   std::unique_ptr<vkw::VertexInputStateCreateInfoBase>
                       vertexState = nullptr);

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;

private:
  std::unique_ptr<vkw::VertexInputStateCreateInfoBase> m_vertexState;
};

class BasicFragmentStage : public imvk::GraphicsPipelineStage {
public:
  BasicFragmentStage(GraphicsEngine &engine, ShaderLoader &shaderFactory,
                     std::string_view shaderName);

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;
};

} // namespace imvk::examples