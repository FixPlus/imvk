#include "IMVKBasicRenderPass.hpp"
#include <array>

namespace imvk::examples {

BasicRenderPass::BasicRenderPass(imvk::GraphicsEngine &engine)
    : m_engine(engine), m_currentSwapchain(&engine.swapchain()),
      m_pass(engine.context().device(), [&]() {
        std::vector<vkw::AttachmentDescription> attachments;
        auto colorFormat = engine.swapchain().images().front().format();

        auto attachmentDescription =
            vkw::AttachmentDescription{0u,
                                       colorFormat,
                                       VK_SAMPLE_COUNT_1_BIT,
                                       VK_ATTACHMENT_LOAD_OP_CLEAR,
                                       VK_ATTACHMENT_STORE_OP_STORE,
                                       VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                       VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
        attachments.push_back(attachmentDescription);

        auto subpassDescription = vkw::SubpassDescription{};
        subpassDescription.addColorAttachment(
            attachments.at(0), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        auto inputDependency = vkw::SubpassDependency{};
        inputDependency.setDstSubpass(subpassDescription);
        inputDependency.srcAccessMask = 0;
        inputDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        inputDependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        inputDependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        inputDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        auto outputDependency = vkw::SubpassDependency{};
        outputDependency.setSrcSubpass(subpassDescription);
        outputDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        outputDependency.dstAccessMask = 0;
        outputDependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        outputDependency.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        outputDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        return vkw::RenderPassCreateInfo{
            std::span<vkw::AttachmentDescription, 1>{attachments.begin(),
                                                     attachments.begin() + 1},
            {subpassDescription},
            {inputDependency, outputDependency}};
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
        std::array<vkw::ImageViewVT<vkw::V2DA> const *, 1> views = {&view};
        return vkw::FrameBuffer{m_engine.context().device(), m_pass,
                                VkExtent2D{view.image()->rawExtents().width,
                                           view.image()->rawExtents().height},
                                views};
      });
}
void BasicRenderPass::run(const SwapFrame &frame,
                          std::function<void(void)> callback) {
  auto &swapchain = frame.swapchain();

  auto &fb = m_framebuffers.at(swapchain.currentImage());
  auto &commands = frame.frame().commands();

  VkClearValue clearValue{.color = {0.8, 0.5, 0.2, 0.0}};

  commands.beginRenderPass(m_pass, fb, fb.getFullRenderArea(),
                           /*use secondary */ false,
                           std::span<const VkClearValue>{&clearValue, 1u});

  std::invoke(callback);
  commands.endRenderPass();
}
} // namespace imvk::examples