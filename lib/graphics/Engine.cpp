#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Frame.hpp"
#include "imvk/graphics/Swapchain.hpp"

#include "vkw/Surface.hpp"

namespace imvk {

GraphicsEngine::GraphicsEngine(ContextImpl &context,
                               const GraphicsEngineCreateInfo &CI)
    : FramedEngine(context,
                   QueueCapsInfo{.present = true,
                                 .graphics = true,
                                 .compute = true,
                                 .transfer = true},
                   CI.maxFramesInFlight),
      m_swapchainFactory(*CI.swapchainFactory),
      m_swapchain(std::make_unique<Swapchain>(
          context.device(), queue(),
          m_swapchainFactory.getCreateInfo(context.device()))) {
  assert(CI.maxFramesInFlight);
  m_frameSyncs.reserve(getFIFCount());

  std::ranges::transform(std::ranges::iota_view{0u, getFIFCount()},
                         std::back_inserter(m_frameSyncs),
                         [&](auto &&i) { return FrameSyncObjects(*this); });
}

std::optional<SwapFrame> GraphicsEngine::m_beginFrame() {
  assert(!m_currentFrame);
  auto &frameSync = m_frameSyncs.at(getCurrentFrameId());
  frameSync.waitIfNeeded();

  auto status = m_swapchain->acquireNextImage(
      frameSync.presentComplete, /* timeout in milliseconds*/ 1000);
  if (status == vkw::SwapChain::AcquireStatus::TIMEOUT)
    return std::nullopt;
  if (status == vkw::SwapChain::AcquireStatus::OUT_OF_DATE ||
      status == vkw::SwapChain::AcquireStatus::SUBOPTIMAL) {
    if (!m_surface_minimized())
      m_recreate_swapchain();
    return std::nullopt;
  }

  m_currentFrame = SwapFrame{beginAndGetCurrentFrame(), *m_swapchain};
  return *m_currentFrame;
}

void GraphicsEngine::m_endFrame() {
  assert(m_currentFrame);
  auto &frameSync = m_frameSyncs.at(getCurrentFrameId());
  endAndAdvanceFrame();
  vkw::SubmitInfo submitInfo{
      m_currentFrame->frame().commands(), frameSync.presentComplete,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, frameSync.renderComplete};
  auto presentInfo = vkw::PresentInfo{*m_swapchain, frameSync.renderComplete};
  auto q = queue().acquire();
  q.get().submit(submitInfo, frameSync.fence);
  q.get().present(presentInfo);
  frameSync.needFenceWait = true;
  m_currentFrame.reset();
}

void GraphicsEngine::m_recreate_swapchain() {
  queue().acquire().get().waitIdle();
  for (auto &&callback :
       m_swapChainCallbacks |
           std::views::transform(
               [](auto &&pair) -> decltype(auto) { return pair.first; }))
    std::invoke(callback);
  m_swapchain.reset();
  m_swapchain = std::make_unique<Swapchain>(
      context().device(), queue(),
      m_swapchainFactory.getCreateInfo(context().device()));
  for (auto &&callback :
       m_swapChainCallbacks |
           std::views::transform(
               [](auto &&pair) -> decltype(auto) { return pair.second; }))
    std::invoke(callback, *m_swapchain);
}

bool GraphicsEngine::m_surface_minimized() {
  auto extents =
      m_swapchainFactory.getSurface()
          .getSurfaceCapabilities(context().device().physicalDevice())
          .currentExtent;
  return extents.width == 0 || extents.height == 0;
}

GraphicsEngine::~GraphicsEngine() = default;

void GraphicsEngine::m_terminate() { queue().acquire().get().waitIdle(); }
} // namespace imvk