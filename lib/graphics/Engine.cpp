#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Swapchain.hpp"

#include <array>

namespace imvk {

SSemaphore::SSemaphore(FramedEngine &engine, Swapchain &swapchain)
    : Swapchained<vkw::Semaphore>(swapchain) {
  onConstruct(engine, /* immediate */ true);
}

FObject::Ptr SSemaphore::constructOne(FramedEngine &engine, unsigned image) {
  return engine.createObject<vkw::Semaphore>(engine.context().device());
}
GraphicsEngine::GraphicsEngine(Context &context,
                               const GraphicsEngineCreateInfo &CI)
    : FramedEngine(context,
                   QueueCapsInfo{.present = true,
                                 .graphics = true,
                                 .compute = true,
                                 .transfer = true},
                   CI.maxFramesInFlight),
      m_swapchainFactory(*CI.swapchainFactory),
      m_swapchain(createNode<Swapchain>()),
      m_renderComplete(createNode<SSemaphore>(*m_swapchain)),
      m_presentComplete(createNode<Semaphore>()) {
  assert(CI.maxFramesInFlight);
}

bool GraphicsEngine::m_aquireSwapchainImage(const Frame &frame) {
  auto status =
      m_swapchain->get().acquireNextImage(m_presentComplete->use(frame),
                                          /* timeout in milliseconds*/ 1000);
  if (status == vkw::SwapChain::AcquireStatus::TIMEOUT) {
    return false;
  }
  if (status == vkw::SwapChain::AcquireStatus::OUT_OF_DATE ||
      status == vkw::SwapChain::AcquireStatus::SUBOPTIMAL) {
    flush();
    if (!m_surface_minimized())
      m_swapchain->reconstruct(*this);
    ;

    return false;
  }
  return true;
}

bool GraphicsEngine::m_surface_minimized() {
  auto extents =
      m_swapchainFactory.getSurface()
          .getSurfaceCapabilities(context().device().physicalDevice())
          .currentExtent;
  return extents.width == 0 || extents.height == 0;
}

GraphicsEngine::~GraphicsEngine() = default;

std::optional<vkw::SubmitInfo> GraphicsEngine::onFrame(const Frame &frame) {
  if (!m_aquireSwapchainImage(frame))
    return std::nullopt;
  auto submitInfo = frameAction(frame);
  submitInfo.addWaitCondition(m_presentComplete->use(frame),
                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  submitInfo.addSignalTo(m_renderComplete->use(frame));
  return submitInfo;
}

void GraphicsEngine::postSubmit(const Frame &frame) {
  auto presentInfo =
      vkw::PresentInfo{swapchain().use(frame), m_renderComplete->use(frame)};
  queue().acquire().get().present(presentInfo);
}

bool GraphicsEngine::shouldStop() { return !midFrameAction(); }
} // namespace imvk