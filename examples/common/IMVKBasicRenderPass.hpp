#pragma once
#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Pipeline.hpp"
#include "imvk/graphics/Swapchain.hpp"

#include "vkw/Framebuffer.hpp"
#include "vkw/Image.hpp"
#include "vkw/RenderPass.hpp"

#include "IMVKShaderLoader.hpp"

namespace imvk::examples {
class RenderPass final
    : public imvk::FONode<vkw::RenderPass, imvk::fon_type::cow> {
public:
  RenderPass(imvk::GraphicsEngine &engine);

private:
  FObject::Ptr constructNew(FramedEngine &engine) noexcept final {
    return nullptr;
  }
};

class FrameBuffer final : public Swapchained<vkw::FrameBuffer> {
public:
  FrameBuffer(imvk::GraphicsEngine &engine, SwapchainView &sv, RenderPass &rp);

  const SwapchainView &view() const { return getUse<const SwapchainView>(1); }
  const RenderPass &renderPass() const { return getUse<const RenderPass>(2); }

private:
  void onUseAction(const Frame &frame, FObject &obj) final {
    // nothing to do.
  }

  FObject::Ptr constructOne(FramedEngine &engine, unsigned id) final;
};

class BasicRenderPass {
public:
  BasicRenderPass(imvk::GraphicsEngine &engine);

  void run(vkw::BufferRecorder &recorder, const imvk::Frame &frame,
           const std::function<void(vkw::RenderPassRecorder &,
                                    const imvk::Frame &)> &callback);

  auto &pass() { return m_pass; }

private:
  Ref<RenderPass> m_pass;
  Ref<SwapchainView> m_sv;
  Ref<FrameBuffer> m_framebuffer;
};

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
                     std::string_view shaderName, const vkw::RenderPass &pass);

  bool isProvoking() const override { return true; }

  vkw::GraphicsPipelineCreateInfo
  initCreateInfo(const vkw::PipelineLayout &layout) const override {
    return vkw::GraphicsPipelineCreateInfo{m_pass, layout};
  }

  void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override;

private:
  const vkw::RenderPass &m_pass;
};

} // namespace imvk::examples