#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Swapchain.hpp"

namespace imvk {

GraphicsEngine::GraphicsEngine(Context &context,
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
                         std::back_inserter(m_frameSyncs), [&](auto &&i) {
                           return std::make_unique<FrameSyncObjects>(*this);
                         });
}

std::optional<GraphicsEngine::SwapFrame> GraphicsEngine::m_beginFrame() {
  auto semas = m_getSemaphores();

  auto status = m_swapchain->acquireNextImage(
      semas->presentComplete, /* timeout in milliseconds*/ 1000);
  if (status == vkw::SwapChain::AcquireStatus::TIMEOUT) {
    m_returnSemaphores(std::move(semas));
    return std::nullopt;
  }
  if (status == vkw::SwapChain::AcquireStatus::OUT_OF_DATE ||
      status == vkw::SwapChain::AcquireStatus::SUBOPTIMAL) {
    m_returnSemaphores(std::move(semas));
    if (!m_surface_minimized())
      m_recreate_swapchain();
    return std::nullopt;
  }

  auto frameFutureOpt = nextFrame();
  assert(frameFutureOpt);
  auto frame = frameFutureOpt->get();

  return SwapFrame{*this, std::move(frame), std::move(semas)};
}

std::unique_ptr<GraphicsEngine::FrameSyncObjects>
GraphicsEngine::m_getSemaphores() {
  if (m_frameSyncs.empty())
    m_frameSyncs.emplace_back(std::make_unique<FrameSyncObjects>(*this));
  auto ret = std::move(m_frameSyncs.back());
  m_frameSyncs.pop_back();
  return ret;
}

void GraphicsEngine::m_returnSemaphores(
    std::unique_ptr<FrameSyncObjects> &&semas) {
  m_frameSyncs.push_back(std::move(semas));
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

GraphicsEngine::FrameSyncObjects::FrameSyncObjects(GraphicsEngine &engine)
    : renderComplete(engine.context().device()),
      presentComplete(engine.context().device()) {}

void GraphicsEngine::SwapFrame::FrameEnder::operator()(
    GraphicsEngine *engine) const {
  if (!engine)
    return;
  auto id = recorder.frame.get().id();
  auto &presentComplete = semas->presentComplete;
  auto &renderComplete = semas->renderComplete;

  engine->submitFrame(
      std::move(recorder),
      [&](vkw::SubmitInfo &submitInfo) {
        submitInfo.addWaitCondition(
            presentComplete, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        submitInfo.addSignalTo(renderComplete);
      },
      [semas = std::move(semas), engine]() mutable {
        engine->m_returnSemaphores(std::move(semas));
      });
  auto presentInfo = vkw::PresentInfo{swapchain.get(), renderComplete};
  auto q = engine->queue().acquire();
  q.get().present(presentInfo);
}
} // namespace imvk