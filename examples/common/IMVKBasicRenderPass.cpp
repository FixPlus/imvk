#include "IMVKBasicRenderPass.hpp"
#include <array>

namespace imvk::examples {

BasicRenderPass::BasicRenderPass(imvk::GraphicsEngine &engine)
    : m_engine(engine), m_currentSwapchain(&engine.swapchain()),
      m_pass(engine.context().device(), [&]() {
        vkw::RenderPassCreateInfoBuilder infoBuilder{1};
        auto colorFormat = engine.swapchain().images().front().format();
        auto colorID = infoBuilder.addAttachment(
            colorFormat, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        auto &subpassDescription = infoBuilder.addSubpass();
        subpassDescription.addColorAttachment(
            colorID, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        // input dependency.
        infoBuilder.addDependency(nullptr, &subpassDescription,
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                  VK_DEPENDENCY_BY_REGION_BIT);
        // output dependency.
        infoBuilder.addDependency(&subpassDescription, nullptr,
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
                                  VK_DEPENDENCY_BY_REGION_BIT);
        return vkw::RenderPassCreateInfo(std::move(infoBuilder));
      }()) {
  m_recreateFramebuffers();
  m_engine.addSwapchainCallback([this]() { m_clearFramebuffers(); },
                                [this](const imvk::Swapchain &swapchain) {
                                  m_currentSwapchain = &swapchain;
                                  m_recreateFramebuffers();
                                });
}

void BasicRenderPass::m_clearFramebuffers() {
  m_framebuffers.clear();
  m_swapImageViews.clear();
}
void BasicRenderPass::m_recreateFramebuffers() {

  std::ranges::transform(
      m_currentSwapchain->images(), std::back_inserter(m_swapImageViews),
      [&](auto &&image) {
        return vkw::ImageView<vkw::COLOR, vkw::V2DA>(
            m_engine.context().device(), image, image.format());
      });

  std::ranges::transform(
      m_swapImageViews, std::back_inserter(m_framebuffers), [&](auto &&view) {
        auto extents =
            VkExtent3D{view.image()->rawExtents().width,
                       view.image()->rawExtents().height, /* layer */ 1};
        vkw::FrameBufferInfo info{m_pass, extents};
        info.addAttachment(view);
        return vkw::FrameBuffer{info};
      });
}
void BasicRenderPass::run(
    GraphicsEngine::SwapFrame &frame,
    const std::function<void(vkw::RenderPassRecorder &, const imvk::Frame &)>
        &callback) {
  auto &swapchain = frame.swapchain();

  auto &fb = m_framebuffers.at(swapchain.currentImage());
  auto &commands = frame.frame().commands();
  auto &commandRcrd = frame.commands();

  VkClearValue clearValue{.color = {0.8, 0.5, 0.2, 0.0}};
  auto drawArea = fb.getFullRenderArea();

  auto rpRcrd = commandRcrd.beginRenderPass(
      fb, drawArea,
      /*use secondary */ false, std::span<const VkClearValue>{&clearValue, 1u});
  auto &drawAreaExtent = drawArea.extent;
  VkViewport viewport;
  viewport.height = drawAreaExtent.height;
  viewport.width = drawAreaExtent.width;
  viewport.x = viewport.y = 0.0f;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor;
  scissor.extent.width = drawAreaExtent.width;
  scissor.extent.height = drawAreaExtent.height;
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  rpRcrd.setViewports({&viewport, 1});
  rpRcrd.setScissors({&scissor, 1});

  std::invoke(callback, rpRcrd, frame.frame());
}

BasicVertexStage::BasicVertexStage(
    GraphicsEngine &engine, ShaderLoader &shaderFactory,
    std::string_view shaderName,
    std::unique_ptr<vkw::VertexInputStateCreateInfoBase> vertexState)
    : GraphicsPipelineStage(
          engine,
          [&]() {
            PipelineStage::Description desc{};
            desc.stage = VK_SHADER_STAGE_VERTEX_BIT;
            desc.shaders.emplace_back(*shaderFactory.getModule(shaderName));
            desc.sets.emplace_back(/* set*/ 0, VK_SHADER_STAGE_VERTEX_BIT,
                                   /* sets per pool*/ 1u);
            return desc;
          }()),
      m_vertexState(std::move(vertexState)) {}

void BasicVertexStage::amendCreateInfo(
    vkw::GraphicsPipelineCreateInfo &info) const {
  info.addInputAssemblyState(vkw::InputAssemblyStateCreateInfo{});
  if (m_vertexState)
    info.addVertexInputState(*m_vertexState);
}

BasicFragmentStage::BasicFragmentStage(GraphicsEngine &engine,
                                       ShaderLoader &shaderFactory,
                                       std::string_view shaderName,
                                       vkw::RenderPass &pass)
    : GraphicsPipelineStage(engine,
                            [&]() {
                              PipelineStage::Description desc{};
                              desc.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                              desc.shaders.emplace_back(
                                  *shaderFactory.getModule(shaderName));
                              return desc;
                            }()),
      m_pass(pass) {}

void BasicFragmentStage::amendCreateInfo(
    vkw::GraphicsPipelineCreateInfo &info) const {
  info.addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
  info.addDynamicState(VK_DYNAMIC_STATE_SCISSOR);
}

} // namespace imvk::examples