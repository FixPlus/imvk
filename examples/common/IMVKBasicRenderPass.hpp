#pragma once
#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Frame.hpp"
#include "imvk/graphics/Pipeline.hpp"
#include "imvk/graphics/Swapchain.hpp"

#include "vkw/Framebuffer.hpp"
#include "vkw/Image.hpp"
#include "vkw/RenderPass.hpp"
namespace imvk::examples {

class BasicRenderPass {
public:
  BasicRenderPass(imvk::GraphicsEngine &engine);

  void run(const SwapFrame &frame, std::function<void(void)> callback);

  auto &pass() { return m_pass; }

private:
  void m_clearFramebuffers();
  void m_recreateFramebuffers();
  imvk::GraphicsEngine &m_engine;
  const imvk::Swapchain *m_currentSwapchain;
  vkw::RenderPass m_pass;
  std::vector<vkw::ImageView<vkw::COLOR, vkw::V2DA>> m_swapImageViews;
  std::vector<vkw::FrameBuffer> m_framebuffers;
};

class BasicVertexStage : public imvk::GraphicsPipelineStage {
public:
  BasicVertexStage(GraphicsEngine &engine, std::string_view shaderName);

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;
};

class BasicFragmentStage : public imvk::GraphicsPipelineStage {
public:
  BasicFragmentStage(GraphicsEngine &engine, std::string_view shaderName,
                     vkw::RenderPass &pass, unsigned subPass);

  bool isProvoking() const override { return true; }

  vkw::GraphicsPipelineCreateInfo
  initCreateInfo(const vkw::PipelineLayout &layout) const override {
    return vkw::GraphicsPipelineCreateInfo{m_pass, m_subPass, layout};
  }

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;

private:
  vkw::RenderPass &m_pass;
  unsigned m_subPass;
};

} // namespace imvk::examples