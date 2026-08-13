#include "IMVKBasicRenderPass.hpp"
#include <array>

namespace imvk::examples {
RenderPass::RenderPass(imvk::GraphicsEngine &engine)
    : imvk::FONode<vkw::RenderPass, imvk::fon_type::cow>(
          engine.createObject<vkw::RenderPass>(
              engine.context().device(), [&]() {
                vkw::RenderPassCreateInfoBuilder infoBuilder{1};
                auto colorFormat =
                    engine.swapchain().get().images().front().format();
                auto colorID = infoBuilder.addAttachment(
                    colorFormat, VK_SAMPLE_COUNT_1_BIT,
                    VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
                auto &subpassDescription = infoBuilder.addSubpass();
                subpassDescription.addColorAttachment(
                    colorID, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                // input dependency.
                infoBuilder.addDependency(
                    nullptr, &subpassDescription,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_DEPENDENCY_BY_REGION_BIT);
                // output dependency.
                infoBuilder.addDependency(
                    &subpassDescription, nullptr,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
                    VK_DEPENDENCY_BY_REGION_BIT);
                return vkw::RenderPassCreateInfo(std::move(infoBuilder));
              }())) {}

FrameBuffer::FrameBuffer(imvk::GraphicsEngine &engine, SwapchainView &sv,
                         RenderPass &rp)
    : imvk::FOENode<vkw::FrameBuffer, imvk::fon_type::ext>([&]() {
        std::array<FONodeBase *, 2> childs{&sv, &rp};
        return childs;
      }()) {
  onConstruct(engine);
}
void FrameBuffer::doConstructNew(
    FramedEngine &engine, const SwapchainView &sv, const RenderPass &rp,
    boost::container::small_vector_base<FObject::Ptr> &res) {
  auto total = std::size(sv.swapchain().images());
  for (auto i : std::ranges::iota_view{0ul, total}) {
    auto &view = sv.get(i);
    auto extents = VkExtent3D{view.image()->rawExtents().width,
                              view.image()->rawExtents().height, /* layer */ 1};
    vkw::FrameBufferInfo info{rp.get(), extents};
    info.addAttachment(view);
    res.push_back(engine.createObject<vkw::FrameBuffer>(info));
  }
}
BasicRenderPass::BasicRenderPass(imvk::GraphicsEngine &engine)
    : m_pass(engine.createNode<RenderPass>()),
      m_sv(engine.createNode<SwapchainView>(engine.swapchain())),
      m_framebuffer(engine.createNode<FrameBuffer>(*m_sv, *m_pass)) {}

void BasicRenderPass::run(
    GraphicsEngine::SwapFrame &frame,
    const std::function<void(vkw::RenderPassRecorder &, const imvk::Frame &)>
        &callback) {
  auto &swapchain = frame.swapchain();

  auto &fb = m_framebuffer->use(frame.frame());
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
            StageLayoutImpl::Description desc{};
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
                                       const vkw::RenderPass &pass)
    : GraphicsPipelineStage(engine,
                            [&]() {
                              StageLayoutImpl::Description desc{};
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