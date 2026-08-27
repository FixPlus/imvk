#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Swapchain.hpp"

#include <array>

namespace imvk {

vkw::Semaphore SSemaphoreImpl::constructOne(FramedEngine &engine,
                                            unsigned image) {
  return vkw::Semaphore(engine.context().device());
}

GraphicsEngine::GraphicsEngine(Context &context,
                               const GraphicsEngineCreateInfo &CI)
    : FramedEngine(context,
                   QueueCapsInfo{.present = true,
                                 .graphics = true,
                                 .compute = true,
                                 .transfer = true},
                   CI.maxFramesInFlight),
      m_swapchainFactory(*CI.swapchainFactory), m_swapchain(*this),
      m_renderComplete(*this, m_swapchain), m_presentComplete(*this) {
  assert(CI.maxFramesInFlight);
}

bool GraphicsEngine::m_aquireSwapchainImage(const Frame &frame) {
  auto status =
      m_swapchain.get().acquireNextImage(m_presentComplete->use(frame),
                                         /* timeout in milliseconds*/ 1000);
  if (status == vkw::SwapChain::AcquireStatus::TIMEOUT) {
    return false;
  }
  if (status == vkw::SwapChain::AcquireStatus::OUT_OF_DATE ||
      status == vkw::SwapChain::AcquireStatus::SUBOPTIMAL) {
    flush();
    if (!m_surface_minimized()) {
      m_swapchain->destroy(/* immediate*/ true);
      m_swapchain->construct();
    }

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

void GraphicsEngine::postSubmit(const Frame &frame) {
  auto presentInfo =
      vkw::PresentInfo{swapchain()->use(frame), m_renderComplete->use(frame)};
  queue().acquire().get().present(presentInfo);
}

} // namespace imvk