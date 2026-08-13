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

class FrameBuffer final
    : public imvk::FOENode<vkw::FrameBuffer, imvk::fon_type::ext> {
public:
  FrameBuffer(imvk::GraphicsEngine &engine, SwapchainView &sv, RenderPass &rp);

  const SwapchainView &view() const {
    return static_cast<const SwapchainView &>(*m_children.front());
  }
  const RenderPass &renderPass() const {
    return static_cast<const RenderPass &>(*m_children.back());
  }

private:
  unsigned getExtIndex(const Frame &frame) const final {
    return view().swapchain().currentImage();
  }
  void onUseAction(const Frame &frame, FObject &obj) final {
    // nothing to do.
  }
  void
  constructNew(FramedEngine &engine,
               boost::container::small_vector_base<FObject::Ptr> &res) final {
    doConstructNew(engine, view(), renderPass(), res);
  }
  static void
  doConstructNew(FramedEngine &engine, const SwapchainView &sv,
                 const RenderPass &rp,
                 boost::container::small_vector_base<FObject::Ptr> &res);
};

class BasicRenderPass {
public:
  BasicRenderPass(imvk::GraphicsEngine &engine);

  void run(GraphicsEngine::SwapFrame &frame,
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